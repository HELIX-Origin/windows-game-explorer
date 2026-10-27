#include "TestHarness.h"

#include "GameExplorerCore/OwnedGames.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace GameExplorer;

namespace {

void AppendVarint(std::string& out, uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

// Mirrors Ubisoft Connect's record framing: 0A <len> 08 <installId> 10 <launchId> 1A <yamlLen> <yaml>.
void AppendUbisoftRecord(std::string& blob, uint64_t installId, const std::string& yaml) {
    std::string body;
    body.push_back(0x08);
    AppendVarint(body, installId);
    body.push_back(0x10);
    AppendVarint(body, installId);
    body.push_back(0x1A);
    AppendVarint(body, yaml.size());
    body += yaml;
    blob.push_back(0x0A);
    AppendVarint(blob, body.size());
    blob += body;
}

OwnedGame MakeGame(const wchar_t* appId, const wchar_t* title, const wchar_t* store,
                   bool installed, int year, uint32_t minutes, const wchar_t* genres,
                   const wchar_t* tags, const wchar_t* rating, bool familyShared = false) {
    OwnedGame game;
    game.appId = appId;
    game.title = title;
    game.sortTitle = OwnedGamesSortTitle(title);
    game.storeKey = store;
    game.storeName = store;
    game.productId = appId;
    game.installed = installed;
    game.releaseYear = year;
    game.playtimeMinutes = minutes;
    game.genres = genres;
    game.tags = tags;
    game.rating = rating;
    game.familyShared = familyShared;
    return game;
}

} // namespace

TEST_CASE(OwnedGames_TokenMatchIsWholeTokenCaseInsensitive) {
    ASSERT_TRUE(OwnedGamesHasToken(L"Action, RPG, Indie", L"rpg"));
    ASSERT_TRUE(OwnedGamesHasToken(L"Single-player;Co-op", L"Single-player") == false);
    ASSERT_FALSE(OwnedGamesHasToken(L"Action, RPG", L"RPGs"));
    ASSERT_TRUE(OwnedGamesHasToken(L"Action, RPG", L""));
}

TEST_CASE(OwnedGames_SearchMatchesTitleCaseInsensitive) {
    ASSERT_TRUE(OwnedGamesTextContains(L"Half-Life 2", L"half"));
    ASSERT_FALSE(OwnedGamesTextContains(L"Half-Life 2", L"portal"));
    ASSERT_TRUE(OwnedGamesTextContains(L"Anything", L""));
}

TEST_CASE(OwnedGames_SortTitleDropsLeadingArticles) {
    ASSERT_EQ(OwnedGamesSortTitle(L"The Witcher 3"), std::wstring(L"Witcher 3"));
    ASSERT_EQ(OwnedGamesSortTitle(L"A Short Hike"), std::wstring(L"Short Hike"));
    ASSERT_EQ(OwnedGamesSortTitle(L"Anodyne"), std::wstring(L"Anodyne"));
    ASSERT_EQ(OwnedGamesSortTitle(L"The"), std::wstring(L"The"));
}

TEST_CASE(OwnedGames_FilterCombinesEveryActiveField) {
    const std::vector<OwnedGame> games{
        MakeGame(L"steam_1", L"Alpha", L"steam", true, 2019, 120, L"Action, RPG", L"Co-op", L"T"),
        MakeGame(L"gog_2", L"Beta", L"gog", false, 2021, 30, L"Puzzle", L"Solo", L"E", true),
        MakeGame(L"steam_3", L"Gamma", L"steam", false, 2019, 0, L"Action", L"PvP", L"T"),
    };

    OwnedGamesFilter filter;
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(3));

    filter.search = L"a";
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(3));

    filter.search = L"bet";
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(1));

    filter = {};
    filter.storeKey = L"steam";
    filter.installed = 0;
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(1));

    filter = {};
    filter.genre = L"Action";
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(2));

    filter = {};
    filter.releaseYear = 2019;
    filter.rating = L"t";
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(2));

    filter = {};
    filter.familyShared = 1;
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(1));

    filter = {};
    filter.minPlaytimeMinutes = 100;
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(1));

    filter = {};
    filter.tag = L"Co-op";
    filter.installed = 1;
    ASSERT_EQ(FilterOwnedGames(games, filter).size(), static_cast<size_t>(1));
}

