#include "TestHarness.h"

#include <string>
#include <vector>

#include "GameExplorerCore/GameUpdates.h"
#include "GameExplorerCore/Notifications.h"
#include "GameExplorerCore/OwnedGames.h"

namespace {

GameExplorer::NotificationItem MakeItem(const std::wstring& id,
                                        GameExplorer::NotificationKind kind,
                                        int64_t published) {
    GameExplorer::NotificationItem item;
    item.id = id;
    item.kind = kind;
    item.source = L"Test";
    item.title = id;
    item.publishedUtc = published;
    return item;
}

} // namespace

TEST_CASE(Notifications_KindTokensRoundTrip) {
    using GameExplorer::NotificationKind;
    const NotificationKind kinds[] = { NotificationKind::GameUpdate, NotificationKind::News,
                                       NotificationKind::FreeGame, NotificationKind::Promotion };
    for (const NotificationKind kind : kinds) {
        NotificationKind parsed{};
        ASSERT_TRUE(GameExplorer::NotificationKindFromToken(
            GameExplorer::NotificationKindToken(kind), parsed));
        ASSERT_TRUE(parsed == kind);
    }

    NotificationKind ignored{};
    ASSERT_FALSE(GameExplorer::NotificationKindFromToken(L"unknown", ignored));
}

TEST_CASE(Notifications_ParseGamerPowerMapsTypes) {
    const std::wstring json = LR"([
        {"id":2352,"title":"Fantasy World Manager","worth":"$19.99",
         "description":"<p>A free RPG &amp; simulation.</p>",
         "open_giveaway_url":"https://www.gamerpower.com/open/fantasy-world-manager",
         "published_date":"2026-10-27 10:00:00","type":"Game","platforms":"PC, Steam",
         "status":"Active"},
        {"id":2353,"title":"Loot Pack","description":"DLC",
         "open_giveaway_url":"https://www.gamerpower.com/open/loot",
         "published_date":"2026-10-26 10:00:00","type":"Loot","status":"Active"},
        {"id":2354,"title":"Beta Keys","description":"beta access",
         "open_giveaway_url":"https://www.gamerpower.com/open/beta",
         "published_date":"2026-10-25 10:00:00","type":"Beta","status":"Active"},
        {"id":2355,"title":"Expired","type":"Game","status":"Expired",
         "open_giveaway_url":"https://www.gamerpower.com/open/expired"}
    ])";

    const std::vector<GameExplorer::NotificationItem> items =
        GameExplorer::ParseGamerPowerGiveaways(json);
    ASSERT_EQ(items.size(), size_t{ 3 });

    ASSERT_EQ(items[0].kind, GameExplorer::NotificationKind::FreeGame);
    ASSERT_EQ(items[0].id, std::wstring(L"gamerpower:2352"));
    ASSERT_EQ(items[0].title, std::wstring(L"Fantasy World Manager"));
    ASSERT_EQ(items[0].body, std::wstring(L"A free RPG & simulation."));
    ASSERT_TRUE(items[0].publishedUtc > 0);

    ASSERT_EQ(items[1].kind, GameExplorer::NotificationKind::Promotion);
    ASSERT_EQ(items[2].kind, GameExplorer::NotificationKind::Promotion);
}

TEST_CASE(Notifications_ParseSteamAnnouncement) {
    const std::wstring json =
        LR"({"appnews":{"appid":620,"newsitems":[{"gid":"1844751498235199",)"
        LR"("title":"Portal 2 - Update","url":"https://store.steampowered.com/news/app/620/view/1",)"
        LR"("contents":"An update has been released: - [b]Security[/b] and exploit fixes.","date":1790707799,)"
        LR"("feedlabel":"Community Announcements","feedname":"steam_community_announcements"}]}})";

    const std::vector<GameExplorer::NotificationItem> items =
        GameExplorer::ParseSteamNewsItems(json, L"Portal 2", L"620");
    ASSERT_EQ(items.size(), size_t{ 1 });
    ASSERT_EQ(items[0].kind, GameExplorer::NotificationKind::News);
    ASSERT_EQ(items[0].source, std::wstring(L"Steam"));
    ASSERT_EQ(items[0].gameTitle, std::wstring(L"Portal 2"));
    ASSERT_EQ(items[0].id, std::wstring(L"steam:620:1844751498235199"));
    ASSERT_EQ(items[0].title, std::wstring(L"Portal 2 - Update"));
    ASSERT_EQ(items[0].body, std::wstring(L"An update has been released: - Security and exploit fixes."));
    ASSERT_EQ(items[0].publishedUtc, int64_t{ 1790707799 });
}

