#include "GameExplorerCore/OwnedGames.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Types.h"

#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GameExplorer {

namespace {

constexpr const wchar_t* kSteamHost = L"api.steampowered.com";

std::wstring ToLower(std::wstring text) {
    for (wchar_t& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::wstring Trim(const std::wstring& text) {
    size_t first = 0;
    while (first < text.size() && std::iswspace(text[first]) != 0) ++first;
    size_t last = text.size();
    while (last > first && std::iswspace(text[last - 1]) != 0) --last;
    return text.substr(first, last - first);
}

int CompareText(const std::wstring& left, const std::wstring& right) {
    const int result = _wcsicmp(left.c_str(), right.c_str());
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

const std::wstring& EffectiveSortTitle(const OwnedGame& game) {
    return game.sortTitle.empty() ? game.title : game.sortTitle;
}

int CompareBy(const OwnedGame& left, const OwnedGame& right, OwnedGamesSort sort) {
    switch (sort) {
    case OwnedGamesSort::Title:
        return CompareText(EffectiveSortTitle(left), EffectiveSortTitle(right));
    case OwnedGamesSort::ReleaseDate: {
        const int cmp = (left.releaseYear < right.releaseYear) ? -1
                        : (left.releaseYear > right.releaseYear) ? 1 : 0;
        return cmp;
    }
    case OwnedGamesSort::Playtime: {
        const int cmp = (left.playtimeMinutes < right.playtimeMinutes) ? -1
                        : (left.playtimeMinutes > right.playtimeMinutes) ? 1 : 0;
        return cmp;
    }
    case OwnedGamesSort::LastPlayed: {
        const int cmp = (left.lastPlayedTimestamp < right.lastPlayedTimestamp) ? -1
                        : (left.lastPlayedTimestamp > right.lastPlayedTimestamp) ? 1 : 0;
        return cmp;
    }
    case OwnedGamesSort::Storefront:
        return CompareText(left.storeName, right.storeName);
    case OwnedGamesSort::InstallSize: {
        const int cmp = (left.installSizeBytes < right.installSizeBytes) ? -1
                        : (left.installSizeBytes > right.installSizeBytes) ? 1 : 0;
        return cmp;
    }
    }
    return 0;
}

// ---- Steam JSON helpers ----------------------------------------------------

std::wstring UnescapeJson(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const wchar_t ch = raw[i];
        if (ch != L'\\' || i + 1 >= raw.size()) {
            out.push_back(ch);
            continue;
        }
        const wchar_t next = raw[++i];
        switch (next) {
        case L'n': out.push_back(L'\n'); break;
        case L'r': out.push_back(L'\r'); break;
        case L't': out.push_back(L'\t'); break;
        case L'"': out.push_back(L'"'); break;
        case L'\\': out.push_back(L'\\'); break;
        case L'/': out.push_back(L'/'); break;
        case L'u': {
            if (i + 4 >= raw.size()) break;
            unsigned code = 0;
            bool ok = true;
            for (int digit = 0; digit < 4; ++digit) {
                const wchar_t hex = raw[i + 1 + static_cast<size_t>(digit)];
                code <<= 4;
                if (hex >= L'0' && hex <= L'9') code |= static_cast<unsigned>(hex - L'0');
                else if (hex >= L'a' && hex <= L'f') code |= static_cast<unsigned>(hex - L'a' + 10);
                else if (hex >= L'A' && hex <= L'F') code |= static_cast<unsigned>(hex - L'A' + 10);
                else { ok = false; break; }
            }
            if (ok) {
                out.push_back(static_cast<wchar_t>(code));
                i += 4;
            }
            break;
        }
        default:
            out.push_back(next);
            break;
        }
    }
    return out;
}

size_t SkipSpace(const std::wstring& json, size_t pos, size_t limit) {
    while (pos < limit && std::iswspace(json[pos]) != 0) ++pos;
    return pos;
}

std::wstring JsonString(const std::wstring& json, const std::wstring& key, size_t from, size_t to) {
    const std::wstring needle = L"\"" + key + L"\"";
    const size_t pos = json.find(needle, from);
    if (pos == std::wstring::npos || pos >= to) return {};
    size_t cursor = json.find(L':', pos + needle.size());
    if (cursor == std::wstring::npos || cursor >= to) return {};
    cursor = SkipSpace(json, cursor + 1, to);
    if (cursor >= to || json[cursor] != L'"') return {};
    ++cursor;
    std::wstring raw;
    while (cursor < to) {
        const wchar_t ch = json[cursor];
        if (ch == L'"') break;
        if (ch == L'\\' && cursor + 1 < to) raw.push_back(json[cursor++]);
        raw.push_back(json[cursor++]);
    }
    return UnescapeJson(raw);
}

bool JsonBool(const std::wstring& json, const std::wstring& key, size_t from, size_t to) {
    const std::wstring needle = L"\"" + key + L"\"";
    const size_t pos = json.find(needle, from);
    if (pos == std::wstring::npos || pos >= to) return false;
    size_t cursor = json.find(L':', pos + needle.size());
    if (cursor == std::wstring::npos || cursor >= to) return false;
    cursor = SkipSpace(json, cursor + 1, to);
    return cursor + 4 <= to && json.compare(cursor, 4, L"true") == 0;
}

long long JsonInt(const std::wstring& json, const std::wstring& key, size_t from, size_t to) {
    const std::wstring needle = L"\"" + key + L"\"";
    const size_t pos = json.find(needle, from);
    if (pos == std::wstring::npos || pos >= to) return 0;
    size_t cursor = json.find(L':', pos + needle.size());
    if (cursor == std::wstring::npos || cursor >= to) return 0;
    cursor = SkipSpace(json, cursor + 1, to);
    bool negative = false;
    if (cursor < to && json[cursor] == L'-') { negative = true; ++cursor; }
    long long value = 0;
    bool any = false;
    while (cursor < to && json[cursor] >= L'0' && json[cursor] <= L'9') {
        value = value * 10 + (json[cursor] - L'0');
        any = true;
        ++cursor;
    }
    if (!any) return 0;
    return negative ? -value : value;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                               nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring wide(static_cast<size_t>(sizeNeeded), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), sizeNeeded);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                               nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string utf8(static_cast<size_t>(sizeNeeded), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), sizeNeeded,
                        nullptr, nullptr);
    return utf8;
}

size_t FindMatchingBrace(const std::wstring& text, size_t openPos, wchar_t openChar, wchar_t closeChar) {
    if (openPos >= text.size() || text[openPos] != openChar) return std::wstring::npos;
    int depth = 0;
    bool inQuote = false;
    for (size_t i = openPos; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\\' && inQuote && i + 1 < text.size()) {
            ++i;
            continue;
        }
        if (ch == L'"') {
            inQuote = !inQuote;
        } else if (!inQuote) {
            if (ch == openChar) {
                ++depth;
            } else if (ch == closeChar) {
                --depth;
                if (depth == 0) return i;
            }
        }
    }
    return std::wstring::npos;
}