TEST_CASE(OwnedGames_SortUsesSelectedKeyThenTitle) {
    std::vector<OwnedGame> games{
        MakeGame(L"steam_1", L"The Alpha", L"steam", true, 2019, 10, L"", L"", L""),
        MakeGame(L"gog_2", L"Beta", L"gog", false, 2021, 90, L"", L"", L""),
        MakeGame(L"steam_3", L"Alpha 2", L"epic", false, 2019, 50, L"", L"", L""),
    };

    SortOwnedGames(games, OwnedGamesSort::Title, OwnedGamesSortDirection::Ascending);
    // Leading articles are dropped from the sort key, so "The Alpha" files under A and precedes
    // the longer "Alpha 2".
    ASSERT_EQ(games[0].title, std::wstring(L"The Alpha"));
    ASSERT_EQ(games[1].title, std::wstring(L"Alpha 2"));
    ASSERT_EQ(games[2].title, std::wstring(L"Beta"));

    SortOwnedGames(games, OwnedGamesSort::Playtime, OwnedGamesSortDirection::Descending);
    ASSERT_EQ(games[0].title, std::wstring(L"Beta"));
    ASSERT_EQ(games[1].title, std::wstring(L"Alpha 2"));
    ASSERT_EQ(games[2].title, std::wstring(L"The Alpha"));

    SortOwnedGames(games, OwnedGamesSort::ReleaseDate, OwnedGamesSortDirection::Descending);
    ASSERT_EQ(games[0].title, std::wstring(L"Beta"));
    // Equal years fall back to the sort title, always ascending.
    ASSERT_EQ(games[1].title, std::wstring(L"The Alpha"));
    ASSERT_EQ(games[2].title, std::wstring(L"Alpha 2"));
}

TEST_CASE(OwnedGames_PagingSlicesAndClamps) {
    std::vector<OwnedGame> games;
    for (int i = 0; i < 100; ++i) {
        const std::wstring title = L"Game " + std::to_wstring(100 + i);
        games.push_back(MakeGame((L"steam_" + std::to_wstring(i)).c_str(), title.c_str(), L"steam",
                                 false, 2000, 0, L"", L"", L""));
    }

    OwnedGamesQuery query;
    query.pageSize = 48;
    OwnedGamesPage first = QueryOwnedGames(games, query);
    ASSERT_EQ(first.totalMatches, 100);
    ASSERT_EQ(first.pageCount, 3);
    ASSERT_EQ(first.pageIndex, 0);
    ASSERT_EQ(first.items.size(), static_cast<size_t>(48));
    ASSERT_EQ(first.items.front().title, std::wstring(L"Game 100"));
    ASSERT_EQ(first.items.back().title, std::wstring(L"Game 147"));

    query.pageIndex = 2;
    OwnedGamesPage last = QueryOwnedGames(games, query);
    ASSERT_EQ(last.pageIndex, 2);
    ASSERT_EQ(last.items.size(), static_cast<size_t>(4));

    query.pageIndex = 99;
    OwnedGamesPage clamped = QueryOwnedGames(games, query);
    ASSERT_EQ(clamped.pageIndex, 2);
    ASSERT_EQ(clamped.items.size(), static_cast<size_t>(4));

    query.pageIndex = 1;
    OwnedGamesFilter filter;
    filter.search = L"does-not-exist";
    query.filter = filter;
    OwnedGamesPage empty = QueryOwnedGames(games, query);
    ASSERT_EQ(empty.totalMatches, 0);
    ASSERT_EQ(empty.pageCount, 0);
    ASSERT_EQ(empty.pageIndex, 0);
    ASSERT_TRUE(empty.items.empty());

    query.pageSize = 0;
    query.filter = {};
    OwnedGamesPage minimum = QueryOwnedGames(games, query);
    ASSERT_EQ(minimum.pageSize, 1);
    ASSERT_EQ(minimum.pageCount, 100);
}

TEST_CASE(OwnedGames_MarkInstalledFollowsLibrary) {
    std::vector<OwnedGame> games{
        MakeGame(L"steam_1", L"Alpha", L"steam", false, 0, 0, L"", L"", L""),
        MakeGame(L"steam_2", L"Beta", L"steam", true, 0, 0, L"", L"", L""),
    };

    MarkInstalledOwnedGames(games, {L"steam_1"});
    ASSERT_TRUE(games[0].installed);
    ASSERT_FALSE(games[1].installed);
}

