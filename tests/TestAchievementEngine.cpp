#include "TestHarness.h"
#include "GameExplorerCore/AchievementEngine.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include <filesystem>

namespace fs = std::filesystem;

class MockCustomAchievementProvider : public GameExplorer::IAchievementProvider {
public:
    [[nodiscard]] GameExplorer::PlatformType GetPlatform() const override {
        return GameExplorer::PlatformType::CustomExecutable;
    }

    [[nodiscard]] bool SupportsGame(const GameExplorer::GameItem& game) const override {
        return (game.platform == GameExplorer::PlatformType::CustomExecutable && game.appId == L"custom_mock_rpg");
    }

    [[nodiscard]] std::vector<GameExplorer::AchievementItem> FetchAchievements(const GameExplorer::GameItem& game) override {
        std::vector<GameExplorer::AchievementItem> items;

        GameExplorer::AchievementItem a1;
        a1.id = L"MOCK_HERO_1";
        a1.gameAppId = game.appId;
        a1.title = L"Hero Awakens";
        a1.description = L"Awaken from your slumber.";
        a1.isUnlocked = true;
        a1.unlockTimestamp = 1700000000ULL;
        a1.rarityPercentage = 99.0f;
        items.push_back(a1);

        GameExplorer::AchievementItem a2;
        a2.id = L"MOCK_HERO_2";
        a2.gameAppId = game.appId;
        a2.title = L"Defeat the Shadow";
        a2.description = L"Conquer the darkness.";
        a2.isUnlocked = false;
        a2.unlockTimestamp = 0ULL;
        a2.rarityPercentage = 15.5f;
        items.push_back(a2);

        return items;
    }
};

TEST_CASE(AchievementEngineProviderDispatchAndCaching) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_AchEngine.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));

    auto& engine = GameExplorer::AchievementEngine::Instance();
    engine.RegisterProvider(std::make_unique<MockCustomAchievementProvider>());

    GameExplorer::GameItem game;
    game.appId = L"custom_mock_rpg";
    game.title = L"Mock RPG Chronicles";
    game.platform = GameExplorer::PlatformType::CustomExecutable;

    // First fetch: should call mock provider and upsert into DB
    auto achievements = engine.GetAchievements(game, false);
    ASSERT_EQ(achievements.size(), static_cast<size_t>(2));
    ASSERT_EQ(achievements[0].id, L"MOCK_HERO_1");
    ASSERT_TRUE(achievements[0].isUnlocked);

    // Check progress
    auto progress = engine.GetProgress(game.appId);
    ASSERT_EQ(progress.totalCount, 2);
    ASSERT_EQ(progress.unlockedCount, 1);
    ASSERT_TRUE(progress.completionPercentage >= 49.9f && progress.completionPercentage <= 50.1f);

    // Second fetch without force: should come directly from database
    auto cached = engine.GetAchievements(game, false);
    ASSERT_EQ(cached.size(), static_cast<size_t>(2));

    // Verify async refresh
    auto future = engine.RefreshGameAsync(game);
    auto asyncResults = future.get();
    ASSERT_EQ(asyncResults.size(), static_cast<size_t>(2));

    db.Close();
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}
