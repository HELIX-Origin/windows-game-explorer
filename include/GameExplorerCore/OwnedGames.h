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
    std::wstring editionLabel;      // edition suffix stripped from the title, e.g. "Deluxe Edition",
                                    // "Console Edition (Game Preview)"; empty = base/standard edition
    bool installed{ false };
    // False only when this storefront publishes evidence that this edition has no Windows build
    // (an Xbox console-only product, a macOS/Linux-only GOG release, an itch.io non-Windows
    // upload). Unknown platform data stays true so a real owned game is never hidden. Every
    // scanner must set this from its own platform evidence; the catalogue drops non-Windows
    // sources in one place (FilterWindowsInstallableOwnedGames).
    bool windowsInstallable{ true };
    // Build identity recorded for the installed copy (Steam `buildid`, GOG `buildId`, Epic
    // `AppVersionString`). Empty when the storefront does not publish one locally.
    std::wstring installedBuildId;
    // Newest build the storefront client recorded locally (for example GOG Galaxy's imported
    // latest build id). Empty when no local comparison is possible. The Steam update check uses
    // the official Web API instead and leaves this empty.
    std::wstring latestBuildId;
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
    std::wstring developer;         // storefront-reported developer
    std::wstring publisher;         // storefront-reported publisher
    std::wstring releaseDate;       // storefront-reported release date (may be a bare year)
    std::wstring genres;            // comma-separated storefront genres
    std::wstring tags;              // comma-separated storefront tags
    std::wstring rating;            // storefront/ESRB/PEGI rating label
    std::wstring synopsis;          // overview / description
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

// Strips common edition/variant suffixes from a title, returning only the base game name.
// E.g. "7 Days to Die - Console Edition (Game Preview)" → "7 Days to Die"
//      "9 Years of Shadows - Amazon Prime" → "9 Years of Shadows"
//      "007 First Light - Deluxe Edition" → "007 First Light"
// The extracted suffix (edition label) is written to `outEditionLabel` when non-null.
[[nodiscard]] std::wstring ExtractBaseTitle(const std::wstring& title,
                                            std::wstring* outEditionLabel = nullptr);

// Normalizes a title into an alphanumeric lowercase key for cross-store deduplication.
// Uses ExtractBaseTitle first so editions of the same game share a key.
[[nodiscard]] std::wstring NormalizeTitleKey(const std::wstring& title);

// --- Windows-only platform gate ------------------------------------------------
// The catalogue only lists titles that can actually be installed on this Windows PC, so console,
// mobile and macOS/Linux-only editions never appear. Every storefront publishes platform data in its
// own shape; these helpers keep each decision pure and testable. A storefront that publishes no
// platform data is treated as "unknown" and keeps the title rather than hiding a real owned game.

// Parses a JSON array of strings, e.g. `"availablePlatforms":["PC","XboxOne"]`. An empty `key`
// parses a bare array such as itch.io's `scanned_platforms`.
[[nodiscard]] std::vector<std::wstring> JsonStringArray(const std::wstring& json,
                                                        const std::wstring& key);

// True when `tokens` names a Windows/PC build (Xbox `PC`, itch.io `windows-amd64`, GOG `Windows`).
// `hasEvidence` is false when the list is empty, meaning the storefront published nothing.
[[nodiscard]] bool OwnedGamesPlatformsIncludeWindows(const std::vector<std::wstring>& tokens,
                                                     bool& hasEvidence);

// GOG Galaxy `osCompatibility` game piece:
// `{"supported":[{"name":"Windows","slug":"windows","versions":null}]}`. `hasEvidence` is false for
// `{"supported":[]}`, which is GOG's "not published" state, not "unsupported".
[[nodiscard]] bool GogOsPieceSupportsWindows(const std::wstring& json, bool& hasEvidence);

// Steam `IStoreBrowseService/GetItems` response: collects every `appid` whose `platforms.windows`
// is false. Returns false when the payload contained no platform blocks at all.
[[nodiscard]] bool ParseSteamStoreBrowsePlatforms(const std::wstring& json,
                                                  std::vector<std::wstring>& outNonWindows);