TEST_CASE(OwnedGames_SteamJsonParsesGamesAndSkipsEmptyName) {
    const std::wstring json =
        L"{\"response\":{\"game_count\":2,\"games\":["
        L"{\"appid\":730,\"name\":\"Counter-Strike 2\",\"playtime_forever\":1250,"
        L"\"rtime_last_played\":1700000000,\"img_icon_url\":\"abc\"},"
        L"{\"appid\":10,\"name\":\"\",\"playtime_forever\":0}"
        L"]}}";

    std::vector<OwnedGame> games;
    ASSERT_TRUE(ParseSteamOwnedGamesJson(json, games));
    ASSERT_EQ(games.size(), static_cast<size_t>(2));

    ASSERT_EQ(games[0].appId, std::wstring(L"steam_730"));
    ASSERT_EQ(games[0].storeKey, std::wstring(L"steam"));
    ASSERT_EQ(games[0].title, std::wstring(L"Counter-Strike 2"));
    ASSERT_EQ(games[0].productId, std::wstring(L"730"));
    ASSERT_EQ(games[0].installUri, std::wstring(L"steam://install/730"));
    ASSERT_EQ(games[0].storePageUrl, std::wstring(L"https://store.steampowered.com/app/730/"));
    ASSERT_EQ(games[0].playtimeMinutes, static_cast<uint32_t>(1250));
    ASSERT_EQ(games[0].lastPlayedTimestamp, static_cast<int64_t>(1700000000));
    ASSERT_FALSE(games[0].installed);

    // A missing name must not produce an empty catalogue row.
    ASSERT_EQ(games[1].title, std::wstring(L"Steam App 10"));
}

TEST_CASE(OwnedGames_SteamJsonHandlesEmptyLibraryAndGarbage) {
    std::vector<OwnedGame> games;

    ASSERT_TRUE(ParseSteamOwnedGamesJson(L"{\"response\":{\"game_count\":0}}", games));
    ASSERT_TRUE(games.empty());

    ASSERT_FALSE(ParseSteamOwnedGamesJson(L"not json at all", games));
    ASSERT_TRUE(games.empty());

    ASSERT_FALSE(ParseSteamOwnedGamesJson(L"{\"response\":[]}", games));
    ASSERT_TRUE(games.empty());
}

TEST_CASE(OwnedGames_NormalizeTitleKeyStripsPunctuationAndLeadingArticles) {
    ASSERT_EQ(NormalizeTitleKey(L"The Witcher 3: Wild Hunt"), std::wstring(L"witcher3wildhunt"));
    ASSERT_EQ(NormalizeTitleKey(L"Half-Life 2: Episode One!"), std::wstring(L"halflife2episodeone"));
    ASSERT_EQ(NormalizeTitleKey(L"A Short Hike"), std::wstring(L"shorthike"));
    ASSERT_EQ(NormalizeTitleKey(L"Anodyne"), std::wstring(L"anodyne"));
    ASSERT_EQ(NormalizeTitleKey(L"Cyberpunk 2077"), std::wstring(L"cyberpunk2077"));
    ASSERT_EQ(NormalizeTitleKey(L"--- !? ---"), std::wstring(L""));
}

TEST_CASE(OwnedGames_PlatformListsAreParsedFromStorefrontJson) {
    bool evidence = false;

    const std::vector<std::wstring> xbox = JsonStringArray(
        L"{\"cacheTimeInMS\":1,\"data\":{\"availablePlatforms\":[\"PC\",\"XboxOne\"]}}",
        L"availablePlatforms");
    ASSERT_EQ(xbox.size(), static_cast<size_t>(2));
    ASSERT_EQ(xbox[0], std::wstring(L"PC"));
    ASSERT_TRUE(OwnedGamesPlatformsIncludeWindows(xbox, evidence));
    ASSERT_TRUE(evidence);

    const std::vector<std::wstring> itch = JsonStringArray(
        L"[\"windows-amd64\",\"linux-x86_64\"]", L"");
    ASSERT_EQ(itch.size(), static_cast<size_t>(2));
    ASSERT_EQ(itch[1], std::wstring(L"linux-x86_64"));

    ASSERT_TRUE(JsonStringArray(L"{\"data\":{}}", L"availablePlatforms").empty());
    ASSERT_TRUE(JsonStringArray(L"", L"").empty());
}