TEST_CASE(Notifications_ParseRssFeedStripsMarkup) {
    const std::wstring xml = LR"(<?xml version="1.0" encoding="UTF-8"?>
        <rss version="2.0"><channel><title>GOG.com news</title>
        <item>
            <title>Release: <strong>Fantasy World Manager</strong></title>
            <link>https://www.gog.com/en/news/release_fantasy_world_manager</link>
            <guid>https://www.gog.com/en/news/release_fantasy_world_manager</guid>
            <description><![CDATA[<p>An RPG &amp; sim, now available.</p>]]></description>
            <pubDate>Wed, 28 Oct 2026 07:55:02 +0200</pubDate>
        </item>
        </channel></rss>)";

    const std::vector<GameExplorer::NotificationItem> items =
        GameExplorer::ParseRssNewsItems(xml, L"GOG", 5);
    ASSERT_EQ(items.size(), size_t{ 1 });
    ASSERT_EQ(items[0].kind, GameExplorer::NotificationKind::News);
    ASSERT_EQ(items[0].source, std::wstring(L"GOG"));
    ASSERT_EQ(items[0].title, std::wstring(L"Release: Fantasy World Manager"));
    ASSERT_EQ(items[0].body, std::wstring(L"An RPG & sim, now available."));
    ASSERT_EQ(items[0].url,
              std::wstring(L"https://www.gog.com/en/news/release_fantasy_world_manager"));
    ASSERT_TRUE(items[0].publishedUtc > 0);
}

TEST_CASE(Notifications_ParseEpicBlogPost) {
    const std::wstring json =
        LR"([{"title":"Epic Games Store news","content":"<p>Hello &amp; welcome.</p>",)"
        LR"("date":"2026-10-20T10:00:00.000Z","url":"/blog/epic-news"}])";

    const std::vector<GameExplorer::NotificationItem> items =
        GameExplorer::ParseEpicBlogPosts(json, 5);
    ASSERT_EQ(items.size(), size_t{ 1 });
    ASSERT_EQ(items[0].source, std::wstring(L"Epic Games"));
    ASSERT_EQ(items[0].body, std::wstring(L"Hello & welcome."));
    ASSERT_EQ(items[0].url, std::wstring(L"https://store.epicgames.com/blog/epic-news"));
    ASSERT_TRUE(items[0].publishedUtc > 0);
}

TEST_CASE(Notifications_FeedAppliesTogglesDedupesAndSorts) {
    using GameExplorer::NotificationKind;
    std::vector<GameExplorer::NotificationItem> items;
    items.push_back(MakeItem(L"news:old", NotificationKind::News, 100));
    items.push_back(MakeItem(L"news:new", NotificationKind::News, 300));
    items.push_back(MakeItem(L"free:1", NotificationKind::FreeGame, 200));
    items.push_back(MakeItem(L"news:new", NotificationKind::News, 300));

    GameExplorer::NotificationSettings settings;
    settings.freeGames = false;

    const GameExplorer::NotificationFeed feed =
        GameExplorer::BuildNotificationFeed(items, settings);
    ASSERT_EQ(feed.items.size(), size_t{ 2 });
    ASSERT_EQ(feed.items[0].id, std::wstring(L"news:new"));
    ASSERT_EQ(feed.items[1].id, std::wstring(L"news:old"));
    ASSERT_FALSE(feed.generatedAt.empty());
}

