#pragma once

#include "Models.h"
#include <vector>
#include <optional>
#include <memory>
#include <mutex>
#include <filesystem>

struct sqlite3;

namespace GameExplorer {

class Database {
public:
    static Database& Instance();

    bool Open(const std::filesystem::path& dbPath);
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    // Schema initialization
    bool InitializeSchema();

    // Transactions & Batching
    bool BeginTransaction();
    bool CommitTransaction();
    bool RollbackTransaction();

    // Game CRUD operations
    bool UpsertGame(const GameItem& item);
    [[nodiscard]] std::vector<GameItem> GetAllGames();
    [[nodiscard]] std::optional<GameItem> GetGameById(int64_t id);
    [[nodiscard]] std::optional<GameItem> GetGameByAppId(const std::wstring& appId);
    bool DeleteGame(int64_t id);
    bool SetInstalled(int64_t id, bool installed);
    bool SetFavorite(int64_t id, bool favorite);

    // Achievement operations
    bool UpsertAchievements(const std::wstring& appId, const std::vector<AchievementItem>& achievements);
    [[nodiscard]] std::vector<AchievementItem> GetAchievementsForGame(const std::wstring& appId);
    [[nodiscard]] GameAchievementProgress GetAchievementProgress(const std::wstring& appId);
    bool DeleteAchievementsForGame(const std::wstring& appId);

    // KV settings in DB
    bool SetSetting(const std::wstring& key, const std::wstring& value);
    [[nodiscard]] std::optional<std::wstring> GetSetting(const std::wstring& key);

private:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* m_db{ nullptr };
    std::mutex m_mutex;
};

} // namespace GameExplorer