// Minimal WinHTTP GET used only for the Steam Web API.
bool HttpGet(const std::wstring& host, const std::wstring& path, std::string& body) {    body.clear();

    HINTERNET session = WinHttpOpen(L"GameExplorerCatalogue/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) return false;
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect != nullptr) {
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
        if (request != nullptr) {
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
                WinHttpReceiveResponse(request, nullptr) != FALSE) {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                WinHttpQueryHeaders(request,
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                    WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
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

// Percent-encodes a UTF-8 query parameter value for a WinHTTP request path.
std::wstring PercentEncodeQuery(const std::string& text) {
    static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(text.size() * 3);
    for (const char rawChar : text) {
        const unsigned char ch = static_cast<unsigned char>(rawChar);
        const bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                                ch == '.' || ch == '~';
        if (unreserved) {
            out.push_back(static_cast<wchar_t>(ch));
        } else {
            out.push_back(L'%');
            out.push_back(kHex[ch >> 4]);
            out.push_back(kHex[ch & 0x0F]);
        }
    }
    return out;
}

std::wstring SteamInstallPath() {
    wchar_t buffer[MAX_PATH * 2]{};
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath",
                                        RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (status != ERROR_SUCCESS) return {};
    return buffer;
}

// Every quoted token in `text`, in order - which is all this needs from a VDF file.
struct VdfToken {
    std::string text;
};

std::vector<VdfToken> TokenizeQuoted(const std::string& text) {
    std::vector<VdfToken> tokens;
    size_t cursor = 0;
    while (true) {
        const size_t open = text.find('"', cursor);
        if (open == std::string::npos) break;
        const size_t close = text.find('"', open + 1);
        if (close == std::string::npos) break;
        tokens.push_back({text.substr(open + 1, close - open - 1)});
        cursor = close + 1;
    }
    return tokens;
}

bool LooksLikeSteamId64(const std::string& token) {
    if (token.size() != 17) return false;
    for (const char ch : token) {
        if (ch < '0' || ch > '9') return false;
    }
    return token.rfind("7656", 0) == 0;
}

std::wstring SteamIdFromLoginUsers(const std::string& text) {
    const std::vector<VdfToken> tokens = TokenizeQuoted(text);
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].text != "MostRecent" || tokens[i + 1].text != "1") continue;
        // The account block key (`"7656119..." {`) appears before its fields.
        for (size_t back = i; back > 0; --back) {
            if (LooksLikeSteamId64(tokens[back].text)) {
                const std::string& id = tokens[back].text;
                return std::wstring(id.begin(), id.end());
            }
        }
    }
    return {};
}

// --- Ubisoft Connect cache helpers -------------------------------------------

bool ReadFileBytes(const std::filesystem::path& path, std::string& out) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// Ubisoft's cache uses plain protobuf varints (not the custom encoding some older launcher code
// used), so a standard little-endian base-128 reader is enough.
uint64_t ReadVarint(const std::string& data, size_t& pos, bool& ok) {
    uint64_t value = 0;
    int shift = 0;
    while (pos < data.size() && shift < 64) {
        const unsigned char byte = static_cast<unsigned char>(data[pos++]);
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return value;
        shift += 7;
    }
    ok = false;
    return 0;
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(L'\n', start);
        const size_t stop = end == std::wstring::npos ? text.size() : end;
        std::wstring line = text.substr(start, stop - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return lines;
}

size_t IndentOf(const std::wstring& line) {
    size_t indent = 0;
    while (indent < line.size() && line[indent] == L' ') ++indent;
    return indent;
}

// Slice of a YAML-ish block that belongs to `key` at `indent`, up to the next key at that indent
// or any shallower one. Ubisoft's game configs nest the interesting fields this way.
std::wstring YamlSection(const std::wstring& text, const std::wstring& key, size_t indent) {
    const std::vector<std::wstring> lines = SplitLines(text);
    size_t firstBodyLine = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring& line = lines[i];
        if (line.size() <= indent || IndentOf(line) != indent) continue;
        if (line.compare(indent, key.size(), key) != 0) continue;
        const size_t after = indent + key.size();
        if (after < line.size() && line[after] != L':') continue;
        firstBodyLine = i + 1;
        break;
    }
    if (firstBodyLine >= lines.size()) return {};

    std::wstring body;
    for (size_t i = firstBodyLine; i < lines.size(); ++i) {
        const std::wstring& line = lines[i];
        if (line.empty()) continue;
        if (IndentOf(line) <= indent) break;
        body += line;
        body += L'\n';
    }
    return body;
}

// Every `key: value` pair exactly `indent` spaces deep. Deeper (nested) keys are ignored.
std::unordered_map<std::wstring, std::wstring> YamlFields(const std::wstring& text, size_t indent) {
    std::unordered_map<std::wstring, std::wstring> fields;
    for (const std::wstring& line : SplitLines(text)) {
        if (line.empty() || IndentOf(line) != indent) continue;
        const size_t colon = line.find(L':', indent);
        if (colon == std::wstring::npos) continue;
        std::wstring key = Trim(line.substr(indent, colon - indent));
        if (key.empty()) continue;
        std::wstring value = Trim(line.substr(colon + 1));
        if (value.size() >= 2 && ((value.front() == L'"' && value.back() == L'"') ||
                                  (value.front() == L'\'' && value.back() == L'\''))) {
            value = value.substr(1, value.size() - 2);
        }
        fields.emplace(std::move(key), std::move(value));
    }
    return fields;
}

// Removes trademark marks and normalizes typographic apostrophes/quotes so one game has one
// display title regardless of which storefront wrote it.
std::wstring SanitizeStoreTitle(const std::wstring& text) {
    std::wstring cleaned;
    cleaned.reserve(text.size());
    for (const wchar_t ch : text) {
        switch (ch) {
        case L'\u00AE':                                             // ®
        case L'\u2122':                                             // ™
            break;
        case L'\u2018':
        case L'\u2019':
            cleaned.push_back(L'\'');
            break;
        case L'\u201C':
        case L'\u201D':
            cleaned.push_back(L'"');
            break;
        default:
            cleaned.push_back(ch);
            break;
        }
    }

    std::wstring collapsed;
    collapsed.reserve(cleaned.size());
    bool previousSpace = false;
    for (const wchar_t ch : cleaned) {
        const bool isSpace = std::iswspace(ch) != 0;
        if (isSpace && previousSpace) continue;
        collapsed.push_back(isSpace ? L' ' : ch);
        previousSpace = isSpace;
    }
    return Trim(collapsed);
}

// `l1`/`l2`/`GAMENAME` are localization placeholders, never real titles.
bool IsUbisoftPlaceholder(const std::wstring& value) {
    if (value.empty()) return true;
    const std::wstring lower = ToLower(value);
    if (lower == L"gamename") return true;
    if (lower.size() < 2 || lower[0] != L'l') return false;
    for (size_t i = 1; i < lower.size(); ++i) {
        if (lower[i] < L'0' || lower[i] > L'9') return false;
    }
    return true;
}

bool LooksLikeTitle(const std::wstring& value) {
    if (value.size() < 2 || value.size() > 120) return false;
    size_t alphanumeric = 0;
    for (const wchar_t ch : value) {
        if (std::iswalnum(ch) != 0) ++alphanumeric;
    }
    return alphanumeric >= 2;
}

// A bare 32-character hex app name is an opaque id, not a title.
bool LooksLikeOpaqueId(const std::wstring& value) {
    if (value.size() != 32) return false;
    for (const wchar_t ch : value) {
        if (std::iswxdigit(ch) == 0) return false;
    }
    return true;
}

// Sort keys carry an ordinal so the launcher can order series ("Assassin's Creed 03").
std::wstring StripTrailingOrdinal(std::wstring value) {
    size_t end = value.size();
    size_t digits = 0;
    while (end > 0 && value[end - 1] >= L'0' && value[end - 1] <= L'9' && digits < 3) {
        --end;
        ++digits;
    }
    if (digits == 0 || digits > 2) return value;
    if (end == 0 || std::iswspace(value[end - 1]) == 0) return value;
    return Trim(value.substr(0, end));
}

bool IsAsciiHexDigit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

void AddUniqueId(std::vector<std::wstring>& ids, std::wstring id) {
    if (id.empty()) return;
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) return;
    ids.push_back(std::move(id));
}

// Every UUID-looking run in a byte blob, lowercased. Ownership caches are binary, but the ids
// themselves are stored as plain ASCII.
std::vector<std::wstring> ScanUuids(const std::string& blob) {
    std::vector<std::wstring> ids;
    for (size_t pos = 0; pos + 36 <= blob.size(); ++pos) {
        bool matches = true;
        for (size_t offset = 0; offset < 36 && matches; ++offset) {
            const char ch = blob[pos + offset];
            if (offset == 8 || offset == 13 || offset == 18 || offset == 23) {
                matches = ch == '-';
            } else {
                matches = IsAsciiHexDigit(ch);
            }
        }
        if (!matches) continue;

        std::wstring id;
        id.reserve(36);
        for (size_t offset = 0; offset < 36; ++offset) {
            const unsigned char ch = static_cast<unsigned char>(blob[pos + offset]);
            id.push_back(static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch))));
        }
        AddUniqueId(ids, std::move(id));
        pos += 35;
    }
    return ids;
}

// app_id/space_id values anywhere in a game config, including nested crash-reporting blocks that
// repeat the ids the ownership cache actually lists.
std::vector<std::wstring> ConfigOwnershipIds(const std::wstring& text) {
    std::vector<std::wstring> ids;
    for (const std::wstring& line : SplitLines(text)) {
        const std::wstring trimmed = Trim(line);
        size_t keyLength = 0;
        if (trimmed.rfind(L"app_id:", 0) == 0) {
            keyLength = 7;
        } else if (trimmed.rfind(L"space_id:", 0) == 0) {
            keyLength = 9;
        } else {
            continue;
        }

        std::wstring value = Trim(trimmed.substr(keyLength));
        if (value.size() >= 2 && ((value.front() == L'"' && value.back() == L'"') ||
                                  (value.front() == L'\'' && value.back() == L'\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (value.size() != 36) continue;

        bool valid = true;
        for (size_t i = 0; i < value.size() && valid; ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                valid = value[i] == L'-';
            } else {
                valid = std::iswxdigit(value[i]) != 0;
            }
        }
        if (!valid) continue;

        for (wchar_t& ch : value) ch = static_cast<wchar_t>(std::towlower(ch));
        AddUniqueId(ids, std::move(value));
    }
    return ids;
}

// Resolves a Ubisoft game title. The root `name` is frequently a localization key (`l1`), so the
// value is looked up in `localizations.default` first; `sort_string` and `installer.game_identifier`
// are the fallbacks when a config only carries launcher metadata.
std::wstring ResolveUbisoftTitle(const std::wstring& text) {
    const std::wstring root = YamlSection(text, L"root", 0);
    const std::wstring localizations = YamlSection(text, L"localizations", 0);
    const std::unordered_map<std::wstring, std::wstring> defaultLocale =
        YamlFields(YamlSection(localizations, L"default", 2), 4);
    const std::unordered_map<std::wstring, std::wstring> rootFields = YamlFields(root, 2);

    auto resolve = [&defaultLocale](const std::wstring& raw) -> std::wstring {
        if (raw.empty()) return {};
        // A localization key is only a key when the default locale actually defines it; a real
        // title like "UNO" is used verbatim.
        const auto it = defaultLocale.find(raw);
        if (it != defaultLocale.end()) return it->second;
        return IsUbisoftPlaceholder(raw) ? std::wstring{} : raw;
    };

    for (const wchar_t* key : { L"display_name", L"name" }) {
        const auto it = rootFields.find(key);
        if (it == rootFields.end()) continue;
        const std::wstring value = SanitizeStoreTitle(resolve(it->second));
        if (LooksLikeTitle(value)) return value;
    }

    const auto sortIt = rootFields.find(L"sort_string");
    if (sortIt != rootFields.end()) {
        const std::wstring value = SanitizeStoreTitle(StripTrailingOrdinal(sortIt->second));
        if (LooksLikeTitle(value)) return value;
    }

    const std::unordered_map<std::wstring, std::wstring> installer =
        YamlFields(YamlSection(root, L"installer", 2), 4);
    const auto identifierIt = installer.find(L"game_identifier");
    if (identifierIt != installer.end()) {
        const std::wstring value = SanitizeStoreTitle(identifierIt->second);
        if (LooksLikeTitle(value)) return value;
    }
    return {};
}

// Official Ubisoft CDN file name for a game's box art (`root.thumb_image`), empty when absent.
// Only a bare file name is accepted so a modified cache can never turn into an arbitrary URL.
std::wstring UbisoftThumbnailHash(const std::wstring& text) {
    const std::unordered_map<std::wstring, std::wstring> rootFields =
        YamlFields(YamlSection(text, L"root", 0), 2);
    const auto it = rootFields.find(L"thumb_image");
    if (it == rootFields.end()) return {};
    const std::wstring value = Trim(it->second);
    if (value.empty() || value.size() > 80) return {};
    if (value.find(L'/') != std::wstring::npos || value.find(L'\\') != std::wstring::npos ||
        value.find(L':') != std::wstring::npos) {
        return {};
    }
    return value;
}

// Epic metadata ships several key images; the wide storefront one suits the catalogue's detail
// header, with any other image as the last resort.
std::wstring LegendaryKeyImage(const std::wstring& json) {    const size_t keyPos = json.find(L"\"keyImages\"");
    if (keyPos == std::wstring::npos) return {};
    const size_t arrayStart = json.find(L'[', keyPos);
    if (arrayStart == std::wstring::npos) return {};
    const size_t arrayEnd = FindMatchingBrace(json, arrayStart, L'[', L']');
    if (arrayEnd == std::wstring::npos) return {};

    int bestScore = -1;
    std::wstring best;
    size_t cursor = arrayStart + 1;
    while (cursor < arrayEnd) {
        const size_t objStart = json.find(L'{', cursor);
        if (objStart == std::wstring::npos || objStart >= arrayEnd) break;
        const size_t objEnd = FindMatchingBrace(json, objStart, L'{', L'}');
        if (objEnd == std::wstring::npos || objEnd > arrayEnd) break;

        const std::wstring type = JsonString(json, L"type", objStart, objEnd);
        const std::wstring url = JsonString(json, L"url", objStart, objEnd);
        int score = 0;
        if (type == L"DieselStoreFrontWide") {
            score = 3;
        } else if (type == L"OfferImageWide") {
            score = 2;
        } else if (type == L"Thumbnail") {
            score = 1;
        }
        if (!url.empty() && score > bestScore) {
            bestScore = score;
            best = url;
        }
        cursor = objEnd + 1;
    }
    return best;
}

// Epic descriptions are plain text with CRLF line endings; the model stores paragraph-normalized
// text ready for the GitHub Flavored Markdown renderer.
std::wstring NormalizeParagraphs(const std::wstring& text) {
    std::wstring normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') continue;
            normalized.push_back(L'\n');
            continue;
        }
        normalized.push_back(text[i]);
    }
    return Trim(normalized);
}



} // namespace

