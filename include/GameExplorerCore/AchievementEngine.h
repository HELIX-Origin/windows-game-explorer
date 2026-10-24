#pragma once

#include "Models.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <future>

namespace GameExplorer {

class IAchievementProvider {
public:
    virtual ~IAchievementProvider() = default;
    [[nodiscard]] virtual PlatformType GetPlatform() const = 0;
    [[nodiscard]] virtual bool SupportsGame(const GameItem& game) const = 0;
    [[nodiscard]] virtual std::vector<AchievementItem> FetchAchievements(const GameItem& game) = 0;
};

class AchievementEngine {
public:
    static AchievementEngine& Instance();

    void InitializeDefaultProviders();
    void RegisterProvider(std::unique_ptr<IAchievementProvider> provider);

    // Queries cached achievements from Database, or refreshes if empty / forceRefresh == true
    [[nodiscard]] std::vector<AchievementItem> GetAchievements(const GameItem& game, bool forceRefresh = false);
    
    // Quick progress lookup from database
    [[nodiscard]] GameAchievementProgress GetProgress(const std::wstring& appId);

    // Asynchronous background refresh for a single game or all installed games
    std::future<std::vector<AchievementItem>> RefreshGameAsync(const GameItem& game);
    void RefreshAllAsync(const std::vector<GameItem>& games, std::function<void(size_t completed, size_t total)> progressCallback = nullptr);

    // Downloads and caches a remote badge image locally in %USERPROFILE%/Game Explorer/cache/badges
    [[nodiscard]] std::wstring CacheBadge(const std::wstring& appId, const std::wstring& achId, const std::wstring& remoteUrl, bool isUnlocked);

private:
    AchievementEngine();
    ~AchievementEngine() = default;
    AchievementEngine(const AchievementEngine&) = delete;
    AchievementEngine& operator=(const AchievementEngine&) = delete;

    std::vector<std::unique_ptr<IAchievementProvider>> m_providers;
};

} // namespace GameExplorer
