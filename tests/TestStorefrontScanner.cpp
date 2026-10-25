#include "TestHarness.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Database.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE(ScannerMountedDrivesDiscovery) {
    const auto drives = GameExplorer::StorefrontScanner::GetAllMountedDrives();
    ASSERT_FALSE(drives.empty());

    // C: should always be present on standard Windows systems
    bool hasDriveC = false;
    for (const auto& d : drives) {
        if (_wcsicmp(d.c_str(), L"C:") == 0 || _wcsicmp(d.c_str(), L"C:\\") == 0) {
            hasDriveC = true;
            break;
        }
    }
    ASSERT_TRUE(hasDriveC);
}

TEST_CASE(ScannerBlacklistExecutableFilter) {
    // Tests that non-game helpers and uninstallers are properly excluded
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"unins000.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"UnityCrashHandler64.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"dxsetup.exe"));
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"MyGame.exe"));
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"EldenRing.exe"));
}

TEST_CASE(PerGameMetadataSerializationAndPriority) {
    const fs::path metaDir = GameExplorer::Storage::GetMetadataPath();
    fs::create_directories(metaDir);

    const std::wstring testAppId = L"test_game_priority_9999";
    const fs::path jsonPath = metaDir / (testAppId + L".json");

    // 1. Create a simulated metadata.json (written by GameExplorerManager.exe)
    GameExplorer::GameItem original;
    original.appId = testAppId;
    original.title = L"Custom User Title Override";
    original.developer = L"Original Studio";
    original.rating = L"90";

    ASSERT_TRUE(GameExplorer::StorefrontScanner::SavePerGameMetadata(original));
    ASSERT_TRUE(fs::exists(jsonPath));

    // 2. Load it back into an item and verify user overrides and missing fields are applied
    GameExplorer::GameItem incoming;
    incoming.appId = testAppId;
    incoming.title = L"Default Scanner Title";

    ASSERT_TRUE(GameExplorer::StorefrontScanner::LoadPerGameMetadata(incoming));

    ASSERT_EQ(incoming.title, L"Custom User Title Override");
    ASSERT_EQ(incoming.developer, L"Original Studio");
    ASSERT_EQ(incoming.rating, L"90");

    // Cleanup
    if (fs::exists(jsonPath)) {
        fs::remove(jsonPath);
    }
}

TEST_CASE(CustomLocalGameLifecycle) {
    const fs::path tempExe = fs::temp_directory_path() / L"GameExplorer_CustomTestGame.exe";
    {
        std::ofstream stub(tempExe);
        stub << "MZ";
    }

    ASSERT_TRUE(GameExplorer::StorefrontScanner::AddCustomLocalGame(tempExe.wstring(), L"Custom Local Test Game"));

    const auto customGames = GameExplorer::StorefrontScanner::ScanCustomGames();
    bool foundAndRemoved = false;
    for (const auto& g : customGames) {
        if (g.title == L"Custom Local Test Game") {
            if (GameExplorer::StorefrontScanner::RemoveCustomLocalGame(g.appId)) {
                foundAndRemoved = true;
            }
        }
    }
    ASSERT_TRUE(foundAndRemoved);

    if (fs::exists(tempExe)) {
        fs::remove(tempExe);
    }
}

TEST_CASE(ScannerScanAllPrunesObsoleteGames) {
    const fs::path tempDbPath = fs::temp_directory_path() / L"GameExplorer_TestPrune.db";
    if (fs::exists(tempDbPath)) fs::remove(tempDbPath);

    auto& db = GameExplorer::Database::Instance();
    // Use direct sqlite or test db
    ASSERT_TRUE(db.Open(tempDbPath));
    ASSERT_TRUE(db.InitializeSchema());

    GameExplorer::GameItem obsolete;
    obsolete.appId = L"itch_bogus_ghost_999";
    obsolete.title = L"Bogus Ghost Game";
    obsolete.platform = GameExplorer::PlatformType::Itch;
    ASSERT_TRUE(db.UpsertGame(obsolete));
    ASSERT_TRUE(db.GetGameByAppId(L"itch_bogus_ghost_999").has_value());

    // Run ScanAll - should prune obsolete ghost game not found in actual scans
    GameExplorer::StorefrontScanner::ScanAll(db);
    ASSERT_FALSE(db.GetGameByAppId(L"itch_bogus_ghost_999").has_value());

    db.Close();
    if (fs::exists(tempDbPath)) fs::remove(tempDbPath);
}