bool OwnedGamesTextContains(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

bool OwnedGamesHasToken(const std::wstring& list, const std::wstring& token) {
    if (token.empty()) return true;
    const std::wstring wanted = ToLower(Trim(token));
    if (wanted.empty()) return true;

    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(L',', start);
        const std::wstring part = Trim(list.substr(start, comma == std::wstring::npos
                                                              ? std::wstring::npos
                                                              : comma - start));
        if (ToLower(part) == wanted) return true;
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return false;
}

std::wstring OwnedGamesSortTitle(const std::wstring& title) {
    const std::wstring trimmed = Trim(title);
    const size_t space = trimmed.find(L' ');
    if (space != std::wstring::npos) {
        const std::wstring head = ToLower(trimmed.substr(0, space));
        if (head == L"the" || head == L"a" || head == L"an") {
            const std::wstring rest = Trim(trimmed.substr(space + 1));
            if (!rest.empty()) return rest;
        }
    }
    return trimmed;
}

bool OwnedGameMatchesStore(const OwnedGame& game, const std::wstring& targetStoreKey) {
    if (CompareText(game.storeKey, targetStoreKey) == 0) return true;
    for (const auto& src : game.sources) {
        if (CompareText(src.storeKey, targetStoreKey) == 0) return true;
    }
    return false;
}

bool OwnedGameMatches(const OwnedGame& game, const OwnedGamesFilter& filter) {
    if (!filter.search.empty() && !OwnedGamesTextContains(game.title, filter.search)) return false;
    if (!filter.storeKey.empty() && !OwnedGameMatchesStore(game, filter.storeKey)) return false;
    if (filter.installed >= 0 && game.installed != (filter.installed == 1)) return false;
    if (filter.familyShared >= 0 && game.familyShared != (filter.familyShared == 1)) return false;
    if (!filter.genre.empty() && !OwnedGamesHasToken(game.genres, filter.genre)) return false;
    if (!filter.tag.empty() && !OwnedGamesHasToken(game.tags, filter.tag)) return false;
    if (filter.releaseYear > 0 && game.releaseYear != filter.releaseYear) return false;
    if (!filter.rating.empty() && CompareText(game.rating, filter.rating) != 0) return false;
    if (game.playtimeMinutes < filter.minPlaytimeMinutes) return false;
    return true;
}

std::vector<OwnedGame> FilterOwnedGames(const std::vector<OwnedGame>& games,
                                        const OwnedGamesFilter& filter) {
    std::vector<OwnedGame> filtered;
    filtered.reserve(games.size());
    for (const OwnedGame& game : games) {
        if (OwnedGameMatches(game, filter)) filtered.push_back(game);
    }
    return filtered;
}

void SortOwnedGames(std::vector<OwnedGame>& games, OwnedGamesSort sort,
                    OwnedGamesSortDirection direction) {
    std::stable_sort(games.begin(), games.end(), [sort, direction](const OwnedGame& left,
                                                                   const OwnedGame& right) {
        int cmp = CompareBy(left, right, sort);
        if (cmp != 0) return direction == OwnedGamesSortDirection::Ascending ? cmp < 0 : cmp > 0;

        cmp = CompareText(EffectiveSortTitle(left), EffectiveSortTitle(right));
        if (cmp != 0) return cmp < 0;
        cmp = CompareText(left.storeName, right.storeName);
        if (cmp != 0) return cmp < 0;
        return CompareText(left.appId, right.appId) < 0;
    });
}

std::vector<OwnedGame> SelectOwnedGames(const std::vector<OwnedGame>& games,
                                        const OwnedGamesFilter& filter, OwnedGamesSort sort,
                                        OwnedGamesSortDirection direction) {
    std::vector<OwnedGame> result = FilterOwnedGames(games, filter);
    SortOwnedGames(result, sort, direction);
    return result;
}

int ClampOwnedGamesPageIndex(int pageIndex, int totalMatches, int pageSize) {
    const int size = (std::max)(1, pageSize);
    if (totalMatches <= 0) return 0;
    const int pageCount = (totalMatches + size - 1) / size;
    if (pageIndex < 0) return 0;
    if (pageIndex >= pageCount) return pageCount - 1;
    return pageIndex;
}

OwnedGamesPage QueryOwnedGames(const std::vector<OwnedGame>& games, const OwnedGamesQuery& query) {
    OwnedGamesPage page;
    page.pageSize = (std::max)(1, query.pageSize);

    std::vector<OwnedGame> matches = SelectOwnedGames(games, query.filter, query.sort,
                                                      query.direction);
    page.totalMatches = static_cast<int>(matches.size());
    if (page.totalMatches == 0) return page;

    page.pageCount = (page.totalMatches + page.pageSize - 1) / page.pageSize;
    page.pageIndex = ClampOwnedGamesPageIndex(query.pageIndex, page.totalMatches, page.pageSize);

    const size_t first = static_cast<size_t>(page.pageIndex) * static_cast<size_t>(page.pageSize);
    const size_t last = (std::min)(matches.size(), first + static_cast<size_t>(page.pageSize));
    page.items.assign(matches.begin() + static_cast<std::ptrdiff_t>(first),
                      matches.begin() + static_cast<std::ptrdiff_t>(last));
    return page;
}

void MarkInstalledOwnedGames(std::vector<OwnedGame>& games,
                             const std::vector<std::wstring>& installedAppIds) {    for (OwnedGame& game : games) {
        game.installed = std::find(installedAppIds.begin(), installedAppIds.end(), game.appId) !=
                         installedAppIds.end();
    }
}

bool ParseSteamOwnedGamesJson(const std::wstring& json, std::vector<OwnedGame>& out) {
    out.clear();
    const size_t responsePos = json.find(L"\"response\"");
    if (responsePos == std::wstring::npos) return false;
    const size_t responseValue = json.find_first_not_of(L" \t\r\n:", responsePos + 10);
    if (responseValue == std::wstring::npos || json[responseValue] != L'{') return false;

    const size_t gamesPos = json.find(L"\"games\"");
    if (gamesPos == std::wstring::npos) {
        return true;
    }

    const size_t arrayStart = json.find(L'[', gamesPos);
    if (arrayStart == std::wstring::npos) return false;
    const size_t arrayEnd = FindMatchingBrace(json, arrayStart, L'[', L']');
    if (arrayEnd == std::wstring::npos) return false;

    size_t cursor = arrayStart + 1;
    while (cursor < arrayEnd) {
        const size_t objStart = json.find(L'{', cursor);
        if (objStart == std::wstring::npos || objStart >= arrayEnd) break;
        const size_t objEnd = FindMatchingBrace(json, objStart, L'{', L'}');
        if (objEnd == std::wstring::npos || objEnd > arrayEnd) break;

        const long long appid = JsonInt(json, L"appid", objStart, objEnd);
        if (appid > 0) {
            const std::wstring id = std::to_wstring(appid);
            OwnedGame game;
            game.appId = L"steam_" + id;
            game.storeKey = L"steam";
            game.storeName = L"Steam";
            game.productId = id;
            game.title = JsonString(json, L"name", objStart, objEnd);
            if (game.title.empty()) game.title = L"Steam App " + id;
            game.title = ExtractBaseTitle(game.title);
            game.sortTitle = OwnedGamesSortTitle(game.title);
            game.coverArtUrl = L"https://cdn.cloudflare.steamstatic.com/steam/apps/" + id +
                               L"/header.jpg";
            game.storePageUrl = L"https://store.steampowered.com/app/" + id + L"/";
            game.installUri = L"steam://install/" + id;

            const long long playtime = JsonInt(json, L"playtime_forever", objStart, objEnd);
            game.playtimeMinutes = playtime > 0 ? static_cast<uint32_t>(playtime) : 0;

            const long long lastPlayed = JsonInt(json, L"rtime_last_played", objStart, objEnd);
            game.lastPlayedTimestamp = lastPlayed > 0 ? lastPlayed : 0;

            out.push_back(std::move(game));
        }

        cursor = objEnd + 1;
    }
    return true;
}

std::vector<OwnedGame> ScanGogOwnedGamesGalaxy() {
    std::vector<OwnedGame> games;
    wchar_t progData[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, progData))) {
        return games;
    }

    std::filesystem::path dbPath = std::filesystem::path(progData) / L"GOG.com" / L"Galaxy" / L"storage" / L"galaxy-2.0.db";
    if (!std::filesystem::exists(dbPath)) return games;

    sqlite3* db = nullptr;
    const std::string utf8Path = WideToUtf8(dbPath.wstring());
    if (sqlite3_open_v2(utf8Path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK || db == nullptr) {
        if (db != nullptr) sqlite3_close(db);
        return games;
    }

    // osCompatibility (game piece type 220) is GOG's per-release platform truth:
    // {"supported":[{"name":"Windows","slug":"windows","versions":null}]}. A release whose list is
    // published but lacks Windows is a macOS/Linux-only title.
    std::unordered_map<std::wstring, std::wstring> osPieces;
    {
        const char* osSql = "SELECT releaseKey, value FROM GamePieces WHERE gamePieceTypeId = 220;";
        sqlite3_stmt* osStmt = nullptr;
        if (sqlite3_prepare_v2(db, osSql, -1, &osStmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(osStmt) == SQLITE_ROW) {
                const unsigned char* keyText = sqlite3_column_text(osStmt, 0);
                const unsigned char* valText = sqlite3_column_text(osStmt, 1);
                if (keyText == nullptr || valText == nullptr) continue;
                osPieces.emplace(Utf8ToWide(reinterpret_cast<const char*>(keyText)),
                                 Utf8ToWide(reinterpret_cast<const char*>(valText)));
            }
            sqlite3_finalize(osStmt);
        }
    }

    const char* sql = "SELECT gp.releaseKey, gp.value FROM GamePieces gp "
                      "WHERE gp.gamePieceTypeId = 304 AND gp.releaseKey LIKE 'gog_%';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* keyText = sqlite3_column_text(stmt, 0);
            const unsigned char* valText = sqlite3_column_text(stmt, 1);
            if (keyText == nullptr || valText == nullptr) continue;

            const std::wstring releaseKey = Utf8ToWide(reinterpret_cast<const char*>(keyText));
            const std::wstring valJson = Utf8ToWide(reinterpret_cast<const char*>(valText));

            // A release without a published osCompatibility piece keeps the title.
            bool windowsInstallable = true;
            const auto osIt = osPieces.find(releaseKey);
            if (osIt != osPieces.end()) {
                bool hasPlatformEvidence = false;
                windowsInstallable = GogOsPieceSupportsWindows(osIt->second, hasPlatformEvidence) ||
                                     !hasPlatformEvidence;
            }

            std::wstring title = JsonString(valJson, L"title", 0, valJson.size());
            if (title.empty()) continue;

            std::wstring prodId;
            if (releaseKey.rfind(L"gog_", 0) == 0) {
                prodId = releaseKey.substr(4);
            } else {
                prodId = releaseKey;
            }

            OwnedGame game;
            game.appId = L"gog_" + prodId;
            game.storeKey = L"gog";
            game.storeName = L"GOG";
            game.productId = prodId;
            game.title = std::move(title);
            game.title = ExtractBaseTitle(game.title);
            game.sortTitle = OwnedGamesSortTitle(game.title);
            game.storePageUrl = L"https://www.gog.com/game/" + prodId;
            game.installUri = L"goggalaxy://openGameView/" + prodId;
            game.sources.push_back({ game.storeKey, game.storeName, game.productId,
                                     game.installUri, game.storePageUrl, {}, false,
                                     windowsInstallable });
            games.push_back(std::move(game));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return games;
}

