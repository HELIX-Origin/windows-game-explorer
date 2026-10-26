#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace GameExplorer {

struct StorefrontSource {
    std::wstring storeKey;          // machine key, e.g. "steam", "xbox", "gog"
    std::wstring storeName;         // display name, e.g. "Steam", "Xbox", "GOG"
    std::wstring productId;         // store-native id
    std::wstring installUri;        // official client protocol URI
    std::wstring storePageUrl;      // store page URL
    bool installed{ false };
};

/**
 * One title the user owns on a storefront, installed or not.
 *
 * The Game Catalogue exists so the shell library can stay strictly installed-games-only: a title
 * the user owns but has not installed is browsable here, and installing it is always delegated to
 * the official storefront client through `installUri`. This page never launches a game - playing is
 * the File Explorer Games view's job.
 */
struct OwnedGame {
    std::wstring appId;             // stable internal id, e.g. "steam_730"
    std::wstring storeKey;          // machine key(s), e.g. "steam" or "steam, xbox"
    std::wstring storeName;         // display name, e.g. "Steam" or "Steam, Xbox"
    std::wstring productId;         // store-native id, e.g. "730"
    std::wstring title;
    std::wstring sortTitle;
    std::wstring coverArtUrl;       // storefront-supplied artwork
    std::wstring storePageUrl;
    std::wstring installUri;        // official client protocol, e.g. "steam://install/730"
    std::wstring genres;            // comma-separated storefront genres
    std::wstring tags;              // comma-separated storefront tags
    std::wstring rating;            // storefront/ESRB/PEGI rating label
    std::vector<StorefrontSource> sources; // all storefronts where the user owns this game
    int releaseYear{ 0 };           // 0 when the storefront does not report one
    bool installed{ false };
    bool familyShared{ false };
    uint32_t playtimeMinutes{ 0 };
    int64_t lastPlayedTimestamp{ 0 };
    uint64_t installSizeBytes{ 0 };
};

enum class OwnedGamesSort {
    Title,
    ReleaseDate,
    Playtime,
    LastPlayed,
    Storefront,
    InstallSize,
};

enum class OwnedGamesSortDirection {
    Ascending,
    Descending,
};

/**
 * Catalogue filter. Every field is inclusive: an empty string or -1 means "any", so the default
 * filter keeps the whole owned library.
 */
struct OwnedGamesFilter {
    std::wstring search;            // case-insensitive title substring
    std::wstring storeKey;          // empty = every storefront
    int installed{ -1 };            // -1 any, 0 owned but not installed, 1 installed
    std::wstring genre;             // matches one comma-separated genre token
    std::wstring tag;               // matches one comma-separated tag token
    int releaseYear{ 0 };           // 0 = any year
    std::wstring rating;            // empty = any rating
    uint32_t minPlaytimeMinutes{ 0 };
    int familyShared{ -1 };         // -1 any, 0 not shared, 1 shared
};

// Case-insensitive substring test used by the search box and the token filters.
[[nodiscard]] bool OwnedGamesTextContains(const std::wstring& haystack, const std::wstring& needle);

// True when a comma-separated token list contains `token` (case-insensitive, trimmed).
[[nodiscard]] bool OwnedGamesHasToken(const std::wstring& list, const std::wstring& token);

// Normalizes a display title for sorting: leading articles ("The", "A", "An") are dropped so
// "The Witcher 3" files under W.
[[nodiscard]] std::wstring OwnedGamesSortTitle(const std::wstring& title);

// Normalizes a title into an alphanumeric lowercase key for cross-store deduplication.
[[nodiscard]] std::wstring NormalizeTitleKey(const std::wstring& title);

// True when the game or any of its storefront sources matches `targetStoreKey`.
[[nodiscard]] bool OwnedGameMatchesStore(const OwnedGame& game, const std::wstring& targetStoreKey);

// True when the game passes every active filter field.
[[nodiscard]] bool OwnedGameMatches(const OwnedGame& game, const OwnedGamesFilter& filter);

// Stable filter, preserving the incoming order.
[[nodiscard]] std::vector<OwnedGame> FilterOwnedGames(const std::vector<OwnedGame>& games,
                                                      const OwnedGamesFilter& filter);