// Official Steam store-browse probe, batched 100 app ids per request (no API key required). Returns
// false on transport failure with `outNonWindows` empty, so the caller keeps the whole library.
[[nodiscard]] bool FetchSteamNonWindowsAppIds(const std::vector<std::wstring>& appIds,
                                              std::vector<std::wstring>& outNonWindows,
                                              std::wstring& error);

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

// Single enforcement point for the Windows-only catalogue: removes every source a storefront
// marked as having no Windows build, recomputes the composite store labels, and drops a title only
// when it had sources and none survive. Titles with no sources are kept because their platform is
// unknown, never treated as unsupported.
[[nodiscard]] std::vector<OwnedGame> FilterWindowsInstallableOwnedGames(std::vector<OwnedGame> games);

// --- Ubisoft Connect owned library --------------------------------------------
// Ubisoft Connect keeps two read-only caches under
// `%LOCALAPPDATA%\Ubisoft Game Launcher\cache\`:
//   ownership\<accountId>            - the app/space ids the signed-in account owns
//   configuration\configurations     - one YAML game configuration per record
// The configuration file is a sequence of protobuf-style records
// (`0A <len> 08 <installId> 10 <launchId> 1A <yamlLen> <yaml>`), where `<installId>` is exactly
// the id Ubisoft's own `uplay://launch/<installId>/0` protocol expects. Titles are usually
// localization tokens (`l1`, `GAMENAME`) that resolve through `localizations.default`.
struct UbisoftConfigEntry {
    std::wstring installId;   // decimal install id, used by uplay://launch/<id>/0
    std::wstring launchId;    // decimal launch id (usually equal to installId)
    std::wstring title;       // resolved, human-readable title
    std::wstring thumbnailHash; // official `root.thumb_image` file name on Ubisoft's CDN
    // Every app/space UUID the configuration references, root or nested (crash reporting blocks
    // repeat them). Ownership files list these ids, so the full set is what maps a cache record to
    // an owned product.
    std::vector<std::wstring> ownershipIds;
};

// Parses the configuration cache blob. Malformed or non-game records are skipped, so a partial or
// newer cache still yields the entries it does contain.
[[nodiscard]] std::vector<UbisoftConfigEntry> ParseUbisoftConfigurationCache(const std::string& blob);

// Every UUID listed by one ownership cache file, lowercased and de-duplicated. The file is a
// binary blob; only the UUIDs are meaningful for the catalogue.
[[nodiscard]] std::vector<std::wstring> ExtractUbisoftOwnershipIds(const std::string& blob);

// Owned Ubisoft Connect library, installed or not. Reads the two caches above and never writes.
[[nodiscard]] std::vector<OwnedGame> ScanUbisoftOwnedGames();

// --- Epic Games through Legendary (optional, read-only) -----------------------
// Legendary is a third-party Epic CLI. When the user has run it, its cache already contains the
// account's owned PC assets (`assets.json`) plus per-app metadata, so the catalogue can list the
// library without authenticating, installing, or launching anything itself. Legendary's own
// `get_assets()` treats the platform asset list as the ownership truth, so this scanner does too.
// Games stay Epic rows: installing opens the official Epic launcher protocol.
struct LegendaryAssetEntry {
    std::wstring appName;
    std::wstring namespaceId;
    std::wstring catalogItemId;
};

struct LegendaryMetadataEntry {
    std::wstring appName;
    std::wstring json;   // contents of <appName>.json
};

// Reads the `Windows` asset list from legendary's `assets.json`, falling back to `Win32` for older
// caches. Unreal Engine namespace assets are skipped, matching legendary's own game list.
[[nodiscard]] std::vector<LegendaryAssetEntry> ParseLegendaryWindowsAssets(const std::wstring& assetsJson);

// Maps legendary assets + metadata to Epic owned games. Pure, so the mapping is unit-testable.
[[nodiscard]] std::vector<OwnedGame> BuildLegendaryOwnedGames(
    const std::vector<LegendaryAssetEntry>& assets,
    const std::vector<LegendaryMetadataEntry>& metadata);

// Scans legendary's local cache (`%USERPROFILE%\.config\legendary` or `%LOCALAPPDATA%\legendary`).
// Returns nothing when legendary was never configured, or when its cache predates a login.
[[nodiscard]] std::vector<OwnedGame> ScanLegendaryEpicOwnedGames();

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
