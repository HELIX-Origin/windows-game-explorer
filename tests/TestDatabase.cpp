#include "TestHarness.h"
#include "GameExplorerCore/Database.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE(DatabaseInitializationAndSchema) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_Init.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));
    ASSERT_TRUE(db.IsOpen());

    db.Close();
    ASSERT_FALSE(db.IsOpen());

    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}

TEST_CASE(DatabaseUpsertAndQueryGame) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_Games.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));

    GameExplorer::GameItem item;
    item.appId = L"steam_123456";
    item.title = L"Test Adventure RPG";
    item.platform = GameExplorer::PlatformType::Steam;
    item.executablePath = L"C:\\Games\\TestRPG\\game.exe";
    item.workingDirectory = L"C:\\Games\\TestRPG";
    item.diskSizeBytes = 1024ULL * 1024ULL * 1024ULL * 25ULL; // 25 GB
    item.developer = L"HELIX Studios";
    item.publisher = L"HELIX Publishing";
    item.releaseDate = L"2026-10-23";
    item.rating = L"95";

    ASSERT_TRUE(db.UpsertGame(item));

    const auto fetched = db.GetGameByAppId(L"steam_123456");
    ASSERT_TRUE(fetched.has_value());
    ASSERT_EQ(fetched->title, L"Test Adventure RPG");
    ASSERT_EQ(fetched->developer, L"HELIX Studios");
    ASSERT_EQ(fetched->rating, L"95");
    ASSERT_EQ(fetched->platform, GameExplorer::PlatformType::Steam);

    const auto all = db.GetAllGames();
    ASSERT_EQ(all.size(), static_cast<size_t>(1));

    db.Close();
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}


TEST_CASE(DatabaseKeyValueSettingsPersistence) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_Meta.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));

    const std::wstring testVal = L"{\"developer\":\"TestDev\",\"score\":\"88\"}";
    ASSERT_TRUE(db.SetSetting(L"steam_999", testVal));

    db.Close();
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}

TEST_CASE(DatabaseAchievementUpsertAndProgress) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_Achievements.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));

    const std::wstring testAppId = L"steam_730";
    std::vector<GameExplorer::AchievementItem> items;

    GameExplorer::AchievementItem a1;
    a1.id = L"ACH_01";
    a1.title = L"First Steps";
    a1.description = L"Complete tutorial";
    a1.iconUnlockedUrl = L"https://cdn.example.com/ach1_unlocked.png";
    a1.iconLockedUrl = L"https://cdn.example.com/ach1_locked.png";
    a1.isUnlocked = true;
    a1.unlockTimestamp = 1700000000ULL;
    a1.rarityPercentage = 85.5f;
    items.push_back(a1);

    GameExplorer::AchievementItem a2;
    a2.id = L"ACH_02";
    a2.title = L"Master Explorer";
    a2.description = L"Explore all regions";
    a2.isUnlocked = false;
    a2.unlockTimestamp = 0ULL;
    a2.rarityPercentage = 12.0f;
    items.push_back(a2);

    GameExplorer::AchievementItem a3;
    a3.id = L"ACH_03";
    a3.title = L"Hidden Secrets";
    a3.description = L"Find secret room";
    a3.isUnlocked = true;
    a3.unlockTimestamp = 1700005000ULL;
    a3.rarityPercentage = 4.2f;
    items.push_back(a3);

    GameExplorer::AchievementItem a4;
    a4.id = L"ACH_04";
    a4.title = L"Flawless Victory";
    a4.description = L"Win without taking damage";
    a4.isUnlocked = false;
    a4.unlockTimestamp = 0ULL;
    a4.rarityPercentage = 1.1f;
    items.push_back(a4);

    ASSERT_TRUE(db.UpsertAchievements(testAppId, items));

    auto retrieved = db.GetAchievementsForGame(testAppId);
    ASSERT_EQ(retrieved.size(), static_cast<size_t>(4));

    // Verify ordering: unlocked first by timestamp desc (a3 then a1), then locked
    ASSERT_EQ(retrieved[0].id, L"ACH_03");
    ASSERT_TRUE(retrieved[0].isUnlocked);
    ASSERT_EQ(retrieved[1].id, L"ACH_01");
    ASSERT_TRUE(retrieved[1].isUnlocked);

    auto progress = db.GetAchievementProgress(testAppId);
    ASSERT_EQ(progress.totalCount, 4);
    ASSERT_EQ(progress.unlockedCount, 2);
    ASSERT_TRUE(progress.completionPercentage >= 49.9f && progress.completionPercentage <= 50.1f);

    // Update ACH_02 to unlocked
    a2.isUnlocked = true;
    a2.unlockTimestamp = 1700010000ULL;
    ASSERT_TRUE(db.UpsertAchievements(testAppId, { a2 }));

    progress = db.GetAchievementProgress(testAppId);
    ASSERT_EQ(progress.totalCount, 4);
    ASSERT_EQ(progress.unlockedCount, 3);
    ASSERT_TRUE(progress.completionPercentage >= 74.9f && progress.completionPercentage <= 75.1f);

    // Delete achievements
    ASSERT_TRUE(db.DeleteAchievementsForGame(testAppId));
    progress = db.GetAchievementProgress(testAppId);
    ASSERT_EQ(progress.totalCount, 0);
    ASSERT_EQ(progress.unlockedCount, 0);

    db.Close();
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}