TEST_CASE(OwnedGames_PcGateNeedsWindowsEvidence) {
    bool evidence = false;

    // Xbox names PC explicitly; console-only and cloud-only lists must fail the gate.
    ASSERT_FALSE(OwnedGamesPlatformsIncludeWindows({ L"XboxOne", L"XboxSeriesX", L"XCloud" },
                                                   evidence));
    ASSERT_TRUE(evidence);
    ASSERT_TRUE(OwnedGamesPlatformsIncludeWindows({ L"XboxSeriesX", L"PC", L"XCloud" }, evidence));
    ASSERT_TRUE(evidence);

    // itch.io publishes butler arch ids.
    ASSERT_TRUE(OwnedGamesPlatformsIncludeWindows({ L"windows-amd64", L"linux-x86_64" }, evidence));
    ASSERT_TRUE(evidence);
    ASSERT_FALSE(OwnedGamesPlatformsIncludeWindows({ L"osx-x86_64", L"linux-x86_64" }, evidence));
    ASSERT_TRUE(evidence);

    // No published list means unknown, so the title is kept.
    ASSERT_FALSE(OwnedGamesPlatformsIncludeWindows({}, evidence));
    ASSERT_FALSE(evidence);

    ASSERT_TRUE(OwnedGamesPlatformsIncludeWindows({ L"WIN32" }, evidence));
    ASSERT_TRUE(OwnedGamesPlatformsIncludeWindows({ L"Windows" }, evidence));
}

TEST_CASE(OwnedGames_GogOsCompatibilityNeedsWindows) {
    bool evidence = false;

    ASSERT_TRUE(GogOsPieceSupportsWindows(
        L"{\"supported\":[{\"name\":\"Windows\",\"slug\":\"windows\",\"versions\":null}]}",
        evidence));
    ASSERT_TRUE(evidence);

    ASSERT_FALSE(GogOsPieceSupportsWindows(
        L"{\"supported\":[{\"name\":\"MacOs\",\"slug\":\"macos\",\"versions\":null}]}", evidence));
    ASSERT_TRUE(evidence);

    // Empty `supported` is GOG's "not published" state: keep the title.
    ASSERT_FALSE(GogOsPieceSupportsWindows(L"{\"supported\":[]}", evidence));
    ASSERT_FALSE(evidence);

    ASSERT_FALSE(GogOsPieceSupportsWindows(L"{}", evidence));
    ASSERT_FALSE(evidence);
}

TEST_CASE(OwnedGames_SteamStoreBrowseMarksOnlyNonWindowsApps) {
    const std::wstring json =
        L"{\"response\":{\"store_items\":["
        L"{\"appid\":730,\"platforms\":{\"windows\":true,\"mac\":false,\"linux\":true}},"
        L"{\"appid\":570,\"platforms\":{\"windows\":false,\"mac\":true,\"linux\":true}},"
        L"{\"appid\":400,\"success\":0}"
        L"]}}";

    std::vector<std::wstring> nonWindows;
    ASSERT_TRUE(ParseSteamStoreBrowsePlatforms(json, nonWindows));
    ASSERT_EQ(nonWindows.size(), static_cast<size_t>(1));
    ASSERT_EQ(nonWindows[0], std::wstring(L"570"));

    // An unresolved app (no platform block) is not evidence of another platform.
    std::vector<std::wstring> missing;
    ASSERT_FALSE(ParseSteamStoreBrowsePlatforms(L"{\"response\":{}}", missing));
    ASSERT_TRUE(missing.empty());
}

