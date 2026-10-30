#include "GameExplorerCore/Notifications.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Settings.h"

namespace GameExplorer {
namespace {

constexpr size_t kMaxResponseBytes = 8u * 1024u * 1024u;
constexpr int64_t kNewsWindowSeconds = 30 * 24 * 60 * 60;
constexpr size_t kMaxPerFeed = 12;

// ---- String helpers ---------------------------------------------------------

std::wstring Trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
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

// ---- Minimal JSON reader ----------------------------------------------------

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

size_t FindJsonValue(const std::wstring& json, const std::wstring& key, size_t from, size_t to) {
    const std::wstring needle = L"\"" + key + L"\"";
    const size_t pos = json.find(needle, from);
    if (pos == std::wstring::npos || pos + needle.size() > to) return std::wstring::npos;
    size_t cursor = json.find(L':', pos + needle.size());
    if (cursor == std::wstring::npos || cursor >= to) return std::wstring::npos;
    cursor = SkipJsonWhitespace(json, cursor + 1, to);
    return cursor < to ? cursor : std::wstring::npos;
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

int64_t ToInt64(const std::wstring& text) {
    if (text.empty()) return 0;
    return static_cast<int64_t>(std::wcstoll(text.c_str(), nullptr, 10));
}

// Every object inside the array that starts at `open`, as [start, end) ranges.
struct JsonRange {
    size_t begin{ 0 };
    size_t end{ 0 };
};

std::vector<JsonRange> JsonArrayObjects(const std::wstring& json, size_t open, size_t close) {
    std::vector<JsonRange> objects;
    size_t cursor = open;
    while (cursor < close) {
        const size_t objectOpen = json.find(L'{', cursor);
        if (objectOpen == std::wstring::npos || objectOpen >= close) break;
        const size_t objectClose = FindMatchingBracket(json, objectOpen, L'{', L'}');
        if (objectClose == std::wstring::npos || objectClose > close) break;
        objects.push_back({ objectOpen, objectClose });
        cursor = objectClose + 1;
    }
    return objects;
}

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
            if (ch < 0x20) continue;
            out.push_back(ch);
            break;
        }
    }
    return out;
}

// ---- Markup and entity cleanup ----------------------------------------------

void AppendUtf8CodePoint(std::wstring& out, unsigned long code) {
    if (code == 0) return;
    if (code <= 0x7FUL) {
        out.push_back(static_cast<wchar_t>(code));
    } else if (code <= 0xFFFFUL) {
        out.push_back(static_cast<wchar_t>(code));
    } else if (code <= 0x10FFFFUL) {
        const unsigned long value = code - 0x10000UL;
        out.push_back(static_cast<wchar_t>(0xD800UL + (value >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00UL + (value & 0x3FFUL)));
    }
}

// Decodes the handful of entities that appear in storefront prose. Unknown entities stay as-is so
// nothing is silently dropped.
std::wstring DecodeHtmlEntities(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] != L'&') {
            out.push_back(text[i++]);
            continue;
        }
        const size_t semicolon = text.find(L';', i + 1);
        if (semicolon == std::wstring::npos || semicolon - i > 10) {
            out.push_back(text[i++]);
            continue;
        }
        const std::wstring entity = text.substr(i + 1, semicolon - i - 1);
        if (entity == L"amp") {
            out.push_back(L'&');
        } else if (entity == L"lt") {
            out.push_back(L'<');
        } else if (entity == L"gt") {
            out.push_back(L'>');
        } else if (entity == L"quot") {
            out.push_back(L'"');
        } else if (entity == L"apos" || entity == L"#39") {
            out.push_back(L'\'');
        } else if (entity == L"nbsp") {
            out.push_back(L' ');
        } else if (entity.size() > 1 && entity[0] == L'#') {
            const bool hex = entity[1] == L'x' || entity[1] == L'X';
            wchar_t* end = nullptr;
            const unsigned long code =
                std::wcstoul(entity.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
            AppendUtf8CodePoint(out, code);
        } else {
            out += text.substr(i, semicolon - i + 1);
        }
        i = semicolon + 1;
    }
    return out;
}

// Removes tags and collapses whitespace so a storefront's HTML reads as one plain paragraph.
std::wstring PlainText(const std::wstring& markup) {
    std::wstring stripped;
    stripped.reserve(markup.size());
    bool inTag = false;
    for (const wchar_t ch : markup) {
        if (ch == L'<') {
            inTag = true;
            stripped.push_back(L' ');
        } else if (ch == L'>') {
            inTag = false;
        } else if (!inTag) {
            stripped.push_back(ch);
        }
    }

    std::wstring out;
    out.reserve(stripped.size());
    bool lastWasSpace = true;
    for (const wchar_t ch : DecodeHtmlEntities(stripped)) {
        const bool isSpace = std::iswspace(ch) != 0;
        if (isSpace) {
            if (!lastWasSpace) out.push_back(L' ');
        } else {
            out.push_back(ch);
        }
        lastWasSpace = isSpace;
    }
    return Trim(out);
}