TEST_CASE(Notifications_FeedRoundTrip) {
    GameExplorer::NotificationFeed feed;
    feed.generatedAt = L"2026-10-28T09:00:00Z";

    GameExplorer::NotificationItem item;
    item.id = L"steam:620:1";
    item.kind = GameExplorer::NotificationKind::News;
    item.source = L"Steam";
    item.gameTitle = L"Portal 2 \u2014 Deluxe";
    item.title = L"Update \"notes\"";
    item.body = L"Line one\nLine two";
    item.url = L"https://example.invalid/news";
    item.publishedUtc = 1790707799;
    feed.items.push_back(item);

    GameExplorer::NotificationFeed parsed;
    ASSERT_TRUE(GameExplorer::ParseNotificationFeed(
        GameExplorer::SerializeNotificationFeed(feed), parsed));
    ASSERT_EQ(parsed.generatedAt, feed.generatedAt);
    ASSERT_EQ(parsed.items.size(), size_t{ 1 });
    ASSERT_EQ(parsed.items[0].id, item.id);
    ASSERT_EQ(parsed.items[0].gameTitle, item.gameTitle);
    ASSERT_EQ(parsed.items[0].title, item.title);
    ASSERT_EQ(parsed.items[0].body, item.body);
    ASSERT_EQ(parsed.items[0].publishedUtc, item.publishedUtc);
}

TEST_CASE(Notifications_FeedToleratesEmptyJson) {
    GameExplorer::NotificationFeed parsed;
    ASSERT_FALSE(GameExplorer::ParseNotificationFeed(L"", parsed));
    ASSERT_TRUE(GameExplorer::ParseNotificationFeed(L"{", parsed));
    ASSERT_TRUE(parsed.items.empty());
}

TEST_CASE(Notifications_SeenIdsRoundTrip) {
    const std::vector<std::wstring> ids{ L"news:new", L"gamerpower:2352", L"quote\"id" };
    const std::vector<std::wstring> parsed =
        GameExplorer::ParseSeenIds(GameExplorer::SerializeSeenIds(ids));
    ASSERT_EQ(parsed.size(), ids.size());
    ASSERT_EQ(parsed[1], std::wstring(L"gamerpower:2352"));
    ASSERT_EQ(parsed[2], std::wstring(L"quote\"id"));
    ASSERT_TRUE(GameExplorer::IsNotificationSeen(parsed, L"news:new"));
    ASSERT_FALSE(GameExplorer::IsNotificationSeen(parsed, L"news:missing"));
}

TEST_CASE(Notifications_UpdateAlertsOnlyCoverNewerBuilds) {
    GameExplorer::OwnedGame game;
    game.title = L"Team Fortress 2";
    game.storePageUrl = L"https://store.steampowered.com/app/440";

    GameExplorer::StorefrontSource source;
    source.storeKey = L"steam";
    source.storeName = L"Steam";
    source.productId = L"440";
    source.installed = true;
    source.installedBuildId = L"100";
    game.sources.push_back(source);

    GameExplorer::GameUpdateReport report;
    GameExplorer::GameUpdateRecord newer;
    newer.storeKey = L"steam";
    newer.productId = L"440";
    newer.info.state = GameExplorer::GameUpdateState::UpdateAvailable;
    newer.info.installedBuildId = L"100";
    newer.info.latestBuildId = L"200";
    report.records.push_back(newer);

    GameExplorer::GameUpdateRecord current;
    current.storeKey = L"steam";
    current.productId = L"441";
    current.info.state = GameExplorer::GameUpdateState::UpToDate;
    report.records.push_back(current);

    std::vector<GameExplorer::NotificationItem> items;
    GameExplorer::AppendUpdateNotifications(report, { game }, items);
    ASSERT_EQ(items.size(), size_t{ 1 });
    ASSERT_EQ(items[0].kind, GameExplorer::NotificationKind::GameUpdate);
    ASSERT_EQ(items[0].gameTitle, game.title);
    ASSERT_EQ(items[0].url, game.storePageUrl);
    ASSERT_TRUE(items[0].body.find(L"200") != std::wstring::npos);

    GameExplorer::NotificationSettings settings;
    settings.gameUpdates = false;
    const GameExplorer::NotificationFeed feed =
        GameExplorer::BuildNotificationFeed(items, settings);
    ASSERT_TRUE(feed.items.empty());
}

TEST_CASE(Notifications_ParsersTolerateTruncatedPayloads) {
    ASSERT_TRUE(GameExplorer::ParseGamerPowerGiveaways(L"[").empty());
    ASSERT_TRUE(GameExplorer::ParseSteamNewsItems(L"{\"appnews\":", L"Game", L"1").empty());
    ASSERT_TRUE(GameExplorer::ParseRssNewsItems(L"<rss>", L"GOG", 5).empty());
    ASSERT_TRUE(GameExplorer::ParseEpicBlogPosts(L"[", 5).empty());
}