TEST_CASE(OwnedGames_WindowsGateDropsOnlyNonWindowsSources) {
    OwnedGame consoleOnly;
    consoleOnly.title = L"Console Only";
    consoleOnly.storeName = L"Xbox";
    consoleOnly.storeKey = L"xbox";
    consoleOnly.sources.push_back({ L"xbox", L"Xbox", L"C1", L"", L"", L"", false, false });

    OwnedGame mixed;
    mixed.title = L"Mixed";
    mixed.storeName = L"Xbox, Steam";
    mixed.storeKey = L"xbox, steam";
    mixed.installUri = L"ms-windows-store://pdp/?ProductId=C2";
    mixed.storePageUrl = L"https://www.xbox.com/games/store/p/C2";
    mixed.sources.push_back({ L"xbox", L"Xbox", L"C2", L"ms-windows-store://pdp/?ProductId=C2",
                              L"https://www.xbox.com/games/store/p/C2", L"", false, false });
    mixed.sources.push_back({ L"steam", L"Steam", L"730", L"steam://install/730",
                              L"https://store.steampowered.com/app/730/", L"", false, true });

    OwnedGame unknown;
    unknown.title = L"Unknown Platform";
    unknown.storeName = L"Unknown";
    unknown.storeKey = L"unknown";

    OwnedGame windowsOnly;
    windowsOnly.title = L"Windows Only";
    windowsOnly.sources.push_back({ L"gog", L"GOG", L"1", L"goggalaxy://openGameView/1", L"", L"",
                                    false, true });

    const std::vector<OwnedGame> filtered = FilterWindowsInstallableOwnedGames(
        { consoleOnly, mixed, unknown, windowsOnly });

    ASSERT_EQ(filtered.size(), static_cast<size_t>(3));
    ASSERT_EQ(filtered[0].title, std::wstring(L"Mixed"));
    ASSERT_EQ(filtered[0].sources.size(), static_cast<size_t>(1));
    ASSERT_EQ(filtered[0].storeKey, std::wstring(L"steam"));
    ASSERT_EQ(filtered[0].storeName, std::wstring(L"Steam"));
    ASSERT_EQ(filtered[0].installUri, std::wstring(L"steam://install/730"));
    ASSERT_EQ(filtered[0].storePageUrl, std::wstring(L"https://store.steampowered.com/app/730/"));
    ASSERT_EQ(filtered[1].title, std::wstring(L"Unknown Platform"));
    ASSERT_EQ(filtered[1].sources.size(), static_cast<size_t>(0));
    ASSERT_EQ(filtered[2].title, std::wstring(L"Windows Only"));
}

TEST_CASE(OwnedGames_MatchesStoreChecksBothPrimaryAndAllSources) {
    OwnedGame game;
    game.title = L"Multi-Store Game";
    game.storeKey = L"steam, xbox";
    game.storeName = L"Steam, Xbox";
    game.sources.push_back({ L"steam", L"Steam", L"123", L"steam://install/123", L"", L"", false });
    game.sources.push_back({ L"xbox", L"Xbox", L"XYZ", L"ms-windows-store://pdp/?ProductId=XYZ", L"", L"", false });

    // Matches individual store keys
    ASSERT_TRUE(OwnedGameMatchesStore(game, L"steam"));
    ASSERT_TRUE(OwnedGameMatchesStore(game, L"STEAM"));
    ASSERT_TRUE(OwnedGameMatchesStore(game, L"xbox"));
    ASSERT_TRUE(OwnedGameMatchesStore(game, L"Xbox"));

    // Does not match other stores
    ASSERT_FALSE(OwnedGameMatchesStore(game, L"gog"));
    ASSERT_FALSE(OwnedGameMatchesStore(game, L"epic"));

    // Filter verification via OwnedGameMatches
    OwnedGamesFilter filterSteam;
    filterSteam.storeKey = L"steam";
    ASSERT_TRUE(OwnedGameMatches(game, filterSteam));

    OwnedGamesFilter filterXbox;
    filterXbox.storeKey = L"xbox";
    ASSERT_TRUE(OwnedGameMatches(game, filterXbox));

    OwnedGamesFilter filterGog;
    filterGog.storeKey = L"gog";
    ASSERT_FALSE(OwnedGameMatches(game, filterGog));
}


// ---------------------------------------------------------------------------
// ExtractBaseTitle – edition suffix stripping
// ---------------------------------------------------------------------------

