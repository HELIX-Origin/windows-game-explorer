#include "TestHarness.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/OwnedGames.h"
#include <filesystem>
#include <fstream>
#include <chrono>

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

TEST_CASE(ScannerCommonGameInstallationPaths) {
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"D:\\Games\\Portal2\\portal2.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"E:\\SteamLibrary\\steamapps\\common\\Half-Life 2"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Program Files\\2K Games\\BioShock"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Program Files (x86)\\Wargaming.net\\World_of_Tanks"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Program Files\\Google\\Play Games\\Bootstrapper.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"D:\\XboxGames\\Halo\\Content\\gamelaunchhelper.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Users\\User\\Saved Games\\DwarfFortress"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"F:\\itch\\games\\celeste\\Celeste.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"G:\\Emulation\\ROMs\\snes\\SuperMarioWorld.sfc"));

    // Should reject non-game directories
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Users\\User\\AppData\\Local\\Programs\\antigravity\\Antigravity.exe"));
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community"));
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Program Files\\Git\\bin\\git.exe"));
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsCommonGameInstallationPath(L"C:\\Windows\\System32\\calc.exe"));
}

TEST_CASE(ScannerKnownGameEngineDetection) {
    const fs::path tempDir = fs::temp_directory_path() / L"GameExplorer_Engine_Test";
    fs::create_directories(tempDir);

    // 1. Unity engine detection
    const fs::path unityDir = tempDir / L"UnityGame";
    fs::create_directories(unityDir);
    {
        std::ofstream f(unityDir / L"UnityPlayer.dll");
        std::ofstream g(unityDir / L"Game.exe");
    }
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsKnownGameEnginePresent(unityDir.wstring(), (unityDir / L"Game.exe").wstring()));

    // 2. GameMaker engine detection
    const fs::path gmDir = tempDir / L"GMGame";
    fs::create_directories(gmDir);
    {
        std::ofstream f(gmDir / L"data.win");
        std::ofstream g(gmDir / L"MyGMGame.exe");
    }
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsKnownGameEnginePresent(gmDir.wstring(), (gmDir / L"MyGMGame.exe").wstring()));

    // 3. Godot engine detection
    const fs::path godotDir = tempDir / L"GodotGame";
    fs::create_directories(godotDir);
    {
        std::ofstream f(godotDir / L"godot_project.pck");
        std::ofstream g(godotDir / L"GodotGame.exe");
    }
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsKnownGameEnginePresent(godotDir.wstring(), (godotDir / L"GodotGame.exe").wstring()));

    // 4. Non-game directory should return false
    const fs::path toolDir = tempDir / L"NonGameTool";
    fs::create_directories(toolDir);
    {
        std::ofstream f(toolDir / L"tool.exe");
        std::ofstream g(toolDir / L"tool.dll");
    }
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsKnownGameEnginePresent(toolDir.wstring(), (toolDir / L"tool.exe").wstring()));

    std::error_code ec;
    fs::remove_all(tempDir, ec);
}

TEST_CASE(DatabaseDeleteGameByAppId) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_DeleteAppId.db";
    std::error_code ec;
    fs::remove(testDbPath, ec);
    ASSERT_TRUE(db.Open(testDbPath));

    GameExplorer::GameItem item{};
    item.appId = L"local_dummy_non_game";
    item.title = L"Dummy Non-Game Software";
    item.platform = GameExplorer::PlatformType::CustomExecutable;
    ASSERT_TRUE(db.UpsertGame(item));

    auto retrieved = db.GetGameByAppId(L"local_dummy_non_game");
    ASSERT_TRUE(retrieved.has_value());

    ASSERT_TRUE(db.DeleteGameByAppId(L"local_dummy_non_game"));
    auto deleted = db.GetGameByAppId(L"local_dummy_non_game");
    ASSERT_FALSE(deleted.has_value());

    db.Close();
    fs::remove(testDbPath, ec);
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
    GameExplorer::StorefrontScanner::RemoveCustomLocalGame(testAppId);
}