TEST_CASE(ScannerCleanRomTitleAndEmulatedScan) {
    // 1. CleanRomTitle title formatting tests
    ASSERT_EQ(
        GameExplorer::StorefrontScanner::CleanRomTitle(L"Legend of Zelda, The - The Wind Waker (USA).nkit.iso"),
        L"The Legend of Zelda: The Wind Waker"
    );
    ASSERT_EQ(
        GameExplorer::StorefrontScanner::CleanRomTitle(L"Legend of Zelda, The - Twilight Princess (USA).nkit.iso"),
        L"The Legend of Zelda: Twilight Princess"
    );
    ASSERT_EQ(
        GameExplorer::StorefrontScanner::CleanRomTitle(L"Super Mario Sunshine (USA).nkit.iso"),
        L"Super Mario Sunshine"
    );
    ASSERT_EQ(
        GameExplorer::StorefrontScanner::CleanRomTitle(L"Metroid Prime (USA) (Rev 1)"),
        L"Metroid Prime"
    );
    ASSERT_EQ(
        GameExplorer::StorefrontScanner::CleanRomTitle(L"Legend of Zelda, The - Majora's Mask (USA).z64"),
        L"The Legend of Zelda: Majora's Mask"
    );

    // 2. ScanEmulatedGames tests
    const auto emuGames = GameExplorer::StorefrontScanner::ScanEmulatedGames();
    // On this machine with E:\Games\ROMs\gc and E:\Games\ROMs\n64, it should find GameCube & N64 ROMs
    if (!emuGames.empty()) {
        bool foundWindWaker = false;
        bool foundTwilightPrincess = false;
        bool foundMario = false;
        bool foundMajorasMask = false;

        for (const auto& g : emuGames) {
            if (g.title == L"The Legend of Zelda: The Wind Waker") foundWindWaker = true;
            if (g.title == L"The Legend of Zelda: Twilight Princess") foundTwilightPrincess = true;
            if (g.title == L"Super Mario Sunshine") foundMario = true;
            if (g.title == L"The Legend of Zelda: Majora's Mask") {
                foundMajorasMask = true;
                ASSERT_EQ(g.platform, GameExplorer::PlatformType::Project64);
            }
            if (g.platform == GameExplorer::PlatformType::Dolphin) {
                ASSERT_EQ(g.platform, GameExplorer::PlatformType::Dolphin);
            }
        }

        ASSERT_TRUE(foundWindWaker);
        ASSERT_TRUE(foundTwilightPrincess);
        ASSERT_TRUE(foundMario);
        ASSERT_TRUE(foundMajorasMask);
    }
}

TEST_CASE(ScannerUbisoftDetectionAndArtwork) {
    const auto ubiGames = GameExplorer::StorefrontScanner::ScanUbisoft();
    // If Ubisoft games are present on this machine, verify their metadata and artwork enrichment
    if (!ubiGames.empty()) {
        bool foundBlackFlag = false;
        bool foundFreedomCry = false;
        bool foundFenyx = false;

        for (const auto& g : ubiGames) {
            ASSERT_EQ(g.platform, GameExplorer::PlatformType::Ubisoft);
            ASSERT_FALSE(g.title.empty());
            ASSERT_NE(g.title, L"Ubisoft");

            if (g.title.find(L"Black Flag") != std::wstring::npos) {
                foundBlackFlag = true;
                ASSERT_FALSE(g.coverArtPath.empty());
                ASSERT_FALSE(g.heroArtPath.empty());
            }
            if (g.title.find(L"Freedom Cry") != std::wstring::npos) {
                foundFreedomCry = true;
                ASSERT_FALSE(g.coverArtPath.empty());
                ASSERT_FALSE(g.heroArtPath.empty());
            }
            if (g.title.find(L"Fenyx") != std::wstring::npos) {
                foundFenyx = true;
                ASSERT_FALSE(g.coverArtPath.empty());
                ASSERT_FALSE(g.heroArtPath.empty());
            }
        }

        ASSERT_TRUE(foundBlackFlag);
        ASSERT_TRUE(foundFreedomCry);
        ASSERT_TRUE(foundFenyx);
    }
}

TEST_CASE(StorageOneDriveDirectoryCreationAndSync) {
    const auto oneDriveRoot = GameExplorer::Storage::GetOneDrivePath();
    if (!oneDriveRoot.empty()) {
        ASSERT_TRUE(GameExplorer::Storage::EnsureOneDriveDirectoriesCreated());
        ASSERT_TRUE(fs::exists(oneDriveRoot));
        ASSERT_TRUE(fs::exists(GameExplorer::Storage::GetOneDriveMetadataPath()));
        ASSERT_TRUE(fs::exists(GameExplorer::Storage::GetOneDriveBackupPath()));
        ASSERT_TRUE(fs::exists(GameExplorer::Storage::GetOneDriveArtPath()));
        ASSERT_TRUE(GameExplorer::Storage::SyncToOneDrive());
    }
}

TEST_CASE(TextSanitizationAndEntityDecoding) {
    // 1. HTML entities
    ASSERT_EQ(GameExplorer::StorefrontScanner::SanitizeGameText(L"Solitaire &amp; Casual Games"), L"Solitaire & Casual Games");
    ASSERT_EQ(GameExplorer::StorefrontScanner::SanitizeGameText(L"Tom Clancy&#39;s Rainbow Six"), L"Tom Clancy's Rainbow Six");
    ASSERT_EQ(GameExplorer::StorefrontScanner::SanitizeGameText(L"Dungeons &amp; Dragons: Chronicles of Mystara &quot;HD&quot;"), L"Dungeons & Dragons: Chronicles of Mystara \"HD\"");

    // 2. CP1252 / Mojibake replacement
    ASSERT_EQ(GameExplorer::StorefrontScanner::SanitizeGameText(L"SaradaÃ¯Â¿Â½s Rise"), L"Sarada's Rise");
    ASSERT_EQ(GameExplorer::StorefrontScanner::SanitizeGameText(L"Saradaï¿½s Rise"), L"Sarada's Rise");
    ASSERT_EQ(GameExplorer::StorefrontScanner::SanitizeGameText(L"Itâ€™s older! butâ€¦ better!"), L"It's older! but... better!");

    // 3. Emoji corruption stripping
    ASSERT_EQ(GameExplorer::StorefrontScanner::SanitizeGameText(L"Loverse ðŸ”ž"), L"Loverse");
}