TEST_CASE(OwnedGames_ExtractBaseTitleStripsCommonEditionSuffixes) {
    std::wstring edition;

    // Storefront attribution
    ASSERT_EQ(ExtractBaseTitle(L"9 Years of Shadows - Amazon Prime", &edition),
              std::wstring(L"9 Years of Shadows"));
    ASSERT_EQ(edition, std::wstring(L"Amazon Prime"));

    // Console / Game Preview (compound)
    ASSERT_EQ(ExtractBaseTitle(L"7 Days to Die - Console Edition (Game Preview)", &edition),
              std::wstring(L"7 Days to Die"));
    ASSERT_EQ(edition, std::wstring(L"Console Edition (Game Preview)"));

    // Deluxe Edition
    ASSERT_EQ(ExtractBaseTitle(L"007 First Light - Deluxe Edition", &edition),
              std::wstring(L"007 First Light"));
    ASSERT_EQ(edition, std::wstring(L"Deluxe Edition"));

    // Parenthesised game preview without dash
    ASSERT_EQ(ExtractBaseTitle(L"Halo Infinite (Game Preview)", &edition),
              std::wstring(L"Halo Infinite"));
    ASSERT_EQ(edition, std::wstring(L"Game Preview"));

    // No suffix -> title unchanged, edition empty
    ASSERT_EQ(ExtractBaseTitle(L"Portal 2", &edition), std::wstring(L"Portal 2"));
    ASSERT_TRUE(edition.empty());

    // Standard Edition (explicit) should be stripped
    ASSERT_EQ(ExtractBaseTitle(L"Elden Ring - Standard Edition", &edition),
              std::wstring(L"Elden Ring"));
    ASSERT_EQ(edition, std::wstring(L"Standard Edition"));
}

TEST_CASE(OwnedGames_NormalizeTitleKeyCollapsesCrossEditionDuplicates) {
    // Editions of the same game must produce the same dedup key so addOrUpdate merges them.
    ASSERT_EQ(NormalizeTitleKey(L"7 Days to Die"),
              NormalizeTitleKey(L"7 Days to Die - Console Edition (Game Preview)"));
    ASSERT_EQ(NormalizeTitleKey(L"9 Years of Shadows"),
              NormalizeTitleKey(L"9 Years of Shadows - Amazon Prime"));
    ASSERT_EQ(NormalizeTitleKey(L"007 First Light"),
              NormalizeTitleKey(L"007 First Light - Deluxe Edition"));
    // Different base titles must NOT share a key.
    ASSERT_NE(NormalizeTitleKey(L"Portal 2"), NormalizeTitleKey(L"Portal"));
}

TEST_CASE(OwnedGames_UbisoftConfigurationCacheResolvesTitles) {
    const std::string brotherhood = R"(version: 2.0
root:
  name: l1
  sort_string: "Assassin's Creed 03"
  thumb_image: 17319894db2463c9d82d789b3e46461f.jpg
  space_id: 50f97655-c930-40fd-82c7-fb33fd024fec
  crash_reporting:
    space_id: 50f97655-c930-40fd-82c7-fb33fd024fec
    app_id: e421a897-4eb7-45a7-a4dc-6aaf4d7bd8a9
  start_game:
    online:
      executables:
        - path:
            relative: ACBSP.exe
          working_directory:
            register: HKEY_LOCAL_MACHINE\SOFTWARE\Ubisoft\Assassin's Creed Brotherhood\GameUpdate\installdir
localizations:
  default:
    l1: Assassin's Creed® Brotherhood
)";

    // A config whose root name is already a real title, not a localization key.
    const std::string uno = R"(version: 2.0
root:
  name: UNO
  space_id: 3352f8a5-0c1b-4a2f-9c55-1c0e59a3f001
  installer:
    game_identifier: UNO
    publisher: Ubisoft
    help_url: http://support.ubi.com
    about_url: http://uno.ubi.com
)";

    std::string blob = "\x7f\x01garbage";
    AppendUbisoftRecord(blob, 26, brotherhood);
    AppendUbisoftRecord(blob, 3352, uno);
    // A short record (launcher metadata only) must not be reported as a game.
    AppendUbisoftRecord(blob, 999, "version: 2.0\nroot:\n  name: l1\n");

    const std::vector<UbisoftConfigEntry> entries = ParseUbisoftConfigurationCache(blob);
    ASSERT_EQ(entries.size(), size_t(2));

    ASSERT_EQ(entries[0].installId, std::wstring(L"26"));
    ASSERT_EQ(entries[0].launchId, std::wstring(L"26"));
    // The localization key resolves, and the trademark mark is stripped.
    ASSERT_EQ(entries[0].title, std::wstring(L"Assassin's Creed Brotherhood"));
    // Box art comes from the box art file name Ubisoft's own CDN serves.
    ASSERT_EQ(entries[0].thumbnailHash, std::wstring(L"17319894db2463c9d82d789b3e46461f.jpg"));
    // Root and nested crash-reporting ids both map to the owned product.
    ASSERT_EQ(entries[0].ownershipIds.size(), size_t(2));
    ASSERT_TRUE(std::find(entries[0].ownershipIds.begin(), entries[0].ownershipIds.end(),
                          std::wstring(L"50f97655-c930-40fd-82c7-fb33fd024fec")) !=
                entries[0].ownershipIds.end());
    ASSERT_TRUE(std::find(entries[0].ownershipIds.begin(), entries[0].ownershipIds.end(),
                          std::wstring(L"e421a897-4eb7-45a7-a4dc-6aaf4d7bd8a9")) !=
                entries[0].ownershipIds.end());

    ASSERT_EQ(entries[1].installId, std::wstring(L"3352"));
    ASSERT_EQ(entries[1].title, std::wstring(L"UNO"));
}

