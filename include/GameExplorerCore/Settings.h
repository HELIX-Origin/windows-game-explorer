#pragma once

#include "Types.h"
#include <string>

namespace GameExplorer {

struct AppSettings {
    LayoutMode layoutMode{ LayoutMode::CoverArt };

    // Theme preference (0 = System, 1 = Light, 2 = Dark)
    uint32_t themeMode{ 0 };

    // Cloud sync
    bool enableOneDriveBackup{ false };

    // Universal Achievement Credentials & Settings
    std::wstring steamWebApiKey;
    std::wstring steamId64;
    std::wstring retroAchievementsUser;
    std::wstring retroAchievementsApiKey;

    // Fallback Metadata & Artwork Providers
    std::wstring steamGridDbApiKey;
    std::wstring igdbClientId;
    std::wstring igdbClientSecret;
};

class SettingsManager {
public:
    [[nodiscard]] static AppSettings Load();
    static bool Save(const AppSettings& settings);

    [[nodiscard]] static LayoutMode GetLayoutMode();
    static bool SetLayoutMode(LayoutMode mode);
};

} // namespace GameExplorer