// Stable sort. Ties fall back to sort title then app id, always ascending, so the catalogue never
// reshuffles rows that compare equal on the selected key.
void SortOwnedGames(std::vector<OwnedGame>& games, OwnedGamesSort sort,
                    OwnedGamesSortDirection direction);

// Filter then sort in one call - what the page does after any toolbar change.
[[nodiscard]] std::vector<OwnedGame> SelectOwnedGames(const std::vector<OwnedGame>& games,
                                                      const OwnedGamesFilter& filter,
                                                      OwnedGamesSort sort,
                                                      OwnedGamesSortDirection direction);

// Rows per page. A large owned library is browsed page by page rather than scrolled as one
// unbounded list, so every page change is a single filter/sort pass plus one slice.
inline constexpr int kOwnedGamesDefaultPageSize = 24;

struct OwnedGamesQuery {
    OwnedGamesFilter filter{};
    OwnedGamesSort sort{ OwnedGamesSort::Title };
    OwnedGamesSortDirection direction{ OwnedGamesSortDirection::Ascending };
    int pageSize{ kOwnedGamesDefaultPageSize };
    int pageIndex{ 0 };
};

struct OwnedGamesPage {
    std::vector<OwnedGame> items;   // only the rows on this page
    int totalMatches{ 0 };          // rows matching the filter across the whole library
    int pageIndex{ 0 };             // clamped page that produced `items`
    int pageCount{ 0 };             // 0 when nothing matches
    int pageSize{ kOwnedGamesDefaultPageSize };
};

// Clamps a page index into the valid range for a result count, so Prev/Next and a shrinking
// library can never point past the last page.
[[nodiscard]] int ClampOwnedGamesPageIndex(int pageIndex, int totalMatches, int pageSize);

// Applies filter, sort and paging. `query.pageSize` is clamped to at least 1.
[[nodiscard]] OwnedGamesPage QueryOwnedGames(const std::vector<OwnedGame>& games,
                                             const OwnedGamesQuery& query);

// Marks every owned entry whose app id appears in the installed library. Used to keep the
// catalogue's installed badge honest without duplicating the scanner.
void MarkInstalledOwnedGames(std::vector<OwnedGame>& games,
                             const std::vector<std::wstring>& installedAppIds);

// Steam Web API: IPlayerService/GetOwnedGames. Requires the user's Web API key; the SteamID64 is
// taken from Configuration when present, otherwise resolved from the local Steam client's
// loginusers.vdf. `error` receives a human-readable reason on failure.
[[nodiscard]] bool FetchSteamOwnedGames(const std::wstring& apiKey, const std::wstring& steamId64,
                                        std::vector<OwnedGame>& out, std::wstring& error);

// SteamID64 of the account the local Steam client last signed in with, read from
// `<Steam>/config/loginusers.vdf`. Empty when Steam or the file is not present.
[[nodiscard]] std::wstring ResolveLocalSteamId64();

// Pure JSON mapping, split out from the HTTP call so it can be unit-tested.
[[nodiscard]] bool ParseSteamOwnedGamesJson(const std::wstring& json, std::vector<OwnedGame>& out);

// Direct in-memory storefront scanners (no database persistence).
[[nodiscard]] std::vector<OwnedGame> ScanGogOwnedGamesGalaxy();
[[nodiscard]] std::vector<OwnedGame> ScanItchOwnedGamesButler();
[[nodiscard]] std::vector<OwnedGame> ScanXboxOwnedGamesAsyncCache();

// Scans and aggregates all owned games across all supported storefronts directly in memory.
// Includes installed games from local client manifests (Steam, Epic, GOG, itch, Xbox, EA,
// Ubisoft, Battle.net, Amazon, etc.) plus uninstalled owned games from Steam Web API,
// GOG Galaxy, and itch.io.
[[nodiscard]] std::vector<OwnedGame> ScanAllOwnedGamesDirect(const std::wstring& steamApiKey = {},
                                                             const std::wstring& steamId64 = {});

} // namespace GameExplorer