TEST_CASE(OwnedGames_UbisoftOwnershipIdsAreLowercasedAndDeduped) {
    std::string blob = "\x01\x02\x00";
    blob += "50F97655-C930-40FD-82C7-FB33FD024FEC";
    blob += "\x00\x00";
    blob += "50f97655-c930-40fd-82c7-fb33fd024fec";
    blob += "\x00";
    blob += "e421a897-4eb7-45a7-a4dc-6aaf4d7bd8a9";
    // Not a UUID: too short, must be ignored.
    blob += " 50f97655-c930-40fd-82c7";

    const std::vector<std::wstring> ids = ExtractUbisoftOwnershipIds(blob);
    ASSERT_EQ(ids.size(), size_t(2));
    ASSERT_EQ(ids[0], std::wstring(L"50f97655-c930-40fd-82c7-fb33fd024fec"));
    ASSERT_EQ(ids[1], std::wstring(L"e421a897-4eb7-45a7-a4dc-6aaf4d7bd8a9"));
}

TEST_CASE(OwnedGames_LegendaryAssetsMapToEpicOwnedGames) {
    const std::wstring assets = LR"({"Win32": [], "Windows": [
        {"app_name": "Pasta", "asset_id": "a1", "catalog_item_id": "c1", "label_name": "Live-Windows", "namespace": "pastans", "build_version": "1"},
        {"app_name": "UE4Editor", "asset_id": "a2", "catalog_item_id": "c2", "label_name": "Live-Windows", "namespace": "ue", "build_version": "1"}
    ]})";

    const std::wstring metadata = LR"({"app_name": "Pasta", "app_title": "Pasta Master",
        "metadata": {"description": "A tasty\r\nbuilder.", "releaseInfo": [{"dateAdded": "2021-05-04T00:00:00.000Z"}],
        "keyImages": [{"type": "Thumbnail", "url": "https://cdn.example/thumb.jpg"},
                      {"type": "DieselStoreFrontWide", "url": "https://cdn.example/wide.jpg"}]}})";

    const std::vector<LegendaryAssetEntry> parsedAssets = ParseLegendaryWindowsAssets(assets);
    // Unreal Engine assets are tools, not games, exactly like legendary's own list.
    ASSERT_EQ(parsedAssets.size(), size_t(1));
    ASSERT_EQ(parsedAssets[0].appName, std::wstring(L"Pasta"));
    ASSERT_EQ(parsedAssets[0].catalogItemId, std::wstring(L"c1"));

    const std::vector<OwnedGame> games =
        BuildLegendaryOwnedGames(parsedAssets, { { L"Pasta", metadata } });
    ASSERT_EQ(games.size(), size_t(1));
    ASSERT_EQ(games[0].appId, std::wstring(L"epic_Pasta"));
    ASSERT_EQ(games[0].storeKey, std::wstring(L"epic"));
    ASSERT_EQ(games[0].title, std::wstring(L"Pasta Master"));
    ASSERT_EQ(games[0].coverArtUrl, std::wstring(L"https://cdn.example/wide.jpg"));
    ASSERT_EQ(games[0].synopsis, std::wstring(L"A tasty\nbuilder."));
    ASSERT_EQ(games[0].releaseYear, 2021);
    // Installs stay with the official Epic launcher.
    ASSERT_EQ(games[0].installUri,
              std::wstring(L"com.epicgames.launcher://apps/Pasta?action=install"));
}
