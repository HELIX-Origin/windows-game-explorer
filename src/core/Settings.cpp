#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"

namespace GameExplorer {

AppSettings SettingsManager::Load() {
    AppSettings s;
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        db.Open(Storage::GetDatabasePath());
    }

    if (auto val = db.GetSetting(L"layout_mode")) {
        s.layoutMode = StringToLayoutMode(*val);
    }
    if (auto val = db.GetSetting(L"theme_mode")) {
        try {
            s.themeMode = static_cast<uint32_t>(std::stoul(*val));
        } catch (...) {}
    }
    if (auto val = db.GetSetting(L"enable_onedrive_backup")) {
        s.enableOneDriveBackup = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"steam_api_key")) {
        s.steamWebApiKey = *val;
    }
    if (auto val = db.GetSetting(L"steam_id64")) {
        s.steamId64 = *val;
    }
    if (auto val = db.GetSetting(L"retroachievements_user")) {
        s.retroAchievementsUser = *val;
    }
    if (auto val = db.GetSetting(L"retroachievements_api_key")) {
        s.retroAchievementsApiKey = *val;
    }
    if (auto val = db.GetSetting(L"auto_sync_achievements")) {
        s.autoSyncAchievements = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"sgdb_api_key")) {
        s.steamGridDbApiKey = *val;
    }
    if (auto val = db.GetSetting(L"igdb_client_id")) {
        s.igdbClientId = *val;
    }
    if (auto val = db.GetSetting(L"igdb_client_secret")) {
        s.igdbClientSecret = *val;
    }

    // Notification toggles default to on; only an explicit stored "0" turns one off.
    auto readToggle = [&db](const wchar_t* key, bool& value) {
        if (auto val = db.GetSetting(key)) {
            value = (*val == L"1" || *val == L"true");
        }
    };
    readToggle(L"notify_game_updates", s.notifyGameUpdates);
    readToggle(L"notify_news", s.notifyNews);
    readToggle(L"notify_news_steam", s.notifySteamNews);
    readToggle(L"notify_news_gog", s.notifyGogNews);
    readToggle(L"notify_news_epic", s.notifyEpicNews);
    readToggle(L"notify_news_xbox", s.notifyXboxNews);
    readToggle(L"notify_free_games", s.notifyFreeGames);
    readToggle(L"notify_promotions", s.notifyPromotions);

    return s;
}

bool SettingsManager::Save(const AppSettings& settings) {
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        if (!db.Open(Storage::GetDatabasePath())) return false;
    }

    db.SetSetting(L"layout_mode", std::wstring(LayoutModeToString(settings.layoutMode)));
    db.SetSetting(L"theme_mode", std::to_wstring(settings.themeMode));
    db.SetSetting(L"enable_onedrive_backup", settings.enableOneDriveBackup ? L"1" : L"0");
    db.SetSetting(L"steam_api_key", settings.steamWebApiKey);
    db.SetSetting(L"steam_id64", settings.steamId64);
    db.SetSetting(L"retroachievements_user", settings.retroAchievementsUser);
    db.SetSetting(L"retroachievements_api_key", settings.retroAchievementsApiKey);
    db.SetSetting(L"auto_sync_achievements", settings.autoSyncAchievements ? L"1" : L"0");
    db.SetSetting(L"sgdb_api_key", settings.steamGridDbApiKey);
    db.SetSetting(L"igdb_client_id", settings.igdbClientId);
    db.SetSetting(L"igdb_client_secret", settings.igdbClientSecret);
    db.SetSetting(L"notify_game_updates", settings.notifyGameUpdates ? L"1" : L"0");
    db.SetSetting(L"notify_news", settings.notifyNews ? L"1" : L"0");
    db.SetSetting(L"notify_news_steam", settings.notifySteamNews ? L"1" : L"0");
    db.SetSetting(L"notify_news_gog", settings.notifyGogNews ? L"1" : L"0");
    db.SetSetting(L"notify_news_epic", settings.notifyEpicNews ? L"1" : L"0");
    db.SetSetting(L"notify_news_xbox", settings.notifyXboxNews ? L"1" : L"0");
    db.SetSetting(L"notify_free_games", settings.notifyFreeGames ? L"1" : L"0");
    db.SetSetting(L"notify_promotions", settings.notifyPromotions ? L"1" : L"0");

    return true;
}

LayoutMode SettingsManager::GetLayoutMode() {
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        db.Open(Storage::GetDatabasePath());
    }

    if (auto val = db.GetSetting(L"layout_mode")) {
        return StringToLayoutMode(*val);
    }
    return LayoutMode::CoverArt;
}

bool SettingsManager::SetLayoutMode(LayoutMode mode) {
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        if (!db.Open(Storage::GetDatabasePath())) return false;
    }

    return db.SetSetting(L"layout_mode", std::wstring(LayoutModeToString(mode)));
}

} // namespace GameExplorer