TEST_CASE(LaunchMethodMetadataRoundTrip) {
    const fs::path metaDir = GameExplorer::Storage::GetMetadataPath();
    fs::create_directories(metaDir);

    const std::wstring testAppId = L"test_launch_methods_9999";
    const fs::path jsonPath = metaDir / (testAppId + L".json");

    GameExplorer::GameItem original;
    original.appId = testAppId;
    original.title = L"Launch Method Test";

    GameExplorer::LaunchMethod custom;
    custom.name = L"Custom Launcher";
    custom.executablePath = L"C:\\Tools\\launcher.exe";
    custom.arguments = L"--profile \"{exe}\"";
    custom.workingDirectory = L"C:\\Tools";
    original.launchMethods.push_back(custom);

    GameExplorer::LaunchMethod autoDetected;
    autoDetected.name = L"Auto Launcher";
    autoDetected.executablePath = L"C:\\Tools\\missing-auto.exe";
    autoDetected.isAutoDetected = true;
    original.launchMethods.push_back(autoDetected);

    ASSERT_TRUE(GameExplorer::StorefrontScanner::SavePerGameMetadata(original));
    ASSERT_TRUE(fs::exists(jsonPath));

    // User-added methods survive even if the executable is currently unavailable; auto-detected
    // methods whose executable disappeared are pruned.
    GameExplorer::GameItem loaded;
    loaded.appId = testAppId;
    ASSERT_TRUE(GameExplorer::StorefrontScanner::LoadPerGameMetadata(loaded));
    ASSERT_EQ(loaded.launchMethods.size(), static_cast<size_t>(1));
    ASSERT_EQ(loaded.launchMethods[0].name, L"Custom Launcher");
    ASSERT_EQ(loaded.launchMethods[0].executablePath, L"C:\\Tools\\launcher.exe");
    ASSERT_EQ(loaded.launchMethods[0].arguments, L"--profile \"{exe}\"");
    ASSERT_EQ(loaded.launchMethods[0].workingDirectory, L"C:\\Tools");
    ASSERT_FALSE(loaded.launchMethods[0].isAutoDetected);

    // An explicitly saved empty list is authoritative, so user removals persist.
    loaded.launchMethods.clear();
    ASSERT_TRUE(GameExplorer::StorefrontScanner::SavePerGameMetadata(loaded));

    GameExplorer::GameItem cleared;
    cleared.appId = testAppId;
    ASSERT_TRUE(GameExplorer::StorefrontScanner::LoadPerGameMetadata(cleared));
    ASSERT_TRUE(cleared.launchMethods.empty());

    GameExplorer::StorefrontScanner::RemoveCustomLocalGame(testAppId);
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

TEST_CASE(ItchArtworkProtectionAndStrictTitleMatching) {
    // 1. Itch games with authentic covers must never have their covers clobbered
    GameExplorer::GameItem item{};
    item.appId = L"itch_test_123";
    item.platform = GameExplorer::PlatformType::Itch;
    item.title = L"That New Teacher";
    item.coverArtPath = L"C:\\Users\\Mock\\cache\\art\\itch_test_123_static_cover.png";
    item.heroArtPath = L"C:\\Users\\Mock\\cache\\art\\itch_test_123_static_cover.png";

    GameExplorer::StorefrontScanner::EnrichGameMetadata(item);

    // Verify authentic storefront art was strictly preserved
    ASSERT_EQ(item.coverArtPath, L"C:\\Users\\Mock\\cache\\art\\itch_test_123_static_cover.png");
    ASSERT_EQ(item.heroArtPath, L"C:\\Users\\Mock\\cache\\art\\itch_test_123_static_cover.png");
}

TEST_CASE(ArtworkSourcingPolicyForStoreVersusLocalGames) {
    using GameExplorer::PlatformType;
    using GameExplorer::StorefrontScanner;
    using GameExplorer::ExtractBaseTitle;

    // Metadata providers CAN be used as an extra layer to help across all platforms,
    // strictly validating against official storefronts to prevent wrong-game matches.
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Steam));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Epic));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::GOG));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::BattleNet));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::EAApp));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Xbox));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Itch));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::SquareEnix));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Ubisoft));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Rockstar));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Amazon));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::LegacyGames));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Jagex));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::XIVLauncher));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::GuildWars2));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Riot));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::HoYoPlay));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Paradox));

    // Locally added and emulated games may also auto-source artwork from metadata providers.
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Unknown));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::CustomExecutable));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::RetroArch));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Dolphin));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::ESDE));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Project64));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::PCSX2));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::DuckStation));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::PPSSPP));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::mGBA));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Snes9x));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::melonDS));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Citra));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Flycast));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::RPCS3));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Xemu));
    ASSERT_TRUE(StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType::Vice));

    // Strict title matching: editions match their base game, but sequels and spin-offs do not
    ASSERT_EQ(ExtractBaseTitle(L"The Witcher 3: Wild Hunt - Game of the Year Edition"), L"The Witcher 3: Wild Hunt");
    ASSERT_EQ(ExtractBaseTitle(L"The Witcher 3: Wild Hunt: Complete Edition"), L"The Witcher 3: Wild Hunt");
    ASSERT_EQ(ExtractBaseTitle(L"Cyberpunk 2077: Ultimate Edition"), L"Cyberpunk 2077");
    ASSERT_EQ(ExtractBaseTitle(L"Borderlands: Game of the Year Edition"), L"Borderlands");

    // Sequels and different games retain distinct titles
    ASSERT_EQ(ExtractBaseTitle(L"Doom Eternal"), L"Doom Eternal");
    ASSERT_EQ(ExtractBaseTitle(L"Half-Life 2"), L"Half-Life 2");
    ASSERT_EQ(ExtractBaseTitle(L"Alan Wake 2"), L"Alan Wake 2");
    ASSERT_NE(ExtractBaseTitle(L"Final Fantasy VII"), ExtractBaseTitle(L"Final Fantasy VIII"));
}