std::vector<OwnedGame> ScanItchOwnedGamesButler() {
    std::vector<OwnedGame> games;
    wchar_t appData[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return games;
    }

    std::filesystem::path dbPath = std::filesystem::path(appData) / L"itch" / L"db" / L"butler.db";
    if (!std::filesystem::exists(dbPath)) return games;

    sqlite3* db = nullptr;
    const std::string utf8Path = WideToUtf8(dbPath.wstring());
    if (sqlite3_open_v2(utf8Path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK || db == nullptr) {
        if (db != nullptr) sqlite3_close(db);
        return games;
    }

    const char* sql = "SELECT id, title, url, cover_url, scanned_platforms FROM games;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const long long id = sqlite3_column_int64(stmt, 0);
            const unsigned char* titleText = sqlite3_column_text(stmt, 1);
            const unsigned char* urlText = sqlite3_column_text(stmt, 2);
            const unsigned char* coverText = sqlite3_column_text(stmt, 3);
            const unsigned char* platformsText = sqlite3_column_text(stmt, 4);
            if (id <= 0 || titleText == nullptr) continue;

            // An empty/NULL `scanned_platforms` means the upload was never scanned, which is not
            // evidence of another platform, so those games stay.
            bool windowsInstallable = true;
            if (platformsText != nullptr) {
                const std::wstring platformsJson =
                    Utf8ToWide(reinterpret_cast<const char*>(platformsText));
                const std::vector<std::wstring> platforms = JsonStringArray(platformsJson, L"");
                bool hasPlatformEvidence = false;
                windowsInstallable =
                    OwnedGamesPlatformsIncludeWindows(platforms, hasPlatformEvidence) ||
                    !hasPlatformEvidence;
            }

            const std::wstring prodId = std::to_wstring(id);
            OwnedGame game;
            game.appId = L"itch_" + prodId;
            game.storeKey = L"itch";
            game.storeName = L"itch.io";
            game.productId = prodId;
            game.title = Utf8ToWide(reinterpret_cast<const char*>(titleText));
            game.title = ExtractBaseTitle(game.title);
            game.sortTitle = OwnedGamesSortTitle(game.title);
            if (urlText != nullptr) game.storePageUrl = Utf8ToWide(reinterpret_cast<const char*>(urlText));
            if (coverText != nullptr) game.coverArtUrl = Utf8ToWide(reinterpret_cast<const char*>(coverText));
            game.installUri = L"itch://games/" + prodId;
            game.sources.push_back({ game.storeKey, game.storeName, game.productId,
                                     game.installUri, game.storePageUrl, {}, false,
                                     windowsInstallable });
            games.push_back(std::move(game));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return games;
}

std::vector<OwnedGame> ScanXboxOwnedGamesAsyncCache() {
    std::vector<OwnedGame> games;
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) != S_OK) {
        GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    }
    if (localAppData[0] == L'\0') return games;

    const std::filesystem::path packagesDir = std::filesystem::path(localAppData) / L"Packages";
    if (!std::filesystem::exists(packagesDir)) return games;

    std::filesystem::path dbPath;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(packagesDir, ec)) {
        if (entry.is_directory(ec)) {
            const std::wstring name = entry.path().filename().wstring();
            if (name.rfind(L"Microsoft.GamingApp", 0) == 0) {
                const std::filesystem::path candidate = entry.path() / L"LocalState" / L"AsyncCache.db";
                if (std::filesystem::exists(candidate)) {
                    dbPath = candidate;
                    break;
                }
            }
        }
    }
    if (dbPath.empty()) return games;

    sqlite3* db = nullptr;
    const std::string utf8Path = WideToUtf8(dbPath.wstring());
    if (sqlite3_open_v2(utf8Path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK || db == nullptr) {
        if (db != nullptr) sqlite3_close(db);
        return games;
    }

    const char* sql = "SELECT key, value FROM AsyncCache WHERE scope='product_summary';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* keyText = sqlite3_column_text(stmt, 0);
            const unsigned char* valText = sqlite3_column_text(stmt, 1);
            if (valText == nullptr) continue;

            const std::string valUtf8(reinterpret_cast<const char*>(valText));
            if (valUtf8.find("\"productKind\":\"GAME\"") == std::string::npos &&
                valUtf8.find("\"productKind\": \"GAME\"") == std::string::npos) {
                continue;
            }

            const std::wstring json = Utf8ToWide(valUtf8);
            const std::wstring title = JsonString(json, L"title", 0, json.size());
            if (title.empty()) continue;

            std::wstring storeId = JsonString(json, L"StoreId", 0, json.size());
            if (storeId.empty() && keyText != nullptr) {
                storeId = Utf8ToWide(reinterpret_cast<const char*>(keyText));
            }
            if (storeId.empty()) continue;

            // Xbox shares one store front between console and PC, so `availablePlatforms` is the
            // only ownership-side evidence of a Windows build. Console-only editions (the common
            // Game Pass case) and mobile/handheld-only entries never belong in a Windows catalogue.
            const std::vector<std::wstring> platforms =
                JsonStringArray(json, L"availablePlatforms");
            bool hasPlatformEvidence = false;
            const bool windowsInstallable =
                OwnedGamesPlatformsIncludeWindows(platforms, hasPlatformEvidence) ||
                !hasPlatformEvidence;

            OwnedGame game;
            game.appId = L"xbox_" + storeId;
            game.storeKey = L"xbox";
            game.storeName = L"Xbox";
            game.productId = storeId;

            // Strip edition suffix before storing the canonical title.
            std::wstring editionLabel;
            game.title = ExtractBaseTitle(title, &editionLabel);
            game.sortTitle = OwnedGamesSortTitle(game.title);

            game.installUri = L"ms-windows-store://pdp/?ProductId=" + storeId;
            game.storePageUrl = L"https://www.xbox.com/games/store/p/" + storeId;

            // Extract artwork
            const size_t artPos = json.find(L"\"artwork\"");
            if (artPos != std::wstring::npos) {
                size_t uriPos = json.find(L"\"uri\":\"", artPos);
                if (uriPos != std::wstring::npos) {
                    uriPos += 7;
                    const size_t endUri = json.find(L"\"", uriPos);
                    if (endUri != std::wstring::npos) {
                        game.coverArtUrl = json.substr(uriPos, endUri - uriPos);
                    }
                }
            }

            // Extract release year
            const std::wstring relDate = JsonString(json, L"releaseDate", 0, json.size());
            if (relDate.size() >= 4) {
                const int y = _wtoi(relDate.substr(0, 4).c_str());
                if (y > 1980 && y < 2100) game.releaseYear = y;
            }

            // Extract categories
            const size_t catPos = json.find(L"\"categories\":[");
            if (catPos != std::wstring::npos) {
                const size_t endCat = json.find(L']', catPos);
                if (endCat != std::wstring::npos) {
                    size_t cur = catPos;
                    std::wstring genresStr;
                    while (true) {
                        const size_t q1 = json.find(L'"', cur);
                        if (q1 == std::wstring::npos || q1 >= endCat) break;
                        const size_t q2 = json.find(L'"', q1 + 1);
                        if (q2 == std::wstring::npos || q2 > endCat) break;
                        const std::wstring cat = json.substr(q1 + 1, q2 - q1 - 1);
                        if (cat != L"categories") {
                            if (!genresStr.empty()) genresStr += L", ";
                            genresStr += cat;
                        }
                        cur = q2 + 1;
                    }
                    game.genres = genresStr;
                }
            }

            std::wstring desc = JsonString(json, L"description", 0, json.size());
            if (desc.empty()) desc = JsonString(json, L"shortDescription", 0, json.size());
            game.synopsis = std::move(desc);

            game.sources.push_back({ game.storeKey, game.storeName, game.productId,
                                     game.installUri, game.storePageUrl, editionLabel, false,
                                     windowsInstallable });
            games.push_back(std::move(game));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return games;
}

