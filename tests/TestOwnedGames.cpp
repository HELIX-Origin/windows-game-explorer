#include "TestHarness.h"

#include "GameExplorerCore/OwnedGames.h"

#include <string>
#include <vector>

using namespace GameExplorer;

namespace {

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

TEST_CASE(OwnedGames_MatchesStoreChecksBothPrimaryAndAllSources) {
    OwnedGame game;
    game.title = L"Multi-Store Game";
    game.storeKey = L"steam, xbox";
    game.storeName = L"Steam, Xbox";
    game.sources.push_back({ L"steam", L"Steam", L"123", L"steam://install/123", L"", false });
    game.sources.push_back({ L"xbox", L"Xbox", L"XYZ", L"ms-windows-store://pdp/?ProductId=XYZ", L"", false });

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