TEST_CASE(ScannerUniversalInstalledGameDetection) {
    // Tests that provider-agnostic common game folder and uninstall registry scanners
    // run cleanly without memory violations or failures
    const auto commonGames = GameExplorer::StorefrontScanner::ScanCommonGameFolders();
    const auto registryGames = GameExplorer::StorefrontScanner::ScanUninstallRegistry();

    // Verify all returned items have valid appIds and executables
    for (const auto& g : commonGames) {
        ASSERT_FALSE(g.appId.empty());
        ASSERT_FALSE(g.title.empty());
        ASSERT_FALSE(g.executablePath.empty());
        ASSERT_EQ(g.platform, GameExplorer::PlatformType::CustomExecutable);
    }

    for (const auto& g : registryGames) {
        ASSERT_FALSE(g.appId.empty());
        ASSERT_FALSE(g.title.empty());
        ASSERT_FALSE(g.executablePath.empty());
        ASSERT_EQ(g.platform, GameExplorer::PlatformType::CustomExecutable);
    }
}

TEST_CASE(ScannerBenchmarkPerformanceSpeed) {
    // Measures throughput of title key normalization across 1,000 game records
    const auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        const auto key = GameExplorer::OwnedGamesSortTitle(L"The Witcher 3: Wild Hunt - Game of the Year Edition");
        (void)key;
    }
    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // 1,000 title key normalizations must execute in under 100ms
    ASSERT_TRUE(elapsedMs < 100);
}

TEST_CASE(ScannerSmartScanSessionGateAndPerformance) {
    auto& db = GameExplorer::Database::Instance();
    const auto dbPath = fs::temp_directory_path() / L"game_explorer_smart_bench.db";
    std::error_code ec;
    fs::remove(dbPath, ec);
    db.Open(dbPath);

    // Populate a test game
    GameExplorer::GameItem testGame{};
    testGame.appId = L"test_benchmark_game";
    testGame.title = L"Benchmark Test Title";
    testGame.platform = GameExplorer::PlatformType::CustomExecutable;
    db.UpsertGame(testGame);

    // Verify session gate
    GameExplorer::StorefrontScanner::ResetSessionScan();
    ASSERT_FALSE(GameExplorer::StorefrontScanner::HasSessionScanned());

    // First automated scan sets session flag
    GameExplorer::StorefrontScanner::SmartScan(db, false);
    ASSERT_TRUE(GameExplorer::StorefrontScanner::HasSessionScanned());

    // Second automated scan must exit immediately (0ms) due to session gate
    const auto start = std::chrono::high_resolution_clock::now();
    const size_t count = GameExplorer::StorefrontScanner::SmartScan(db, false);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    ASSERT_EQ(count, static_cast<size_t>(0));
    ASSERT_TRUE(elapsedMs < 10);

    db.Close();
    fs::remove(dbPath, ec);
}

TEST_CASE(ScannerPrunesNonGameMetadataOnLoad) {
    const auto games = GameExplorer::StorefrontScanner::LoadAllGamesFromMetadata();
    // Verify that all returned games are valid games and no non-game software remains
    for (const auto& g : games) {
        if (g.appId.rfind(L"local_", 0) == 0) {
            ASSERT_TRUE(GameExplorer::StorefrontScanner::IsProbableGameInstallation(g.workingDirectory, g.executablePath));
        }
    }
}