std::vector<UbisoftConfigEntry> ParseUbisoftConfigurationCache(const std::string& blob) {
    std::vector<UbisoftConfigEntry> entries;
    size_t pos = 0;
    while (pos < blob.size()) {
        // Each record is a length-delimited protobuf message introduced by field tag 0x0A.
        if (static_cast<unsigned char>(blob[pos]) != 0x0A) {
            ++pos;
            continue;
        }

        size_t cursor = pos + 1;
        bool ok = true;
        const uint64_t recordSize = ReadVarint(blob, cursor, ok);
        if (!ok || recordSize == 0 || cursor + recordSize > blob.size()) {
            ++pos;
            continue;
        }
        const size_t recordEnd = cursor + static_cast<size_t>(recordSize);

        uint64_t installId = 0;
        uint64_t launchId = 0;
        std::string payload;
        while (cursor < recordEnd && ok) {
            const unsigned char tag = static_cast<unsigned char>(blob[cursor++]);
            if (tag == 0x08) {
                installId = ReadVarint(blob, cursor, ok);
            } else if (tag == 0x10) {
                launchId = ReadVarint(blob, cursor, ok);
            } else if (tag == 0x1A) {
                const uint64_t length = ReadVarint(blob, cursor, ok);
                if (!ok || length == 0 || cursor + length > recordEnd) {
                    ok = false;
                    break;
                }
                payload.assign(blob, cursor, static_cast<size_t>(length));
                cursor += static_cast<size_t>(length);
            } else {
                ok = false;
                break;
            }
        }
        pos = recordEnd;
        if (!ok || installId == 0 || payload.size() < 200) continue;

        const std::wstring text = Utf8ToWide(payload);
        if (text.find(L"root:") == std::wstring::npos) continue;

        UbisoftConfigEntry entry;
        entry.installId = std::to_wstring(installId);
        entry.launchId = std::to_wstring(launchId == 0 ? installId : launchId);
        entry.title = ResolveUbisoftTitle(text);
        entry.thumbnailHash = UbisoftThumbnailHash(text);
        entry.ownershipIds = ConfigOwnershipIds(text);
        if (entry.title.empty() || entry.ownershipIds.empty()) continue;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<std::wstring> ExtractUbisoftOwnershipIds(const std::string& blob) {
    return ScanUuids(blob);
}

std::vector<OwnedGame> ScanUbisoftOwnedGames() {
    std::vector<OwnedGame> games;

    wchar_t localAppData[MAX_PATH] = { 0 };
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) != S_OK) {
        GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    }
    if (localAppData[0] == L'\0') return games;

    const std::filesystem::path cacheDir =
        std::filesystem::path(localAppData) / L"Ubisoft Game Launcher" / L"cache";

    std::string configurationBlob;
    if (!ReadFileBytes(cacheDir / L"configuration" / L"configurations", configurationBlob)) {
        return games;
    }

    const std::vector<UbisoftConfigEntry> entries = ParseUbisoftConfigurationCache(configurationBlob);
    if (entries.empty()) return games;

    // Ownership files list app/space ids; several ids can point at the same game config.
    std::unordered_map<std::wstring, const UbisoftConfigEntry*> byOwnershipId;
    for (const UbisoftConfigEntry& entry : entries) {
        for (const std::wstring& id : entry.ownershipIds) {
            byOwnershipId.emplace(id, &entry);
        }
    }

    std::unordered_map<std::wstring, bool> emittedInstallIds;
    std::error_code ec;
    const std::filesystem::path ownershipDir = cacheDir / L"ownership";
    for (const auto& file : std::filesystem::directory_iterator(ownershipDir, ec)) {
        if (!file.is_regular_file(ec)) continue;

        std::string ownershipBlob;
        if (!ReadFileBytes(file.path(), ownershipBlob)) continue;

        for (const std::wstring& id : ExtractUbisoftOwnershipIds(ownershipBlob)) {
            const auto it = byOwnershipId.find(id);
            if (it == byOwnershipId.end()) continue;

            const UbisoftConfigEntry& entry = *it->second;
            if (!emittedInstallIds.emplace(entry.installId, true).second) continue;

            OwnedGame game;
            game.appId = L"uplay_" + entry.installId;
            game.storeKey = L"ubisoft";
            game.storeName = L"Ubisoft Connect";
            game.productId = entry.installId;

            std::wstring editionLabel;
            game.title = ExtractBaseTitle(entry.title, &editionLabel);
            game.sortTitle = OwnedGamesSortTitle(game.title);

            // Official Ubisoft CDN box art, the same asset base the installed-game scanner uses.
            if (!entry.thumbnailHash.empty()) {
                game.coverArtUrl = L"https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/" +
                                   entry.thumbnailHash;
            }

            // Ubisoft Connect's own protocol; installing and authentication stay in the client.
            game.installUri = L"uplay://launch/" + entry.installId + L"/0";
            game.sources.push_back({ game.storeKey, game.storeName, game.productId,
                                     game.installUri, game.storePageUrl, editionLabel, false,
                                     true });
            games.push_back(std::move(game));
        }
    }
    return games;
}

std::vector<LegendaryAssetEntry> ParseLegendaryWindowsAssets(const std::wstring& assetsJson) {
    std::vector<LegendaryAssetEntry> assets;

    // Newer caches key the Windows platform list as "Windows"; older ones use "Win32".
    for (const wchar_t* platform : { L"Windows", L"Win32" }) {
        const std::wstring needle = L"\"" + std::wstring(platform) + L"\"";
        const size_t keyPos = assetsJson.find(needle);
        if (keyPos == std::wstring::npos) continue;

        const size_t arrayStart = assetsJson.find(L'[', keyPos);
        if (arrayStart == std::wstring::npos) continue;
        const size_t arrayEnd = FindMatchingBrace(assetsJson, arrayStart, L'[', L']');
        if (arrayEnd == std::wstring::npos || arrayEnd <= arrayStart) continue;

        size_t cursor = arrayStart + 1;
        while (cursor < arrayEnd) {
            const size_t objStart = assetsJson.find(L'{', cursor);
            if (objStart == std::wstring::npos || objStart >= arrayEnd) break;
            const size_t objEnd = FindMatchingBrace(assetsJson, objStart, L'{', L'}');
            if (objEnd == std::wstring::npos || objEnd > arrayEnd) break;

            LegendaryAssetEntry asset;
            asset.appName = JsonString(assetsJson, L"app_name", objStart, objEnd);
            asset.namespaceId = JsonString(assetsJson, L"namespace", objStart, objEnd);
            asset.catalogItemId = JsonString(assetsJson, L"catalog_item_id", objStart, objEnd);

            // Legendary's own game list skips Unreal Engine assets; they are tools, not games.
            const bool isUnrealEngine = ToLower(asset.namespaceId) == L"ue";
            const bool duplicate = std::any_of(
                assets.begin(), assets.end(),
                [&asset](const LegendaryAssetEntry& existing) { return existing.appName == asset.appName; });
            if (!asset.appName.empty() && !isUnrealEngine && !duplicate) {
                assets.push_back(std::move(asset));
            }
            cursor = objEnd + 1;
        }
        break;
    }
    return assets;
}

std::vector<OwnedGame> BuildLegendaryOwnedGames(
    const std::vector<LegendaryAssetEntry>& assets,
    const std::vector<LegendaryMetadataEntry>& metadata) {
    std::vector<OwnedGame> games;

    std::unordered_map<std::wstring, const LegendaryMetadataEntry*> metadataByApp;
    for (const LegendaryMetadataEntry& entry : metadata) {
        metadataByApp.emplace(ToLower(entry.appName), &entry);
    }

    for (const LegendaryAssetEntry& asset : assets) {
        std::wstring title;
        std::wstring synopsis;
        std::wstring artwork;
        std::wstring addedDate;

        const auto metaIt = metadataByApp.find(ToLower(asset.appName));
        if (metaIt != metadataByApp.end()) {
            const std::wstring& json = metaIt->second->json;
            title = JsonString(json, L"app_title", 0, json.size());
            synopsis = JsonString(json, L"description", 0, json.size());
            artwork = LegendaryKeyImage(json);
            addedDate = JsonString(json, L"dateAdded", 0, json.size());
        }

        // Without metadata, an opaque 32-hex app name is not a browsable title.
        if (title.empty()) title = asset.appName;
        if (LooksLikeOpaqueId(title)) continue;

        OwnedGame game;
        game.appId = L"epic_" + asset.appName;
        game.storeKey = L"epic";
        game.storeName = L"Epic Games";
        game.productId = asset.appName;

        std::wstring editionLabel;
        game.title = ExtractBaseTitle(SanitizeStoreTitle(title), &editionLabel);
        if (game.title.empty()) continue;
        game.sortTitle = OwnedGamesSortTitle(game.title);
        game.coverArtUrl = std::move(artwork);
        game.synopsis = NormalizeParagraphs(synopsis);

        if (addedDate.size() >= 4) {
            const int year = _wtoi(addedDate.substr(0, 4).c_str());
            if (year > 1980 && year < 2100) game.releaseYear = year;
        }

        // Same official protocol the installed Epic scanner uses, so an owned-but-uninstalled row
        // installs through the Epic launcher, never through legendary.
        game.installUri = L"com.epicgames.launcher://apps/" + asset.appName + L"?action=install";
        game.sources.push_back({ game.storeKey, game.storeName, game.productId, game.installUri,
                                 game.storePageUrl, editionLabel, false, true });
        games.push_back(std::move(game));
    }
    return games;
}

