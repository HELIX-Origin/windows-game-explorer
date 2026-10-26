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
            game.sortTitle = OwnedGamesSortTitle(game.title);
            game.storePageUrl = L"https://www.gog.com/game/" + prodId;
            game.installUri = L"goggalaxy://openGameView/" + prodId;
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

    const char* sql = "SELECT id, title, url, cover_url FROM games;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const long long id = sqlite3_column_int64(stmt, 0);
            const unsigned char* titleText = sqlite3_column_text(stmt, 1);
            const unsigned char* urlText = sqlite3_column_text(stmt, 2);
            const unsigned char* coverText = sqlite3_column_text(stmt, 3);
            if (id <= 0 || titleText == nullptr) continue;

            const std::wstring prodId = std::to_wstring(id);
            OwnedGame game;
            game.appId = L"itch_" + prodId;
            game.storeKey = L"itch";
            game.storeName = L"itch.io";
            game.productId = prodId;
            game.title = Utf8ToWide(reinterpret_cast<const char*>(titleText));
            game.sortTitle = OwnedGamesSortTitle(game.title);
            if (urlText != nullptr) game.storePageUrl = Utf8ToWide(reinterpret_cast<const char*>(urlText));
            if (coverText != nullptr) game.coverArtUrl = Utf8ToWide(reinterpret_cast<const char*>(coverText));
            game.installUri = L"itch://games/" + prodId;
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

            OwnedGame game;
            game.appId = L"xbox_" + storeId;
            game.storeKey = L"xbox";
            game.storeName = L"Xbox";
            game.productId = storeId;
            game.title = title;
            game.sortTitle = OwnedGamesSortTitle(title);
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

            game.sources.push_back({ game.storeKey, game.storeName, game.productId,
                                     game.installUri, game.storePageUrl, false });
            games.push_back(std::move(game));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return games;
}

std::wstring NormalizeTitleKey(const std::wstring& title) {
    const std::wstring base = OwnedGamesSortTitle(title);
    std::wstring norm;
    norm.reserve(base.size());
    for (const wchar_t ch : base) {
        if (std::iswalnum(ch) != 0) {
            norm.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
    }
    return norm;
}

std::vector<OwnedGame> ScanAllOwnedGamesDirect(const std::wstring& steamApiKey,
                                              const std::wstring& steamId64) {
    std::vector<OwnedGame> allOwned;
    std::unordered_map<std::wstring, size_t> indexByTitleKey;
    std::unordered_map<std::wstring, size_t> indexByAppId;

    auto addOrUpdate = [&](OwnedGame&& game) {
        if (game.sources.empty() && !game.storeKey.empty()) {
            game.sources.push_back({ game.storeKey, game.storeName, game.productId,
                                     game.installUri, game.storePageUrl, game.installed });
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
                    if (CompareText(exSrc.storeKey, newSrc.storeKey) == 0) {
                        found = true;
                        if (newSrc.installed) exSrc.installed = true;
                        if (exSrc.installUri.empty() && !newSrc.installUri.empty()) exSrc.installUri = newSrc.installUri;
                        if (exSrc.storePageUrl.empty() && !newSrc.storePageUrl.empty()) exSrc.storePageUrl = newSrc.storePageUrl;
                        break;
                    }
                }
                if (!found) {
                    existing.sources.push_back(newSrc);
                }
            }

            if (game.installed) existing.installed = true;

            // Rebuild composite storeName and storeKey
            std::wstring compositeNames;
            std::wstring compositeKeys;
            for (const auto& src : existing.sources) {
                if (!compositeNames.empty()) compositeNames += L", ";
                compositeNames += src.storeName;
                if (!compositeKeys.empty()) compositeKeys += L", ";
                compositeKeys += src.storeKey;
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
            og.title = item.title;
            og.sortTitle = OwnedGamesSortTitle(item.title);
            og.coverArtUrl = item.coverArtPath;
            og.installed = true;
            og.installSizeBytes = item.diskSizeBytes;
            og.playtimeMinutes = item.playtimeMinutes;
            og.lastPlayedTimestamp = item.lastPlayedTimestamp;
            og.genres = item.genres;
            og.rating = item.rating;

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

            og.sources.push_back({ storeKey, storeName, og.productId, og.installUri, og.storePageUrl, true });
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

    return allOwned;
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
    return true;
}

} // namespace GameExplorer