std::wstring TruncateText(std::wstring text, size_t limit) {
    if (text.size() <= limit) return text;
    // Cut at a word boundary when one is close enough to the limit.
    const size_t space = text.rfind(L' ', limit);
    if (space != std::wstring::npos && space > limit / 2) text.resize(space);
    else text.resize(limit);
    return Trim(text) + L"\u2026";
}

// ---- Dates ------------------------------------------------------------------

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's civil algorithm).
int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153u * (month > 2 ? month - 3u : month + 9u) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

int64_t UnixFromParts(int year, unsigned month, unsigned day, unsigned hour, unsigned minute,
                      unsigned second) {
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    const int64_t days = DaysFromCivil(year, month, day);
    return days * 86400 + static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 +
           static_cast<int64_t>(second);
}

bool ReadNumber(const std::wstring& text, size_t pos, size_t digits, unsigned& value) {
    if (pos + digits > text.size()) return false;
    value = 0;
    for (size_t i = 0; i < digits; ++i) {
        const wchar_t ch = text[pos + i];
        if (ch < L'0' || ch > L'9') return false;
        value = value * 10u + static_cast<unsigned>(ch - L'0');
    }
    return true;
}

// Parses the `YYYY-MM-DD[T ]HH:MM:SS` shape used by GamerPower and Epic. A trailing zone is
// applied only when it is numeric; named zones are treated as UTC because the feed only feeds a
// recency window.
int64_t ParseIsoLikeDate(const std::wstring& text) {
    const std::wstring value = Trim(text);
    if (value.size() < 19) return 0;
    unsigned year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!ReadNumber(value, 0, 4, year) || value[4] != L'-' || !ReadNumber(value, 5, 2, month) ||
        value[7] != L'-' || !ReadNumber(value, 8, 2, day)) {
        return 0;
    }
    if (value[10] != L'T' && value[10] != L' ') return 0;
    if (!ReadNumber(value, 11, 2, hour) || value[13] != L':' ||
        !ReadNumber(value, 14, 2, minute) || value[16] != L':' ||
        !ReadNumber(value, 17, 2, second)) {
        return 0;
    }
    int64_t result = UnixFromParts(static_cast<int>(year), month, day, hour, minute, second);

    if (value.size() >= 25) {
        const wchar_t sign = value[19];
        unsigned offsetHour = 0, offsetMinute = 0;
        if ((sign == L'+' || sign == L'-') && value[22] == L':' &&
            ReadNumber(value, 20, 2, offsetHour) && ReadNumber(value, 23, 2, offsetMinute)) {
            const int64_t offset =
                static_cast<int64_t>(offsetHour) * 3600 + static_cast<int64_t>(offsetMinute) * 60;
            result += sign == L'+' ? -offset : offset;
        }
    }
    return result;
}

const wchar_t* const kMonthNames[] = { L"jan", L"feb", L"mar", L"apr", L"may", L"jun",
                                       L"jul", L"aug", L"sep", L"oct", L"nov", L"dec" };

bool IsBbCodeTag(const std::wstring& token) {
    // Only Steam's documented BBCode names are removed; free-form bracketed prose such as
    // "[Update]" stays intact.
    static const wchar_t* const kTags[] = {
        L"b", L"i", L"u", L"s", L"url", L"h1", L"h2", L"h3", L"list", L"ol", L"ul", L"*",
        L"img", L"quote", L"code", L"spoiler", L"noparse", L"hr", L"strike", L"table", L"tr",
        L"td", L"th", L"p", L"pre", L"color", L"size", L"left", L"right", L"center", L"video",
        L"youtube", L"previewyoutube", L"bilibili"
    };

    std::wstring name = token;
    if (!name.empty() && name.front() == L'/') name.erase(0, 1);
    const size_t equals = name.find(L'=');
    if (equals != std::wstring::npos) name.resize(equals);

    for (const wchar_t* tag : kTags) {
        if (_wcsicmp(name.c_str(), tag) == 0) return true;
    }
    return false;
}