std::vector<OwnedGame> ScanLegendaryEpicOwnedGames() {
    std::vector<OwnedGame> games;

    // Legendary writes its cache under the user profile on Windows; older installs used AppData.
    std::filesystem::path baseDir;
    wchar_t profile[MAX_PATH] = { 0 };
    if (SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, profile) == S_OK) {
        const std::filesystem::path candidate =
            std::filesystem::path(profile) / L".config" / L"legendary";
        if (std::filesystem::exists(candidate / L"assets.json")) baseDir = candidate;
    }
    if (baseDir.empty()) {
        wchar_t localAppData[MAX_PATH] = { 0 };
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) == S_OK) {
            const std::filesystem::path candidate =
                std::filesystem::path(localAppData) / L"legendary";
            if (std::filesystem::exists(candidate / L"assets.json")) baseDir = candidate;
        }
    }
    if (baseDir.empty()) return games;

    // `assets.json` is written after the user authenticates legendary, and lists exactly the
    // account's owned assets per platform - the same source legendary's own `list` uses.
    std::string assetsBlob;
    if (!ReadFileBytes(baseDir / L"assets.json", assetsBlob)) return games;

    const std::vector<LegendaryAssetEntry> assets = ParseLegendaryWindowsAssets(Utf8ToWide(assetsBlob));
    if (assets.empty()) return games;

    std::vector<LegendaryMetadataEntry> metadata;
    metadata.reserve(assets.size());
    for (const LegendaryAssetEntry& asset : assets) {
        std::string metadataBlob;
        if (!ReadFileBytes(baseDir / L"metadata" / (asset.appName + L".json"), metadataBlob)) continue;
        metadata.push_back({ asset.appName, Utf8ToWide(metadataBlob) });
    }

    return BuildLegendaryOwnedGames(assets, metadata);
}

std::wstring ExtractBaseTitle(const std::wstring& title, std::wstring* outEditionLabel) {
    if (outEditionLabel != nullptr) outEditionLabel->clear();

    // Common edition / variant separators and suffix patterns matched case-insensitively.
    // Order matters: longer / more-specific patterns must come first.
    static const wchar_t* const kEditionSuffixes[] = {
        // Storefront attribution suffixes
        L"- amazon prime",
        L"- pc game pass",
        L"- game pass",
        L"- xbox game pass",
        L"- with xbox game pass",
        // Edition keywords (preceded by " - " or ": ")
        L"- ultimate edition",
        L": ultimate edition",
        L"- legendary edition",
        L": legendary edition",
        L"- definitive edition",
        L": definitive edition",
        L"- complete edition",
        L": complete edition",
        L"- gold edition",
        L": gold edition",
        L"- platinum edition",
        L": platinum edition",
        L"- anniversary edition",
        L": anniversary edition",
        L"- goty edition",
        L": goty edition",
        L"- game of the year edition",
        L": game of the year edition",
        L"- digital deluxe edition",
        L": digital deluxe edition",
        L"- deluxe edition",
        L": deluxe edition",
        L"- premium edition",
        L": premium edition",
        L"- enhanced edition",
        L": enhanced edition",
        L"- remastered edition",
        L": remastered edition",
        L"- remastered",
        L": remastered",
        L"- director's cut",
        L": director's cut",
        L"- directors cut",
        L": directors cut",
        L"- expanded edition",
        L": expanded edition",
        L"- extended edition",
        L": extended edition",
        L"- special edition",
        L": special edition",
        L"- collector's edition",
        L": collector's edition",
        L"- collectors edition",
        L": collectors edition",
        L"- standard edition",
        L": standard edition",
        L"- base edition",
        L": base edition",
        L"- console edition",
        L": console edition",
        L"- pc edition",
        L": pc edition",
        // Parenthesized editions
        L"(game of the year edition)",
        L"(goty edition)",
        L"(definitive edition)",
        L"(complete edition)",
        L"(deluxe edition)",
        L"(digital deluxe edition)",
        L"(ultimate edition)",
        L"(special edition)",
        L"(remastered)",
        L"(enhanced edition)",
        L"(anniversary edition)",
        L"(collector's edition)",
        L"(standard edition)",
        L"(director's cut)",
        // Preview / beta tags
        L"- console edition (game preview)",
        L"- game preview",
        L"(game preview)",
        L"(preview)",
        L"(beta)",
        L"(early access)",
        nullptr
    };

    // Lowercase copy of title for case-insensitive matching.
    std::wstring lower;
    lower.reserve(title.size());
    for (const wchar_t ch : title) lower.push_back(static_cast<wchar_t>(std::towlower(ch)));

    for (size_t i = 0; kEditionSuffixes[i] != nullptr; ++i) {
        const std::wstring suffix(kEditionSuffixes[i]);
        // Accept the suffix anywhere after at least one character of base title (preceded by space
        // or colon so we don't clip words like "Preview" that are part of a proper subtitle).
        const size_t pos = lower.find(suffix);
        if (pos == std::wstring::npos || pos == 0) continue;

        // If suffix begins with a separator (':', '-', '(', '['), the preceding character
        // can be alphanumeric (e.g. "Hunt: Complete Edition", "Game(GOTY)"). Otherwise,
        // the character immediately before the suffix must be a space so we don't clip mid-word.
        const wchar_t firstCh = suffix.empty() ? L'\0' : suffix[0];
        const bool isPunctSep = (firstCh == L':' || firstCh == L'-' || firstCh == L'(' || firstCh == L'[');
        if (!isPunctSep && lower[pos - 1] != L' ' && lower[pos - 1] != L':') continue;

        const std::wstring base = title.substr(0, pos);

        // Trim trailing " -" or " :" from the base fragment.
        size_t end = base.size();
        while (end > 0 && std::iswspace(base[end - 1]) != 0) --end;
        if (end > 1 && (base[end - 1] == L'-' || base[end - 1] == L':')) --end;
        while (end > 0 && std::iswspace(base[end - 1]) != 0) --end;

        if (end == 0) continue; // Don't strip the whole title.

        if (outEditionLabel != nullptr) {
            // Re-derive the suffix from the original case.
            size_t suffixStart = pos;
            // Skip leading " - " or " : " spacer that is part of the separator, not the label.
            while (suffixStart < title.size() && (std::iswspace(title[suffixStart]) != 0
                                                  || title[suffixStart] == L'-'
                                                  || title[suffixStart] == L':')) {
                ++suffixStart;
            }
            *outEditionLabel = title.substr(suffixStart);
            // Trim trailing whitespace.
            size_t labelEnd = outEditionLabel->size();
            while (labelEnd > 0 && std::iswspace((*outEditionLabel)[labelEnd - 1]) != 0) --labelEnd;
            outEditionLabel->resize(labelEnd);

            // If the extracted label is enclosed in outer parentheses or brackets, unwrap them.
            if (outEditionLabel->size() >= 2) {
                if ((outEditionLabel->front() == L'(' && outEditionLabel->back() == L')') ||
                    (outEditionLabel->front() == L'[' && outEditionLabel->back() == L']')) {
                    *outEditionLabel = outEditionLabel->substr(1, outEditionLabel->size() - 2);
                    while (!outEditionLabel->empty() && std::iswspace(outEditionLabel->front()) != 0) {
                        *outEditionLabel = outEditionLabel->substr(1);
                    }
                    while (!outEditionLabel->empty() && std::iswspace(outEditionLabel->back()) != 0) {
                        outEditionLabel->pop_back();
                    }
                }
            }
        }
        return title.substr(0, end);
    }

    return title; // No recognised suffix: return title unchanged.
}

