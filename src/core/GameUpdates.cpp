#include "GameExplorerCore/GameUpdates.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "GameExplorerCore/Storage.h"

namespace GameExplorer {
namespace {

constexpr const wchar_t* kSteamHost = L"api.steampowered.com";
constexpr const wchar_t* kSteamUpToDateCheckPath = L"/ISteamApps/UpToDateCheck/v1/";

// ---- Small string helpers ---------------------------------------------------

std::wstring Trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool IsDigits(const std::wstring& text) {
    if (text.empty()) return false;
    return std::all_of(text.begin(), text.end(),
                       [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
}

std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::wstring WideFromUtf8(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// Reads a UTF-8 file whole. Returns false for a missing or unreadable path.
bool ReadFileText(const std::filesystem::path& path, std::string& out) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return file.good() || file.eof();
}

bool WriteFileText(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    return file.good();
}

// ---- Minimal JSON reader (only what the Steam payload and the cache need) ----

size_t SkipJsonWhitespace(const std::wstring& json, size_t pos, size_t limit) {
    while (pos < limit && std::iswspace(json[pos]) != 0) ++pos;
    return pos;
}

size_t FindMatchingBracket(const std::wstring& json, size_t open, wchar_t opener, wchar_t closer) {
    if (open >= json.size() || json[open] != opener) return std::wstring::npos;
    int depth = 0;
    bool inString = false;
    for (size_t i = open; i < json.size(); ++i) {
        const wchar_t ch = json[i];
        if (inString) {
            if (ch == L'\\') {
                ++i;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }
        if (ch == L'"') {
            inString = true;
        } else if (ch == opener) {
            ++depth;
        } else if (ch == closer) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::wstring::npos;
}

// Position of the value that follows `"key" :` inside [from, to), or npos.
size_t FindJsonValue(const std::wstring& json, const std::wstring& key, size_t from, size_t to) {
    const std::wstring needle = L"\"" + key + L"\"";
    size_t pos = json.find(needle, from);
    while (pos != std::wstring::npos && pos + needle.size() <= to) {
        size_t cursor = json.find(L':', pos + needle.size());
        if (cursor == std::wstring::npos || cursor >= to) return std::wstring::npos;
        cursor = SkipJsonWhitespace(json, cursor + 1, to);
        if (cursor < to) return cursor;
        return std::wstring::npos;
    }
    return std::wstring::npos;
}

std::wstring ReadJsonStringAt(const std::wstring& json, size_t pos, size_t limit) {
    if (pos >= limit || json[pos] != L'"') return {};
    std::wstring out;
    for (size_t i = pos + 1; i < limit; ++i) {
        const wchar_t ch = json[i];
        if (ch == L'"') return out;
        if (ch != L'\\' || i + 1 >= limit) {
            out.push_back(ch);
            continue;
        }
        const wchar_t escaped = json[++i];
        switch (escaped) {
        case L'n': out.push_back(L'\n'); break;
        case L'r': out.push_back(L'\r'); break;
        case L't': out.push_back(L'\t'); break;
        case L'b': out.push_back(L'\b'); break;
        case L'f': out.push_back(L'\f'); break;
        case L'u': {
            if (i + 4 >= limit) return out;
            wchar_t hex[5]{ json[i + 1], json[i + 2], json[i + 3], json[i + 4], L'\0' };
            wchar_t* end = nullptr;
            const unsigned long code = std::wcstoul(hex, &end, 16);
            if (end == hex + 4 && code <= 0xFFFFUL) out.push_back(static_cast<wchar_t>(code));
            i += 4;
            break;
        }
        default: out.push_back(escaped); break;
        }
    }
    return {};
}

std::wstring ReadJsonString(const std::wstring& json, const std::wstring& key, size_t from,
                            size_t to) {
    const size_t pos = FindJsonValue(json, key, from, to);
    if (pos == std::wstring::npos) return {};
    return ReadJsonStringAt(json, pos, to);
}

bool ReadJsonBool(const std::wstring& json, const std::wstring& key, size_t from, size_t to,
                  bool& out) {
    const size_t pos = FindJsonValue(json, key, from, to);
    if (pos == std::wstring::npos) return false;
    if (json.compare(pos, 4, L"true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(pos, 5, L"false") == 0) {
        out = false;
        return true;
    }
    return false;
}

std::wstring ReadJsonIntegerText(const std::wstring& json, const std::wstring& key, size_t from,
                                 size_t to) {
    const size_t pos = FindJsonValue(json, key, from, to);
    if (pos == std::wstring::npos) return {};
    size_t start = pos;
    if (start < to && json[start] == L'-') ++start;
    size_t end = start;
    while (end < to && json[end] >= L'0' && json[end] <= L'9') ++end;
    if (end == start) return {};
    return json.substr(pos, end - pos);
}

uint64_t ToUnsigned(const std::wstring& text) {
    uint64_t value = 0;
    for (const wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return 0;
        value = value * 10ULL + static_cast<uint64_t>(ch - L'0');
    }
    return value;
}

// ---- JSON writing -----------------------------------------------------------

std::wstring EscapeJson(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 8);
    for (const wchar_t ch : text) {
        switch (ch) {
        case L'"': out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (ch < 0x20) continue;  // control characters carry no meaning in these strings
            out.push_back(ch);
            break;
        }
    }
    return out;
}

const wchar_t* StateToken(GameUpdateState state) {
    switch (state) {
    case GameUpdateState::UpToDate: return L"up_to_date";
    case GameUpdateState::UpdateAvailable: return L"update_available";
    case GameUpdateState::NotSupported: return L"not_supported";
    case GameUpdateState::Failed: return L"failed";
    case GameUpdateState::Unknown: break;
    }
    return L"unknown";
}

GameUpdateState StateFromToken(const std::wstring& token) {
    if (token == L"up_to_date") return GameUpdateState::UpToDate;
    if (token == L"update_available") return GameUpdateState::UpdateAvailable;
    if (token == L"not_supported") return GameUpdateState::NotSupported;
    if (token == L"failed") return GameUpdateState::Failed;
    return GameUpdateState::Unknown;
}

std::wstring NowIso8601Utc() {
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ", utc.wYear, utc.wMonth, utc.wDay,
               utc.wHour, utc.wMinute, utc.wSecond);
    return buffer;
}

// ---- HTTP -------------------------------------------------------------------

bool HttpGetHttps(const std::wstring& host, const std::wstring& path, std::string& body,
                  std::wstring& error) {
    body.clear();
    error.clear();

    HINTERNET session = WinHttpOpen(L"GameExplorer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error = L"WinHTTP could not start a session (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect == nullptr) {
        error = L"WinHTTP could not reach " + host + L" (error " + std::to_wstring(GetLastError()) + L").";
    } else {
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
        if (request == nullptr) {
            error = L"WinHTTP could not build the request (error " + std::to_wstring(GetLastError()) + L").";
        } else {
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE ||
                WinHttpReceiveResponse(request, nullptr) == FALSE) {
                error = L"WinHTTP request failed (error " + std::to_wstring(GetLastError()) + L").";
            } else {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                    WINHTTP_NO_HEADER_INDEX);
                if (status != 200) {
                    error = L"Steam answered with HTTP " + std::to_wstring(status) + L".";
                } else {
                    ok = true;
                    for (;;) {
                        DWORD available = 0;
                        if (WinHttpQueryDataAvailable(request, &available) == FALSE ||
                            available == 0) {
                            break;
                        }
                        std::string chunk(available, '\0');
                        DWORD read = 0;
                        if (WinHttpReadData(request, chunk.data(), available, &read) == FALSE ||
                            read == 0) {
                            break;
                        }
                        chunk.resize(read);
                        body += chunk;
                    }
                }
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);
    return ok;
}

} // namespace

std::wstring BuildSteamUpToDateCheckPath(const std::wstring& appId,
                                         const std::wstring& installedBuildId) {
    return std::wstring(kSteamUpToDateCheckPath) + L"?appid=" + Trim(appId) +
           L"&version=" + Trim(installedBuildId);
}

bool ParseSteamUpToDateCheckJson(const std::wstring& json, bool& outSuccess, bool& outUpToDate,
                                 std::wstring& outRequiredVersion, std::wstring& outMessage) {
    outSuccess = false;
    outUpToDate = false;
    outRequiredVersion.clear();
    outMessage.clear();

    const size_t response = json.find(L"\"response\"");
    if (response == std::wstring::npos) return false;
    const size_t open = json.find(L'{', response);
    if (open == std::wstring::npos) return false;
    const size_t close = FindMatchingBracket(json, open, L'{', L'}');
    if (close == std::wstring::npos) return false;

    if (!ReadJsonBool(json, L"success", open, close, outSuccess)) return false;
    ReadJsonBool(json, L"up_to_date", open, close, outUpToDate);
    outRequiredVersion = ReadJsonIntegerText(json, L"required_version", open, close);
    outMessage = ReadJsonString(json, L"message", open, close);
    return true;
}

bool QuerySteamUpToDate(const std::wstring& appId, const std::wstring& installedBuildId,
                        GameUpdateInfo& out, std::wstring& error) {
    out = GameUpdateInfo{};
    out.installedBuildId = Trim(installedBuildId);
    error.clear();

    const std::wstring trimmedAppId = Trim(appId);
    if (trimmedAppId.empty()) {
        out.state = GameUpdateState::NotSupported;
        out.message = L"Steam did not record an app id for this edition.";
        return true;
    }
    if (out.installedBuildId.empty()) {
        out.state = GameUpdateState::NotSupported;
        out.message = L"Steam did not record an installed build id for this edition.";
        return true;
    }

    std::string body;
    if (!HttpGetHttps(kSteamHost, BuildSteamUpToDateCheckPath(trimmedAppId, out.installedBuildId),
                      body, error)) {
        out.state = GameUpdateState::Failed;
        out.message = error;
        return false;
    }

    bool success = false;
    bool upToDate = false;
    std::wstring required;
    std::wstring message;
    if (!ParseSteamUpToDateCheckJson(WideFromUtf8(body), success, upToDate, required, message)) {
        out.state = GameUpdateState::Failed;
        error = L"Steam returned a response this version cannot read.";
        out.message = error;
        return false;
    }

    out.latestBuildId = required;
    out.message = message;
    if (!success) {
        // Steam's own "cannot compare" answer, for example when the build is too old or unknown.
        out.state = GameUpdateState::NotSupported;
        return true;
    }
    out.state = upToDate ? GameUpdateState::UpToDate : GameUpdateState::UpdateAvailable;
    return true;
}

GameUpdateInfo CompareLocalBuilds(const std::wstring& installedBuildId,
                                  const std::wstring& latestBuildId) {
    GameUpdateInfo info;
    info.installedBuildId = Trim(installedBuildId);
    info.latestBuildId = Trim(latestBuildId);

    if (info.installedBuildId.empty()) {
        info.state = GameUpdateState::NotSupported;
        info.message = L"This client did not record an installed build id.";
        return info;
    }
    if (info.latestBuildId.empty()) {
        info.state = GameUpdateState::NotSupported;
        info.message = L"This storefront does not keep a newer build id on this PC.";
        return info;
    }
    if (info.installedBuildId == info.latestBuildId) {
        info.state = GameUpdateState::UpToDate;
        return info;
    }

    bool newer = false;
    if (IsDigits(info.installedBuildId) && IsDigits(info.latestBuildId)) {
        const uint64_t installed = ToUnsigned(info.installedBuildId);
        const uint64_t latest = ToUnsigned(info.latestBuildId);
        newer = latest > installed;
    } else {
        // Non-numeric identities have no ordering, so a different id is the only evidence.
        newer = true;
    }
    info.state = newer ? GameUpdateState::UpdateAvailable : GameUpdateState::UpToDate;
    return info;
}

GameUpdateInfo CheckSourceUpdate(const StorefrontSource& source) {
    if (!source.installed) {
        GameUpdateInfo info;
        info.state = GameUpdateState::NotSupported;
        info.message = L"The game is not installed through this storefront.";
        return info;
    }

    if (_wcsicmp(source.storeKey.c_str(), L"steam") == 0) {
        GameUpdateInfo info;
        std::wstring error;
        const bool transportOk = QuerySteamUpToDate(source.productId, source.installedBuildId,
                                                   info, error);
        if (!transportOk && info.state == GameUpdateState::Unknown) {
            info.state = GameUpdateState::Failed;
            info.message = error;
        }
        return info;
    }

    return CompareLocalBuilds(source.installedBuildId, source.latestBuildId);
}

GameUpdateReport CheckGameUpdates(const std::vector<OwnedGame>& games) {
    GameUpdateReport report;
    report.checkedAt = NowIso8601Utc();

    for (const OwnedGame& game : games) {
        for (const StorefrontSource& source : game.sources) {
            if (!source.installed) continue;
            ++report.installedSources;

            GameUpdateRecord record;
            record.storeKey = source.storeKey;
            record.productId = source.productId;
            record.info = CheckSourceUpdate(source);

            if (record.info.state != GameUpdateState::NotSupported &&
                record.info.state != GameUpdateState::Unknown) {
                ++report.checkedSources;
            }
            if (record.info.state == GameUpdateState::UpdateAvailable) {
                ++report.updatesAvailable;
            }
            report.records.push_back(std::move(record));
        }
    }

    return report;
}

std::filesystem::path UpdateReportPath() {
    return Storage::GetUserRootPath() / L"updates.json";
}

std::wstring SerializeUpdateReport(const GameUpdateReport& report) {
    std::wstring json;
    json.reserve(256 + report.records.size() * 180);
    json += L"{\n";
    json += L"  \"version\": 1,\n";
    json += L"  \"checkedAt\": \"" + EscapeJson(report.checkedAt) + L"\",\n";
    json += L"  \"installedSources\": " + std::to_wstring(report.installedSources) + L",\n";
    json += L"  \"checkedSources\": " + std::to_wstring(report.checkedSources) + L",\n";
    json += L"  \"updatesAvailable\": " + std::to_wstring(report.updatesAvailable) + L",\n";
    json += L"  \"records\": [";

    for (size_t i = 0; i < report.records.size(); ++i) {
        const GameUpdateRecord& record = report.records[i];
        json += (i == 0 ? L"\n" : L",\n");
        json += L"    {\"store\": \"" + EscapeJson(record.storeKey) +
                L"\", \"product\": \"" + EscapeJson(record.productId) +
                L"\", \"state\": \"" + StateToken(record.info.state) +
                L"\", \"installed\": \"" + EscapeJson(record.info.installedBuildId) +
                L"\", \"latest\": \"" + EscapeJson(record.info.latestBuildId) +
                L"\", \"message\": \"" + EscapeJson(record.info.message) + L"\"}";
    }

    json += report.records.empty() ? L"]\n" : L"\n  ]\n";
    json += L"}\n";
    return json;
}

bool ParseUpdateReport(const std::wstring& json, GameUpdateReport& out) {
    out = GameUpdateReport{};
    if (json.empty()) return false;

    out.checkedAt = ReadJsonString(json, L"checkedAt", 0, json.size());
    out.installedSources =
        static_cast<size_t>(ToUnsigned(ReadJsonIntegerText(json, L"installedSources", 0, json.size())));
    out.checkedSources =
        static_cast<size_t>(ToUnsigned(ReadJsonIntegerText(json, L"checkedSources", 0, json.size())));
    out.updatesAvailable =
        static_cast<size_t>(ToUnsigned(ReadJsonIntegerText(json, L"updatesAvailable", 0, json.size())));

    const size_t recordsKey = json.find(L"\"records\"");
    if (recordsKey == std::wstring::npos) return true;
    const size_t arrayOpen = json.find(L'[', recordsKey);
    if (arrayOpen == std::wstring::npos) return true;
    const size_t arrayClose = FindMatchingBracket(json, arrayOpen, L'[', L']');
    if (arrayClose == std::wstring::npos) return true;

    size_t cursor = arrayOpen;
    while (cursor < arrayClose) {
        const size_t objectOpen = json.find(L'{', cursor);
        if (objectOpen == std::wstring::npos || objectOpen >= arrayClose) break;
        const size_t objectClose = FindMatchingBracket(json, objectOpen, L'{', L'}');
        if (objectClose == std::wstring::npos || objectClose > arrayClose) break;

        GameUpdateRecord record;
        record.storeKey = ReadJsonString(json, L"store", objectOpen, objectClose);
        record.productId = ReadJsonString(json, L"product", objectOpen, objectClose);
        record.info.state = StateFromToken(ReadJsonString(json, L"state", objectOpen, objectClose));
        record.info.installedBuildId = ReadJsonString(json, L"installed", objectOpen, objectClose);
        record.info.latestBuildId = ReadJsonString(json, L"latest", objectOpen, objectClose);
        record.info.message = ReadJsonString(json, L"message", objectOpen, objectClose);
        out.records.push_back(std::move(record));
        cursor = objectClose + 1;
    }
    return true;
}

bool SaveUpdateReport(const GameUpdateReport& report, const std::filesystem::path& path) {
    return WriteFileText(path, Utf8FromWide(SerializeUpdateReport(report)));
}

bool LoadUpdateReport(GameUpdateReport& out, const std::filesystem::path& path) {
    out = GameUpdateReport{};
    std::string bytes;
    if (!ReadFileText(path, bytes) || bytes.empty()) return false;
    return ParseUpdateReport(WideFromUtf8(bytes), out);
}

const GameUpdateRecord* FindUpdateRecord(const GameUpdateReport& report,
                                         const std::wstring& storeKey,
                                         const std::wstring& productId) {
    for (const GameUpdateRecord& record : report.records) {
        if (_wcsicmp(record.storeKey.c_str(), storeKey.c_str()) == 0 &&
            record.productId == productId) {
            return &record;
        }
    }
    return nullptr;
}

} // namespace GameExplorer