std::wstring StripBbCode(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const size_t close = text[i] == L'[' ? text.find(L']', i + 1) : std::wstring::npos;
        if (close != std::wstring::npos && close - i <= 32 &&
            IsBbCodeTag(text.substr(i + 1, close - i - 1))) {
            i = close + 1;
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

// Parses RSS `Wed, 28 Oct 2026 07:55:02 +0200` (the day name and zone are optional).
int64_t ParseRfc822Date(const std::wstring& text) {
    std::wstring value = Trim(text);
    const size_t comma = value.find(L',');
    if (comma != std::wstring::npos) value = Trim(value.substr(comma + 1));

    // Split the remaining `28 Oct 2026 07:55:02 +0200` into whitespace-separated tokens. Parsing
    // tokens keeps this independent of the C runtime's scanf field handling and of the current
    // locale, which `%s` in wide scanf does not guarantee.
    std::vector<std::wstring> tokens;
    for (size_t i = 0; i < value.size();) {
        while (i < value.size() && std::iswspace(value[i]) != 0) ++i;
        const size_t start = i;
        while (i < value.size() && std::iswspace(value[i]) == 0) ++i;
        if (i > start) tokens.push_back(value.substr(start, i - start));
    }
    if (tokens.size() < 4 || tokens[3].size() < 8) return 0;

    unsigned day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (!ReadNumber(tokens[0], 0, tokens[0].size(), day) || day == 0 || day > 31) return 0;

    unsigned month = 0;
    for (unsigned i = 0; i < 12; ++i) {
        if (_wcsicmp(tokens[1].c_str(), kMonthNames[i]) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == 0) return 0;
    if (!ReadNumber(tokens[2], 0, tokens[2].size(), year) || year < 1970) return 0;
    if (tokens[3][2] != L':' || tokens[3][5] != L':') return 0;
    if (!ReadNumber(tokens[3], 0, 2, hour) || !ReadNumber(tokens[3], 3, 2, minute) ||
        !ReadNumber(tokens[3], 6, 2, second)) {
        return 0;
    }

    int64_t result = UnixFromParts(static_cast<int>(year), month, day, hour, minute, second);

    // A numeric trailing offset is honoured; a named zone is left as UTC.
    const size_t zone = value.find_last_of(L' ');
    if (zone != std::wstring::npos && zone + 5 <= value.size()) {
        const std::wstring tail = value.substr(zone + 1);
        if (tail.size() == 5 && (tail[0] == L'+' || tail[0] == L'-')) {
            unsigned oh = 0, om = 0;
            if (ReadNumber(tail, 1, 2, oh) && ReadNumber(tail, 3, 2, om)) {
                const int64_t offset = static_cast<int64_t>(oh) * 3600 + static_cast<int64_t>(om) * 60;
                result += tail[0] == L'+' ? -offset : offset;
            }
        }
    }
    return result;
}

int64_t ParseAnyDate(const std::wstring& text) {
    const int64_t iso = ParseIsoLikeDate(text);
    if (iso != 0) return iso;
    return ParseRfc822Date(text);
}

int64_t NowUnix() {
    FILETIME fileTime{};
    GetSystemTimeAsFileTime(&fileTime);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    return static_cast<int64_t>(ticks.QuadPart / 10'000'000ULL) - 11'644'473'600LL;
}

std::wstring NowIso8601Utc() {
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ", utc.wYear, utc.wMonth, utc.wDay,
               utc.wHour, utc.wMinute, utc.wSecond);
    return buffer;
}

// ---- RSS helpers ------------------------------------------------------------

std::wstring RssTagText(const std::wstring& block, const std::wstring& tag) {
    const std::wstring open = L"<" + tag;
    size_t start = block.find(open);
    if (start == std::wstring::npos) return {};
    start = block.find(L'>', start);
    if (start == std::wstring::npos) return {};
    const std::wstring close = L"</" + tag + L">";
    const size_t end = block.find(close, start + 1);
    if (end == std::wstring::npos) return {};
    std::wstring value = Trim(block.substr(start + 1, end - start - 1));
    if (value.rfind(L"<![CDATA[", 0) == 0) {
        value = Trim(value.substr(9));
        if (value.size() >= 3 && value.compare(value.size() - 3, 3, L"]]>") == 0) {
            value.resize(value.size() - 3);
        }
    }
    return value;
}

// ---- HTTP -------------------------------------------------------------------

bool HttpGetUrl(const std::wstring& url, std::string& body, std::wstring& error) {
    body.clear();
    error.clear();

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (WinHttpCrackUrl(url.c_str(), 0, 0, &parts) == FALSE) {
        error = L"The request URL could not be parsed.";
        return false;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0 && parts.lpszExtraInfo != nullptr) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) GameExplorer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error = L"WinHTTP could not start a session (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host.c_str(),
                                       secure ? INTERNET_DEFAULT_HTTPS_PORT
                                              : INTERNET_DEFAULT_HTTP_PORT,
                                       0);
    if (connect == nullptr) {
        error = L"WinHTTP could not reach " + host + L" (error " + std::to_wstring(GetLastError()) +
                L").";
    } else {
        HINTERNET request = WinHttpOpenRequest(
            connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
        if (request == nullptr) {
            error = L"WinHTTP could not build the request (error " +
                    std::to_wstring(GetLastError()) + L").";
        } else {
            const BOOL sent =
                WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            if (sent == FALSE || WinHttpReceiveResponse(request, nullptr) == FALSE) {
                error = L"WinHTTP request to " + host + L" failed (error " +
                        std::to_wstring(GetLastError()) + L").";
            } else {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                    WINHTTP_NO_HEADER_INDEX);
                if (status != 200) {
                    error = host + L" answered with HTTP " + std::to_wstring(status) + L".";
                } else {
                    ok = true;
                    for (;;) {
                        DWORD available = 0;
                        if (WinHttpQueryDataAvailable(request, &available) == FALSE ||
                            available == 0) {
                            break;
                        }
                        if (body.size() + available > kMaxResponseBytes) {
                            ok = false;
                            error = host + L" sent more data than this version accepts.";
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
    if (!ok) body.clear();
    return ok;
}

} // namespace

// ---- Kind tokens and labels -------------------------------------------------

const wchar_t* NotificationKindToken(NotificationKind kind) {
    switch (kind) {
    case NotificationKind::GameUpdate: return L"update";
    case NotificationKind::News: return L"news";
    case NotificationKind::FreeGame: return L"free_game";
    case NotificationKind::Promotion: return L"promotion";
    }
    return L"news";
}

bool NotificationKindFromToken(const std::wstring& token, NotificationKind& out) {
    if (token == L"update") out = NotificationKind::GameUpdate;
    else if (token == L"news") out = NotificationKind::News;
    else if (token == L"free_game") out = NotificationKind::FreeGame;
    else if (token == L"promotion") out = NotificationKind::Promotion;
    else return false;
    return true;
}

const wchar_t* NotificationKindLabel(NotificationKind kind) {
    switch (kind) {
    case NotificationKind::GameUpdate: return L"Update available";
    case NotificationKind::News: return L"Store news";
    case NotificationKind::FreeGame: return L"Free game";
    case NotificationKind::Promotion: return L"Deal & promotion";
    }
    return L"News";
}

bool NotificationKindEnabled(NotificationKind kind, const NotificationSettings& settings) {
    switch (kind) {
    case NotificationKind::GameUpdate: return settings.gameUpdates;
    case NotificationKind::News: return settings.news;
    case NotificationKind::FreeGame: return settings.freeGames;
    case NotificationKind::Promotion: return settings.promotions;
    }
    return false;
}

NotificationSettings NotificationSettingsFrom(const AppSettings& settings) {
    NotificationSettings out;
    out.gameUpdates = settings.notifyGameUpdates;
    out.news = settings.notifyNews;
    out.steamNews = settings.notifySteamNews;
    out.gogNews = settings.notifyGogNews;
    out.epicNews = settings.notifyEpicNews;
    out.xboxNews = settings.notifyXboxNews;
    out.freeGames = settings.notifyFreeGames;
    out.promotions = settings.notifyPromotions;
    return out;
}

// ---- Feed cache -------------------------------------------------------------

std::filesystem::path NotificationFeedPath() {
    return Storage::GetUserRootPath() / L"notifications.json";
}

std::filesystem::path NotificationSeenPath() {
    return Storage::GetUserRootPath() / L"notifications-seen.json";
}

std::wstring SerializeNotificationFeed(const NotificationFeed& feed) {
    std::wstring json;
    json.reserve(256 + feed.items.size() * 260);
    json += L"{\n";
    json += L"  \"version\": 1,\n";
    json += L"  \"generatedAt\": \"" + EscapeJson(feed.generatedAt) + L"\",\n";
    json += L"  \"items\": [";

    for (size_t i = 0; i < feed.items.size(); ++i) {
        const NotificationItem& item = feed.items[i];
        json += (i == 0 ? L"\n" : L",\n");
        json += L"    {\"id\": \"" + EscapeJson(item.id) + L"\", \"kind\": \"" +
                NotificationKindToken(item.kind) + L"\", \"source\": \"" +
                EscapeJson(item.source) + L"\", \"game\": \"" + EscapeJson(item.gameTitle) +
                L"\", \"title\": \"" + EscapeJson(item.title) + L"\", \"body\": \"" +
                EscapeJson(item.body) + L"\", \"url\": \"" + EscapeJson(item.url) +
                L"\", \"published\": " + std::to_wstring(item.publishedUtc) + L"}";
    }

    json += feed.items.empty() ? L"]\n" : L"\n  ]\n";
    json += L"}\n";
    return json;
}

bool ParseNotificationFeed(const std::wstring& json, NotificationFeed& out) {
    out = NotificationFeed{};
    if (json.empty()) return false;

    out.generatedAt = ReadJsonString(json, L"generatedAt", 0, json.size());

    const size_t itemsKey = json.find(L"\"items\"");
    if (itemsKey == std::wstring::npos) return true;
    const size_t arrayOpen = json.find(L'[', itemsKey);
    if (arrayOpen == std::wstring::npos) return true;
    const size_t arrayClose = FindMatchingBracket(json, arrayOpen, L'[', L']');
    if (arrayClose == std::wstring::npos) return true;

    for (const JsonRange& range : JsonArrayObjects(json, arrayOpen, arrayClose)) {
        NotificationItem item;
        item.id = ReadJsonString(json, L"id", range.begin, range.end);
        if (item.id.empty()) continue;
        const bool kindKnown =
            NotificationKindFromToken(ReadJsonString(json, L"kind", range.begin, range.end),
                                      item.kind);
        (void)kindKnown;
        item.source = ReadJsonString(json, L"source", range.begin, range.end);
        item.gameTitle = ReadJsonString(json, L"game", range.begin, range.end);
        item.title = ReadJsonString(json, L"title", range.begin, range.end);
        item.body = ReadJsonString(json, L"body", range.begin, range.end);
        item.url = ReadJsonString(json, L"url", range.begin, range.end);
        item.publishedUtc = ToInt64(ReadJsonIntegerText(json, L"published", range.begin, range.end));
        out.items.push_back(std::move(item));
    }
    return true;
}

bool SaveNotificationFeed(const NotificationFeed& feed, const std::filesystem::path& path) {
    return WriteFileText(path, Utf8FromWide(SerializeNotificationFeed(feed)));
}

bool LoadNotificationFeed(NotificationFeed& out, const std::filesystem::path& path) {
    out = NotificationFeed{};
    std::string bytes;
    if (!ReadFileText(path, bytes) || bytes.empty()) return false;
    return ParseNotificationFeed(WideFromUtf8(bytes), out);
}

std::vector<std::wstring> ParseSeenIds(const std::wstring& json) {
    std::vector<std::wstring> ids;
    if (json.empty()) return ids;

    const size_t arrayOpen = json.find(L'[');
    if (arrayOpen == std::wstring::npos) return ids;
    const size_t arrayClose = FindMatchingBracket(json, arrayOpen, L'[', L']');
    if (arrayClose == std::wstring::npos) return ids;

    size_t cursor = arrayOpen + 1;
    while (cursor < arrayClose) {
        const size_t quote = json.find(L'"', cursor);
        if (quote == std::wstring::npos || quote >= arrayClose) break;
        const std::wstring id = ReadJsonStringAt(json, quote, arrayClose);

        // Advance past this string literal so the next iteration reads the following id.
        size_t close = quote + 1;
        while (close < arrayClose && json[close] != L'"') {
            close += json[close] == L'\\' ? 2 : 1;
        }
        if (!id.empty()) ids.push_back(id);
        cursor = close + 1;
    }
    return ids;
}

std::wstring SerializeSeenIds(const std::vector<std::wstring>& ids) {
    std::wstring json = L"{\n  \"version\": 1,\n  \"seen\": [";
    for (size_t i = 0; i < ids.size(); ++i) {
        json += (i == 0 ? L"\n" : L",\n");
        json += L"    \"" + EscapeJson(ids[i]) + L"\"";
    }
    json += ids.empty() ? L"]\n}\n" : L"\n  ]\n}\n";
    return json;
}

bool LoadSeenIds(std::vector<std::wstring>& out, const std::filesystem::path& path) {
    out.clear();
    std::string bytes;
    if (!ReadFileText(path, bytes) || bytes.empty()) return false;
    out = ParseSeenIds(WideFromUtf8(bytes));
    return true;
}

bool SaveSeenIds(const std::vector<std::wstring>& ids, const std::filesystem::path& path) {
    return WriteFileText(path, Utf8FromWide(SerializeSeenIds(ids)));
}

bool IsNotificationSeen(const std::vector<std::wstring>& seen, const std::wstring& id) {
    return std::find(seen.begin(), seen.end(), id) != seen.end();
}

// ---- GamerPower -------------------------------------------------------------

std::vector<NotificationItem> ParseGamerPowerGiveaways(const std::wstring& json) {
    std::vector<NotificationItem> items;
    const size_t arrayOpen = json.find(L'[');
    if (arrayOpen == std::wstring::npos) return items;
    const size_t arrayClose = FindMatchingBracket(json, arrayOpen, L'[', L']');
    if (arrayClose == std::wstring::npos) return items;

    for (const JsonRange& range : JsonArrayObjects(json, arrayOpen, arrayClose)) {
        const std::wstring type = ReadJsonString(json, L"type", range.begin, range.end);
        NotificationKind kind{};
        if (_wcsicmp(type.c_str(), L"Game") == 0) {
            kind = NotificationKind::FreeGame;
        } else if (_wcsicmp(type.c_str(), L"Loot") == 0 || _wcsicmp(type.c_str(), L"Beta") == 0) {
            kind = NotificationKind::Promotion;
        } else {
            continue;
        }

        const std::wstring status = ReadJsonString(json, L"status", range.begin, range.end);
        if (!status.empty() && _wcsicmp(status.c_str(), L"Active") != 0) continue;

        NotificationItem item;
        item.kind = kind;
        item.source = L"GamerPower";
        item.title = PlainText(ReadJsonString(json, L"title", range.begin, range.end));
        if (item.title.empty()) continue;

        const std::wstring numericId = ReadJsonIntegerText(json, L"id", range.begin, range.end);
        std::wstring url = ReadJsonString(json, L"open_giveaway_url", range.begin, range.end);
        if (url.empty()) url = ReadJsonString(json, L"gamerpower_url", range.begin, range.end);

        // Filter out giveaways from blocked storefronts (IndieGala)
        std::wstring lowerTitle = item.title;
        std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::towlower);
        std::wstring lowerUrl = url;
        std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::towlower);
        if (lowerTitle.find(L"indiegala") != std::wstring::npos ||
            lowerUrl.find(L"indiegala.com") != std::wstring::npos) {
            continue;
        }

        item.id = L"gamerpower:" + (numericId.empty() ? url : numericId);
        item.url = url;
        item.body = TruncateText(PlainText(ReadJsonString(json, L"description", range.begin,
                                                             range.end)),
                                 260);
        item.publishedUtc =
            ParseAnyDate(ReadJsonString(json, L"published_date", range.begin, range.end));
        items.push_back(std::move(item));
    }
    return items;
}

// ---- Steam ------------------------------------------------------------------

std::vector<NotificationItem> ParseSteamNewsItems(const std::wstring& json,
                                                  const std::wstring& gameTitle,
                                                  const std::wstring& appId) {
    std::vector<NotificationItem> items;
    const size_t newsKey = json.find(L"\"newsitems\"");
    if (newsKey == std::wstring::npos) return items;
    const size_t arrayOpen = json.find(L'[', newsKey);
    if (arrayOpen == std::wstring::npos) return items;
    const size_t arrayClose = FindMatchingBracket(json, arrayOpen, L'[', L']');
    if (arrayClose == std::wstring::npos) return items;

    for (const JsonRange& range : JsonArrayObjects(json, arrayOpen, arrayClose)) {
        NotificationItem item;
        item.kind = NotificationKind::News;
        item.source = L"Steam";
        item.gameTitle = gameTitle;
        item.title = PlainText(ReadJsonString(json, L"title", range.begin, range.end));
        if (item.title.empty()) continue;

        const std::wstring gid = ReadJsonString(json, L"gid", range.begin, range.end);
        item.id = L"steam:" + appId + L":" + (gid.empty() ? item.title : gid);
        item.url = ReadJsonString(json, L"url", range.begin, range.end);
        item.body = TruncateText(
            StripBbCode(PlainText(ReadJsonString(json, L"contents", range.begin, range.end))), 260);
        item.publishedUtc =
            ToInt64(ReadJsonIntegerText(json, L"date", range.begin, range.end));
        items.push_back(std::move(item));
    }
    return items;
}

// ---- RSS --------------------------------------------------------------------

std::vector<NotificationItem> ParseRssNewsItems(const std::wstring& xml,
                                                const std::wstring& source, size_t limit) {
    std::vector<NotificationItem> items;
    const std::wstring open = L"<item";
    const std::wstring close = L"</item>";

    size_t cursor = 0;
    while (items.size() < limit) {
        const size_t start = xml.find(open, cursor);
        if (start == std::wstring::npos) break;
        const size_t end = xml.find(close, start);
        if (end == std::wstring::npos) break;
        const std::wstring block = xml.substr(start, end - start);
        cursor = end + close.size();

        NotificationItem item;
        item.kind = NotificationKind::News;
        item.source = source;
        item.title = PlainText(RssTagText(block, L"title"));
        if (item.title.empty()) continue;

        const std::wstring link = Trim(RssTagText(block, L"link"));
        const std::wstring guid = Trim(RssTagText(block, L"guid"));
        item.url = link.empty() ? guid : link;
        item.id = source + L":" + (guid.empty() ? (link.empty() ? item.title : link) : guid);
        item.body = TruncateText(PlainText(RssTagText(block, L"description")), 260);
        item.publishedUtc = ParseAnyDate(RssTagText(block, L"pubDate"));
        items.push_back(std::move(item));
    }
    return items;
}

// ---- Epic -------------------------------------------------------------------

std::vector<NotificationItem> ParseEpicBlogPosts(const std::wstring& json, size_t limit) {
    std::vector<NotificationItem> items;
    const size_t arrayOpen = json.find(L'[');
    if (arrayOpen == std::wstring::npos) return items;
    const size_t arrayClose = FindMatchingBracket(json, arrayOpen, L'[', L']');
    if (arrayClose == std::wstring::npos) return items;

    for (const JsonRange& range : JsonArrayObjects(json, arrayOpen, arrayClose)) {
        if (items.size() >= limit) break;

        NotificationItem item;
        item.kind = NotificationKind::News;
        item.source = L"Epic Games";
        item.title = PlainText(ReadJsonString(json, L"title", range.begin, range.end));
        if (item.title.empty()) continue;

        const std::wstring relative = ReadJsonString(json, L"url", range.begin, range.end);
        item.url = relative.empty() ? std::wstring() : L"https://store.epicgames.com" + relative;
        item.id = L"epic:" + (relative.empty() ? item.title : relative);
        item.body = TruncateText(
            PlainText(ReadJsonString(json, L"content", range.begin, range.end)), 260);
        item.publishedUtc = ParseAnyDate(ReadJsonString(json, L"date", range.begin, range.end));
        items.push_back(std::move(item));
    }
    return items;
}

// ---- Feed construction ------------------------------------------------------

void AppendUpdateNotifications(const GameUpdateReport& report, const std::vector<OwnedGame>& games,
                               std::vector<NotificationItem>& items) {
    for (const GameUpdateRecord& record : report.records) {
        if (record.info.state != GameUpdateState::UpdateAvailable) continue;

        NotificationItem item;
        item.kind = NotificationKind::GameUpdate;
        item.id = L"update:" + record.storeKey + L":" + record.productId;
        item.source = record.storeKey;

        for (const OwnedGame& game : games) {
            bool matched = false;
            for (const StorefrontSource& source : game.sources) {
                if (_wcsicmp(source.storeKey.c_str(), record.storeKey.c_str()) == 0 &&
                    source.productId == record.productId) {
                    matched = true;
                    break;
                }
            }
            if (matched) {
                item.gameTitle = game.title;
                item.url = game.storePageUrl;
                break;
            }
        }

        item.title = item.gameTitle.empty() ? L"Update available" : item.gameTitle;
        item.body = L"A newer build is available from this storefront.";
        if (!record.info.installedBuildId.empty()) {
            item.body += L" Installed build " + record.info.installedBuildId;
            if (!record.info.latestBuildId.empty()) {
                item.body += L", latest build " + record.info.latestBuildId;
            }
            item.body += L".";
        }
        items.push_back(std::move(item));
    }
}

NotificationFeed BuildNotificationFeed(std::vector<NotificationItem> items,
                                       const NotificationSettings& settings) {
    NotificationFeed feed;
    feed.generatedAt = NowIso8601Utc();

    std::vector<std::wstring> seenIds;
    for (NotificationItem& item : items) {
        if (item.id.empty() || item.title.empty()) continue;
        if (!NotificationKindEnabled(item.kind, settings)) continue;
        if (std::find(seenIds.begin(), seenIds.end(), item.id) != seenIds.end()) continue;
        seenIds.push_back(item.id);
        feed.items.push_back(std::move(item));
    }

    std::stable_sort(feed.items.begin(), feed.items.end(),
                     [](const NotificationItem& a, const NotificationItem& b) {
                         if (a.publishedUtc != b.publishedUtc) {
                             return a.publishedUtc > b.publishedUtc;
                         }
                         return a.title < b.title;
                     });
    return feed;
}

NotificationFeed CollectNotifications(const std::vector<OwnedGame>& games,
                                      const NotificationSettings& settings,
                                      GameUpdateReport* updates, std::wstring* error) {
    if (error != nullptr) error->clear();
    std::vector<NotificationItem> items;

    GameUpdateReport localReport;
    GameUpdateReport& report = updates != nullptr ? *updates : localReport;
    report = CheckGameUpdates(games);
    AppendUpdateNotifications(report, games, items);

    // Only storefronts with an installed edition are queried, so a player who never installed a
    // store's client does not receive that store's marketing feed.
    bool steamInstalled = false;
    bool gogInstalled = false;
    bool epicInstalled = false;
    bool xboxInstalled = false;
    std::vector<std::wstring> steamAppIds;
    for (const OwnedGame& game : games) {
        for (const StorefrontSource& source : game.sources) {
            if (!source.installed) continue;
            if (_wcsicmp(source.storeKey.c_str(), L"steam") == 0) {
                steamInstalled = true;
                if (!source.productId.empty() &&
                    std::find(steamAppIds.begin(), steamAppIds.end(), source.productId) ==
                        steamAppIds.end()) {
                    steamAppIds.push_back(source.productId);
                }
            } else if (_wcsicmp(source.storeKey.c_str(), L"gog") == 0) {
                gogInstalled = true;
            } else if (_wcsicmp(source.storeKey.c_str(), L"epic") == 0) {
                epicInstalled = true;
            } else if (_wcsicmp(source.storeKey.c_str(), L"xbox") == 0) {
                xboxInstalled = true;
            }
        }
    }

    const int64_t now = NowUnix();
    auto keepFresh = [&](std::vector<NotificationItem>& feedItems) {
        feedItems.erase(std::remove_if(feedItems.begin(), feedItems.end(),
                                       [&](const NotificationItem& item) {
                                           return item.publishedUtc != 0 &&
                                                  item.publishedUtc < now - kNewsWindowSeconds;
                                       }),
                        feedItems.end());
    };

    std::wstring transportError;
    auto noteError = [&](const std::wstring& text) {
        if (transportError.empty()) transportError = text;
    };

    if (settings.news && settings.steamNews && steamInstalled) {
        // Steam publishes the only official per-game feed, so it is the one source that can say
        // which owned game an item belongs to. Developer announcements are requested explicitly;
        // they are the feed Steam itself surfaces as the game's news.
        size_t fetched = 0;
        for (const std::wstring& appId : steamAppIds) {
            if (fetched >= 40) break;
            ++fetched;
            std::string body;
            std::wstring errorText;
            const std::wstring url = L"https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/"
                                     L"?appid=" + appId +
                                     L"&count=2&maxlength=600&feeds=steam_community_announcements"
                                     L"&format=json";
            if (!HttpGetUrl(url, body, errorText)) {
                noteError(errorText);
                continue;
            }
            std::vector<NotificationItem> parsed =
                ParseSteamNewsItems(WideFromUtf8(body), {}, appId);
            for (NotificationItem& item : parsed) {
                for (const OwnedGame& game : games) {
                    for (const StorefrontSource& source : game.sources) {
                        if (_wcsicmp(source.storeKey.c_str(), L"steam") == 0 &&
                            source.productId == appId) {
                            item.gameTitle = game.title;
                            break;
                        }
                    }
                    if (!item.gameTitle.empty()) break;
                }
            }
            keepFresh(parsed);
            items.insert(items.end(), std::make_move_iterator(parsed.begin()),
                         std::make_move_iterator(parsed.end()));
        }
    }

    if (settings.news && settings.gogNews && gogInstalled) {
        std::string body;
        std::wstring errorText;
        if (HttpGetUrl(kGogNewsFeedUrl, body, errorText)) {
            std::vector<NotificationItem> parsed =
                ParseRssNewsItems(WideFromUtf8(body), L"GOG", kMaxPerFeed);
            keepFresh(parsed);
            items.insert(items.end(), std::make_move_iterator(parsed.begin()),
                         std::make_move_iterator(parsed.end()));
        } else {
            noteError(errorText);
        }
    }

    if (settings.news && settings.epicNews && epicInstalled) {
        std::string body;
        std::wstring errorText;
        if (HttpGetUrl(kEpicBlogUrl, body, errorText)) {
            std::vector<NotificationItem> parsed = ParseEpicBlogPosts(WideFromUtf8(body), kMaxPerFeed);
            keepFresh(parsed);
            items.insert(items.end(), std::make_move_iterator(parsed.begin()),
                         std::make_move_iterator(parsed.end()));
        } else {
            noteError(errorText);
        }
    }

    if (settings.news && settings.xboxNews && xboxInstalled) {
        std::string body;
        std::wstring errorText;
        if (HttpGetUrl(kXboxNewsFeedUrl, body, errorText)) {
            std::vector<NotificationItem> parsed =
                ParseRssNewsItems(WideFromUtf8(body), L"Xbox", kMaxPerFeed);
            keepFresh(parsed);
            items.insert(items.end(), std::make_move_iterator(parsed.begin()),
                         std::make_move_iterator(parsed.end()));
        } else {
            noteError(errorText);
        }
    }

    if (settings.freeGames || settings.promotions) {
        std::string body;
        std::wstring errorText;
        if (HttpGetUrl(kGamerPowerGiveawaysUrl, body, errorText)) {
            // GamerPower only returns active giveaways, so no recency window is applied: a free
            // game that has been running for months is still worth announcing.
            std::vector<NotificationItem> parsed = ParseGamerPowerGiveaways(WideFromUtf8(body));
            items.insert(items.end(), std::make_move_iterator(parsed.begin()),
                         std::make_move_iterator(parsed.end()));
        } else {
            noteError(errorText);
        }
    }

    if (error != nullptr) *error = transportError;
    return BuildNotificationFeed(std::move(items), settings);
}

} // namespace GameExplorer
