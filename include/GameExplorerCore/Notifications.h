#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "GameExplorerCore/GameUpdates.h"
#include "GameExplorerCore/OwnedGames.h"

namespace GameExplorer {

// What an alert is about. The catalogue bell groups and labels items by this value, and the
// Configuration page toggles each one independently.
enum class NotificationKind {
    GameUpdate,   // an installed edition has a newer build on its own storefront
    News,         // a storefront news item (Steam announcements, store blog/RSS)
    FreeGame,     // a currently free full game (GamerPower type "Game")
    Promotion,    // a deal, loot drop or beta key (GamerPower type "Loot" / "Beta")
};

// One alert. Everything the panel needs is stored in the cached feed so the bell renders without
// any further network access.
struct NotificationItem {
    std::wstring id;          // stable de-duplication key, unique across sources
    NotificationKind kind{ NotificationKind::News };
    std::wstring source;      // storefront or aggregator that published it
    std::wstring gameTitle;   // empty when the item is not tied to one owned game
    std::wstring title;
    std::wstring body;        // short plain-text summary
    std::wstring url;         // official page to open, empty when there is nothing to open
    int64_t publishedUtc{ 0 };  // Unix seconds; 0 when the source published no date
};

// User toggles, mirrored from AppSettings. Every source is separately switchable so a player can
// keep update alerts while silencing store marketing.
struct NotificationSettings {
    bool gameUpdates{ true };
    bool news{ true };
    bool steamNews{ true };
    bool gogNews{ true };
    bool epicNews{ true };
    bool xboxNews{ true };
    bool freeGames{ true };
    bool promotions{ true };
};

// The cached result of one refresh pass. The background service writes it; the studio only reads.
struct NotificationFeed {
    std::wstring generatedAt;   // ISO-8601 UTC, empty until the first successful pass
    std::vector<NotificationItem> items;
};

// Machine token used in the cached JSON. Kept stable because the file survives upgrades.
[[nodiscard]] const wchar_t* NotificationKindToken(NotificationKind kind);
[[nodiscard]] bool NotificationKindFromToken(const std::wstring& token, NotificationKind& out);

// Human label for the bell panel ("Update", "News", "Free game", "Deal & promotion").
[[nodiscard]] const wchar_t* NotificationKindLabel(NotificationKind kind);

// Per-user cache next to the rest of the user environment:
// `%USERPROFILE%\Game Explorer\notifications.json`.
[[nodiscard]] std::filesystem::path NotificationFeedPath();

// Read/unread state is kept separately so a refresh never resurrects alerts the user dismissed.
// `%USERPROFILE%\Game Explorer\notifications-seen.json`.
[[nodiscard]] std::filesystem::path NotificationSeenPath();

// JSON round-trip for the shared cache. Parsing is deliberately lenient: an unreadable or
// truncated file produces an empty feed instead of failing the calling application.
[[nodiscard]] std::wstring SerializeNotificationFeed(const NotificationFeed& feed);
[[nodiscard]] bool ParseNotificationFeed(const std::wstring& json, NotificationFeed& out);
[[nodiscard]] bool SaveNotificationFeed(const NotificationFeed& feed,
                                        const std::filesystem::path& path);
[[nodiscard]] bool LoadNotificationFeed(NotificationFeed& out, const std::filesystem::path& path);

// Seen-state helpers. The file is a flat JSON array of item ids; unknown ids are ignored, so a
// refreshed feed can add and drop items freely.
[[nodiscard]] std::vector<std::wstring> ParseSeenIds(const std::wstring& json);
[[nodiscard]] std::wstring SerializeSeenIds(const std::vector<std::wstring>& ids);
[[nodiscard]] bool LoadSeenIds(std::vector<std::wstring>& out, const std::filesystem::path& path);
[[nodiscard]] bool SaveSeenIds(const std::vector<std::wstring>& ids,
                               const std::filesystem::path& path);
[[nodiscard]] bool IsNotificationSeen(const std::vector<std::wstring>& seen,
                                      const std::wstring& id);

// True when the item's kind is enabled by the user's toggles.
[[nodiscard]] bool NotificationKindEnabled(NotificationKind kind,
                                           const NotificationSettings& settings);

struct AppSettings;
// Copies the Configuration page's notification toggles into the feed settings.
[[nodiscard]] NotificationSettings NotificationSettingsFrom(const AppSettings& settings);

// --- Offline parsers (pure, unit-tested) --------------------------------------

// GamerPower `GET https://www.gamerpower.com/api/giveaways?platform=pc` payload. Items are mapped
// by their own `type` field: "Game" becomes FreeGame, "Loot" and "Beta" become Promotion. Only
// currently active giveaways are kept.
[[nodiscard]] std::vector<NotificationItem> ParseGamerPowerGiveaways(const std::wstring& json);

// Steam `ISteamNews/GetNewsForApp/v2/?appid=..&feeds=steam_community_announcements` payload.
// `gameTitle` is attached to every item so the panel can say which game posted it.
[[nodiscard]] std::vector<NotificationItem> ParseSteamNewsItems(const std::wstring& json,
                                                                const std::wstring& gameTitle,
                                                                const std::wstring& appId);

// RSS 2.0 news feed (GOG.com news, Xbox Wire). `source` labels the storefront.
[[nodiscard]] std::vector<NotificationItem> ParseRssNewsItems(const std::wstring& xml,
                                                              const std::wstring& source,
                                                              size_t limit = 6);

// Epic Games Store blog payload (`https://store-content.ak.epicgames.com/api/en-US/content/blog`).
[[nodiscard]] std::vector<NotificationItem> ParseEpicBlogPosts(const std::wstring& json,
                                                               size_t limit = 6);

// --- Feed construction --------------------------------------------------------

// One alert per installed edition whose storefront reported a newer build. The feed reuses the
// cached report so the bell and the details pane always agree.
void AppendUpdateNotifications(const GameUpdateReport& report,
                               const std::vector<OwnedGame>& games,
                               std::vector<NotificationItem>& items);

// Drops disabled kinds, removes duplicate ids and sorts newest-first. Shared by every producer so
// the panel ordering never depends on which network call finished first.
[[nodiscard]] NotificationFeed BuildNotificationFeed(std::vector<NotificationItem> items,
                                                     const NotificationSettings& settings);

// Performs one full refresh: the Steam update pass, per-game Steam announcements, the GOG/Epic/
// Xbox news feeds and GamerPower, then returns the merged feed. Network failures are non-fatal and
// simply leave that source out. `error` receives the first transport problem, if any.
[[nodiscard]] NotificationFeed CollectNotifications(const std::vector<OwnedGame>& games,
                                                    const NotificationSettings& settings,
                                                    GameUpdateReport* updates = nullptr,
                                                    std::wstring* error = nullptr);

// Official feeds the refresh reads. Exposed so the Configuration page can document its sources.
inline constexpr wchar_t kGamerPowerGiveawaysUrl[] =
    L"https://www.gamerpower.com/api/giveaways?platform=pc";
inline constexpr wchar_t kGamerPowerHomeUrl[] = L"https://www.gamerpower.com";
inline constexpr wchar_t kGogNewsFeedUrl[] = L"https://www.gog.com/news/feed";
inline constexpr wchar_t kEpicBlogUrl[] =
    L"https://store-content.ak.epicgames.com/api/en-US/content/blog";
inline constexpr wchar_t kXboxNewsFeedUrl[] = L"https://news.xbox.com/feed/";

} // namespace GameExplorer