std::wstring NormalizeTitleKey(const std::wstring& title) {
    const std::wstring base = OwnedGamesSortTitle(ExtractBaseTitle(title));
    std::wstring norm;
    norm.reserve(base.size());
    for (const wchar_t ch : base) {
        if (std::iswalnum(ch) != 0) {
            norm.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
    }
    return norm;
}

std::vector<std::wstring> JsonStringArray(const std::wstring& json, const std::wstring& key) {
    std::vector<std::wstring> out;

    size_t arrayStart = std::wstring::npos;
    if (key.empty()) {
        arrayStart = json.find(L'[');
    } else {
        const std::wstring needle = L"\"" + key + L"\"";
        const size_t pos = json.find(needle);
        if (pos == std::wstring::npos) return out;
        const size_t colon = json.find(L':', pos + needle.size());
        if (colon == std::wstring::npos) return out;
        arrayStart = json.find(L'[', colon);
    }
    if (arrayStart == std::wstring::npos) return out;

    const size_t arrayEnd = FindMatchingBrace(json, arrayStart, L'[', L']');
    if (arrayEnd == std::wstring::npos) return out;

    size_t cursor = arrayStart + 1;
    while (cursor < arrayEnd) {
        const size_t quote = json.find(L'"', cursor);
        if (quote == std::wstring::npos || quote >= arrayEnd) break;

        std::wstring raw;
        size_t i = quote + 1;
        while (i < arrayEnd && json[i] != L'"') {
            if (json[i] == L'\\' && i + 1 < arrayEnd) raw.push_back(json[i++]);
            raw.push_back(json[i++]);
        }
        if (i >= arrayEnd) break;

        out.push_back(UnescapeJson(raw));
        cursor = i + 1;
    }
    return out;
}

bool OwnedGamesPlatformsIncludeWindows(const std::vector<std::wstring>& tokens, bool& hasEvidence) {
    hasEvidence = !tokens.empty();
    for (const std::wstring& token : tokens) {
        const std::wstring lowered = ToLower(Trim(token));
        if (lowered.empty()) continue;
        // Xbox publishes "PC"; itch.io publishes "windows-amd64"/"windows-386"; a few stores use
        // "Windows" or "Win32"/"Win64". Nothing else in these lists names a PC build.
        if (lowered == L"pc" || lowered == L"win32" || lowered == L"win64") return true;
        if (lowered.find(L"windows") != std::wstring::npos) return true;
    }
    return false;
}

bool GogOsPieceSupportsWindows(const std::wstring& json, bool& hasEvidence) {
    hasEvidence = false;
    const std::wstring needle = L"\"supported\"";
    const size_t pos = json.find(needle);
    if (pos == std::wstring::npos) return false;
    const size_t colon = json.find(L':', pos + needle.size());
    if (colon == std::wstring::npos) return false;
    const size_t arrayStart = json.find(L'[', colon);
    if (arrayStart == std::wstring::npos) return false;
    const size_t arrayEnd = FindMatchingBrace(json, arrayStart, L'[', L']');
    if (arrayEnd == std::wstring::npos) return false;

    // An empty `supported` array is GOG's "compatibility not published" state, not "no platforms".
    const size_t firstObject = json.find(L'{', arrayStart);
    if (firstObject == std::wstring::npos || firstObject >= arrayEnd) return false;
    hasEvidence = true;

    const std::wstring supported = ToLower(json.substr(arrayStart, arrayEnd - arrayStart + 1));
    return supported.find(L"\"slug\":\"windows\"") != std::wstring::npos ||
           supported.find(L"\"slug\": \"windows\"") != std::wstring::npos ||
           supported.find(L"\"name\":\"windows\"") != std::wstring::npos ||
           supported.find(L"\"name\": \"windows\"") != std::wstring::npos;
}

bool ParseSteamStoreBrowsePlatforms(const std::wstring& json,
                                    std::vector<std::wstring>& outNonWindows) {
    outNonWindows.clear();
    const size_t itemsPos = json.find(L"\"store_items\"");
    if (itemsPos == std::wstring::npos) return false;
    const size_t arrayStart = json.find(L'[', itemsPos);
    if (arrayStart == std::wstring::npos) return false;
    const size_t arrayEnd = FindMatchingBrace(json, arrayStart, L'[', L']');
    if (arrayEnd == std::wstring::npos) return false;

    bool sawPlatforms = false;
    size_t cursor = arrayStart + 1;
    while (cursor < arrayEnd) {
        const size_t objStart = json.find(L'{', cursor);
        if (objStart == std::wstring::npos || objStart >= arrayEnd) break;
        const size_t objEnd = FindMatchingBrace(json, objStart, L'{', L'}');
        if (objEnd == std::wstring::npos || objEnd > arrayEnd) break;

        const size_t platformsPos = json.find(L"\"platforms\"", objStart);
        if (platformsPos != std::wstring::npos && platformsPos < objEnd) {
            const size_t platformsColon = json.find(L':', platformsPos + 11);
            if (platformsColon != std::wstring::npos && platformsColon < objEnd) {
                const size_t blockStart = SkipSpace(json, platformsColon + 1, objEnd);
                if (blockStart < objEnd && json[blockStart] == L'{') {
                    const size_t blockEnd = FindMatchingBrace(json, blockStart, L'{', L'}');
                    if (blockEnd != std::wstring::npos && blockEnd <= objEnd) {
                        // A platform block is evidence even when this particular app failed to
                        // resolve; only apps that explicitly lack a Windows build are dropped.
                        sawPlatforms = true;
                        const long long appid = JsonInt(json, L"appid", objStart, objEnd);
                        if (appid > 0 && !JsonBool(json, L"windows", blockStart, blockEnd)) {
                            outNonWindows.push_back(std::to_wstring(appid));
                        }
                    }
                }
            }
        }

        cursor = objEnd + 1;
    }
    return sawPlatforms;
}

bool FetchSteamNonWindowsAppIds(const std::vector<std::wstring>& appIds,
                                std::vector<std::wstring>& outNonWindows, std::wstring& error) {
    outNonWindows.clear();
    error.clear();
    if (appIds.empty()) return true;

    // StoreBrowse accepts at most 100 app ids per request.
    constexpr size_t kBatchSize = 100;
    for (size_t start = 0; start < appIds.size(); start += kBatchSize) {
        const size_t end = std::min(start + kBatchSize, appIds.size());

        std::wstring ids;
        for (size_t i = start; i < end; ++i) {
            if (!ids.empty()) ids += L',';
            ids += L"{\"appid\":" + appIds[i] + L"}";
        }
        const std::wstring payload =
            L"{\"ids\":[" + ids + L"],\"context\":{\"language\":\"english\","
            L"\"country_code\":\"US\",\"steam_realm\":\"1\"},"
            L"\"data_request\":{\"include_platforms\":true}}";
        const std::wstring path = L"/IStoreBrowseService/GetItems/v1/?input_json=" +
                                  PercentEncodeQuery(WideToUtf8(payload));

        std::string body;
        if (!HttpGet(kSteamHost, path, body)) {
            outNonWindows.clear();
            error = L"The Steam store-browse service could not be reached; every owned Steam game "
                    L"was kept.";
            return false;
        }

        std::vector<std::wstring> batch;
        if (ParseSteamStoreBrowsePlatforms(Utf8ToWide(body), batch)) {
            outNonWindows.insert(outNonWindows.end(), batch.begin(), batch.end());
        }
    }
    return true;
}

std::vector<OwnedGame> FilterWindowsInstallableOwnedGames(std::vector<OwnedGame> games) {
    std::vector<OwnedGame> kept;
    kept.reserve(games.size());

    for (OwnedGame& game : games) {
        // No sources means the platform is unknown, not unsupported.
        if (game.sources.empty()) {
            kept.push_back(std::move(game));
            continue;
        }

        game.sources.erase(std::remove_if(game.sources.begin(), game.sources.end(),
                                          [](const StorefrontSource& source) {
                                              return !source.windowsInstallable;
                                          }),
                           game.sources.end());
        if (game.sources.empty()) continue;

        // Rebuild the composite labels from the surviving sources so the catalogue and its
        // storefront filter never mention a store that was removed.
        std::wstring names;
        std::wstring keys;
        for (const StorefrontSource& source : game.sources) {
            if (!OwnedGamesHasToken(names, source.storeName)) {
                if (!names.empty()) names += L", ";
                names += source.storeName;
            }
            if (!OwnedGamesHasToken(keys, source.storeKey)) {
                if (!keys.empty()) keys += L", ";
                keys += source.storeKey;
            }
        }
        game.storeName = std::move(names);
        game.storeKey = std::move(keys);
        game.installUri = game.sources.front().installUri;
        game.storePageUrl = game.sources.front().storePageUrl;
        kept.push_back(std::move(game));
    }

    return kept;
}

std::vector<OwnedGame> ScanAllOwnedGamesDirect(const std::wstring& steamApiKey,
                                              const std::wstring& steamId64) {
    std::vector<OwnedGame> allOwned;
    std::unordered_map<std::wstring, size_t> indexByTitleKey;
    std::unordered_map<std::wstring, size_t> indexByAppId;

    auto addOrUpdate = [&](OwnedGame&& game) {
        if (game.sources.empty() && !game.storeKey.empty()) {
            game.sources.push_back({ game.storeKey, game.storeName, game.productId,
                                     game.installUri, game.storePageUrl, {}, game.installed,
                                     true });
        }

        const std::wstring titleKey = NormalizeTitleKey(game.title);
        const std::wstring appIdKey = ToLower(game.appId);

        size_t existingIdx = static_cast<size_t>(-1);
        const auto itTitle = !titleKey.empty() ? indexByTitleKey.find(titleKey) : indexByTitleKey.end();
        if (itTitle != indexByTitleKey.end()) {
            existingIdx = itTitle->second;
        } else {
            const auto itApp = indexByAppId.find(appIdKey);
            if (itApp != indexByAppId.end()) {
                existingIdx = itApp->second;
            }
        }

        if (existingIdx != static_cast<size_t>(-1) && existingIdx < allOwned.size()) {
            OwnedGame& existing = allOwned[existingIdx];

            // Merge sources
            for (const auto& newSrc : game.sources) {
                bool found = false;
                for (auto& exSrc : existing.sources) {
                    const bool sameStore = (CompareText(exSrc.storeKey, newSrc.storeKey) == 0);
                    const bool sameProduct = (!newSrc.productId.empty() && exSrc.productId == newSrc.productId);
                    const bool sameEdition = (CompareText(exSrc.editionLabel, newSrc.editionLabel) == 0);
                    if (sameStore && (sameProduct || sameEdition)) {
                        found = true;
                        if (!exSrc.windowsInstallable && newSrc.windowsInstallable) {
                            // A PC edition supersedes a console/macOS-only edition of the same
                            // title, so the merged row is still installable on Windows.
                            const bool wasInstalled = exSrc.installed;
                            exSrc = newSrc;
                            exSrc.installed = exSrc.installed || wasInstalled;
                        } else {
                            if (newSrc.installed) exSrc.installed = true;
                            if (exSrc.installUri.empty() && !newSrc.installUri.empty()) exSrc.installUri = newSrc.installUri;
                            if (exSrc.storePageUrl.empty() && !newSrc.storePageUrl.empty()) exSrc.storePageUrl = newSrc.storePageUrl;
                            if (exSrc.editionLabel.empty() && !newSrc.editionLabel.empty()) exSrc.editionLabel = newSrc.editionLabel;
                        }
                        break;
                    }
                }
                if (!found) {
                    existing.sources.push_back(newSrc);
                }
            }

            if (game.installed) existing.installed = true;

            // Rebuild distinct composite storeName and storeKey (avoiding duplicates like "GOG, GOG")
            std::wstring compositeNames;
            std::wstring compositeKeys;
            for (const auto& src : existing.sources) {
                if (!OwnedGamesHasToken(compositeNames, src.storeName)) {
                    if (!compositeNames.empty()) compositeNames += L", ";
                    compositeNames += src.storeName;
                }
                if (!OwnedGamesHasToken(compositeKeys, src.storeKey)) {
                    if (!compositeKeys.empty()) compositeKeys += L", ";
                    compositeKeys += src.storeKey;
                }
            }
            existing.storeName = compositeNames;
            existing.storeKey = compositeKeys;

            if (!existing.installed && game.installed) {
                existing.installUri = game.installUri;
                existing.storePageUrl = game.storePageUrl;
            } else if (existing.installUri.empty() && !game.installUri.empty()) {
                existing.installUri = game.installUri;
            }

            if (existing.coverArtUrl.empty() && !game.coverArtUrl.empty()) existing.coverArtUrl = game.coverArtUrl;
            if (game.playtimeMinutes > 0) existing.playtimeMinutes = game.playtimeMinutes;
            if (game.lastPlayedTimestamp > 0) existing.lastPlayedTimestamp = game.lastPlayedTimestamp;
            if (game.installSizeBytes > 0 && existing.installSizeBytes == 0) existing.installSizeBytes = game.installSizeBytes;
            if (existing.genres.empty() && !game.genres.empty()) existing.genres = game.genres;
            if (existing.tags.empty() && !game.tags.empty()) existing.tags = game.tags;
            if (existing.rating.empty() && !game.rating.empty()) existing.rating = game.rating;
            if (existing.synopsis.empty() && !game.synopsis.empty()) existing.synopsis = game.synopsis;
            if (existing.releaseYear == 0 && game.releaseYear > 0) existing.releaseYear = game.releaseYear;
        } else {
            const size_t newIdx = allOwned.size();
            if (!titleKey.empty()) indexByTitleKey[titleKey] = newIdx;
            indexByAppId[appIdKey] = newIdx;
            allOwned.push_back(std::move(game));
        }
    };

    auto mapInstalled = [&](std::vector<GameItem>&& items, const std::wstring& storeKey,
                            const std::wstring& storeName) {
        for (const GameItem& item : items) {
            OwnedGame og;
            og.appId = item.appId;
            og.storeKey = storeKey;
            og.storeName = storeName;

            // Strip edition suffix from the raw title; store it on the source so the
            // install popup can distinguish "Deluxe Edition" from the base game.
            std::wstring editionLabel;
            og.title = ExtractBaseTitle(item.title, &editionLabel);
            og.sortTitle = OwnedGamesSortTitle(og.title);

            og.coverArtUrl = item.coverArtPath;
            og.installed = true;
            og.installSizeBytes = item.diskSizeBytes;
            og.playtimeMinutes = item.playtimeMinutes;
            og.lastPlayedTimestamp = item.lastPlayedTimestamp;
            og.genres = item.genres;
            og.rating = item.rating;
            og.synopsis = item.synopsis;

            if (item.appId.rfind(storeKey + L"_", 0) == 0) {
                og.productId = item.appId.substr(storeKey.size() + 1);
            } else {
                og.productId = item.appId;
            }

            if (storeKey == L"steam") {
                og.installUri = L"steam://install/" + og.productId;
                og.storePageUrl = L"https://store.steampowered.com/app/" + og.productId + L"/";
            } else if (storeKey == L"epic") {
                og.installUri = L"com.epicgames.launcher://apps/" + og.productId + L"?action=install";
            } else if (storeKey == L"gog") {
                og.installUri = L"goggalaxy://openGameView/" + og.productId;
                og.storePageUrl = L"https://www.gog.com/game/" + og.productId;
            } else if (storeKey == L"itch") {
                og.installUri = L"itch://games/" + og.productId;
            } else if (storeKey == L"xbox") {
                og.installUri = L"ms-windows-store://pdp/?ProductId=" + og.productId;
            } else if (storeKey == L"ea") {
                og.installUri = L"origin2://game/launch?offerIds=" + og.productId;
            } else if (storeKey == L"ubisoft") {
                og.installUri = L"uplay://launch/" + og.productId + L"/0";
            } else if (storeKey == L"battlenet") {
                og.installUri = L"battlenet://" + og.productId;
            } else if (storeKey == L"amazon") {
                og.installUri = L"amazon-games://play/" + og.productId;
            }

            og.sources.push_back({ storeKey, storeName, og.productId, og.installUri,
                                   og.storePageUrl, editionLabel, true, true });
            addOrUpdate(std::move(og));
        }
    };

    // 1) Installed titles across all supported storefronts
    mapInstalled(StorefrontScanner::ScanSteam(), L"steam", L"Steam");
    mapInstalled(StorefrontScanner::ScanGOG(), L"gog", L"GOG");
    mapInstalled(StorefrontScanner::ScanEpic(), L"epic", L"Epic Games");
    mapInstalled(StorefrontScanner::ScanItch(), L"itch", L"itch.io");
    mapInstalled(StorefrontScanner::ScanUbisoft(), L"ubisoft", L"Ubisoft Connect");
    mapInstalled(StorefrontScanner::ScanRockstar(), L"rockstar", L"Rockstar Games");
    mapInstalled(StorefrontScanner::ScanEA(), L"ea", L"EA App");
    mapInstalled(StorefrontScanner::ScanAmazon(), L"amazon", L"Amazon Games");
    mapInstalled(StorefrontScanner::ScanXbox(), L"xbox", L"Xbox");
    mapInstalled(StorefrontScanner::ScanBattleNet(), L"battlenet", L"Battle.net");
    mapInstalled(StorefrontScanner::ScanLegacyGames(), L"legacygames", L"Legacy Games");
    mapInstalled(StorefrontScanner::ScanJagex(), L"jagex", L"Jagex");
    mapInstalled(StorefrontScanner::ScanGuildWars2(), L"guildwars2", L"Guild Wars 2");
    mapInstalled(StorefrontScanner::ScanRiot(), L"riot", L"Riot Games");
    mapInstalled(StorefrontScanner::ScanHoYoPlay(), L"hoyoplay", L"HoYoPlay");

    // 2) Uninstalled owned titles directly from storefront client DBs/APIs
    // GOG Galaxy
    for (OwnedGame& gogGame : ScanGogOwnedGamesGalaxy()) {
        addOrUpdate(std::move(gogGame));
    }

    // itch.io butler
    for (OwnedGame& itchGame : ScanItchOwnedGamesButler()) {
        addOrUpdate(std::move(itchGame));
    }

    // Xbox PC App AsyncCache
    for (OwnedGame& xboxGame : ScanXboxOwnedGamesAsyncCache()) {
        addOrUpdate(std::move(xboxGame));
    }

    // Ubisoft Connect ownership cache (lists owned games whether installed or not)
    for (OwnedGame& ubisoftGame : ScanUbisoftOwnedGames()) {
        addOrUpdate(std::move(ubisoftGame));
    }

    // Epic library cached by the optional legendary CLI. Read-only: the user authenticates and
    // installs through legendary/EGL themselves, and rows keep the official Epic install URI.
    for (OwnedGame& legendaryGame : ScanLegendaryEpicOwnedGames()) {
        addOrUpdate(std::move(legendaryGame));
    }

    // Steam Web API (if key is supplied or in settings)
    std::wstring apiKey = steamApiKey;
    std::wstring steamId = steamId64;
    if (apiKey.empty()) {
        const AppSettings settings = SettingsManager::Load();
        apiKey = settings.steamWebApiKey;
        if (steamId.empty()) steamId = settings.steamId64;
    }
    if (!apiKey.empty()) {
        std::vector<OwnedGame> steamGames;
        std::wstring steamError;
        if (FetchSteamOwnedGames(apiKey, steamId, steamGames, steamError)) {
            for (OwnedGame& sg : steamGames) {
                addOrUpdate(std::move(sg));
            }
        }
    }

    return FilterWindowsInstallableOwnedGames(std::move(allOwned));
}

std::wstring ResolveLocalSteamId64() {
    const std::wstring steamPath = SteamInstallPath();
    if (steamPath.empty()) return {};

    std::ifstream stream(std::filesystem::path(steamPath) / L"config" / L"loginusers.vdf",
                         std::ios::binary);
    if (!stream.is_open()) return {};

    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return SteamIdFromLoginUsers(text);
}

bool FetchSteamOwnedGames(const std::wstring& apiKey, const std::wstring& steamId64,
                          std::vector<OwnedGame>& out, std::wstring& error) {
    out.clear();
    error.clear();

    if (apiKey.empty()) {
        error = L"Add your Steam Web API key in Configuration to sync owned Steam games.";
        return false;
    }

    // The key alone is enough for the signed-in owner: fall back to the local client's account id.
    const std::wstring steamId = steamId64.empty() ? ResolveLocalSteamId64() : steamId64;
    if (steamId.empty()) {
        error = L"Add your SteamID64 in Configuration - the local Steam account could not be read.";
        return false;
    }

    const std::wstring path = L"/IPlayerService/GetOwnedGames/v1/?key=" + apiKey +
                              L"&steamid=" + steamId +
                              L"&include_appinfo=1&include_played_free_games=1&format=json";

    std::string body;
    if (!HttpGet(kSteamHost, path, body)) {
        error = L"Could not reach the Steam Web API. Check your connection and API key.";
        return false;
    }

    if (!ParseSteamOwnedGamesJson(Utf8ToWide(body), out)) {
        error = L"The Steam Web API returned a response this build could not read.";
        return false;
    }
    if (out.empty()) {
        error = L"Steam returned no owned games for the configured SteamID64.";
        return false;
    }

    // GetOwnedGames does not report platforms, so ask the official store-browse service in batches.
    // Each game carries its own verdict; FilterWindowsInstallableOwnedGames drops the non-Windows
    // ones later, together with every other storefront's non-Windows sources.
    std::vector<std::wstring> appIds;
    appIds.reserve(out.size());
    for (const OwnedGame& game : out) {
        if (!game.productId.empty()) appIds.push_back(game.productId);
    }

    std::vector<std::wstring> nonWindowsAppIds;
    std::wstring platformError;
    const bool haveVerdicts =
        FetchSteamNonWindowsAppIds(appIds, nonWindowsAppIds, platformError);
    (void)platformError; // A probe failure keeps every game rather than hiding a real library.

    for (OwnedGame& game : out) {
        const bool nonWindows =
            haveVerdicts && std::find(nonWindowsAppIds.begin(), nonWindowsAppIds.end(),
                                      game.productId) != nonWindowsAppIds.end();
        game.sources.push_back({ game.storeKey, game.storeName, game.productId, game.installUri,
                                 game.storePageUrl, {}, false, !nonWindows });
    }

    return true;
}

} // namespace GameExplorer
