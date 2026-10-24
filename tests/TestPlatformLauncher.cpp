#include "TestHarness.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/Types.h"

TEST_CASE(PlatformEnumToStringAndBack) {
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::Steam), L"Steam");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::Epic), L"Epic Games");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::GOG), L"GOG");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::Itch), L"itch.io");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::BattleNet), L"Battle.net");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::Ubisoft), L"Ubisoft");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::Rockstar), L"Rockstar Games");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::EAApp), L"EA App");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::Amazon), L"Amazon Games");
    ASSERT_EQ(GameExplorer::PlatformToString(GameExplorer::PlatformType::Xbox), L"Xbox");

    ASSERT_EQ(GameExplorer::StringToPlatform(L"Steam"), GameExplorer::PlatformType::Steam);
    ASSERT_EQ(GameExplorer::StringToPlatform(L"Epic Games"), GameExplorer::PlatformType::Epic);
    ASSERT_EQ(GameExplorer::StringToPlatform(L"itch.io"), GameExplorer::PlatformType::Itch);
    ASSERT_EQ(GameExplorer::StringToPlatform(L"Xbox"), GameExplorer::PlatformType::Xbox);
}

TEST_CASE(PlatformLauncherSteamPathQuery) {
    // Tests that steam path detection does not crash and queries either registry or fixed drives
    const std::wstring steamExe = GameExplorer::PlatformLauncher::GetSteamExecutablePath();
    // If Steam is installed on this dev machine, it must end with steam.exe
    if (!steamExe.empty()) {
        const std::wstring suffix = L"steam.exe";
        ASSERT_TRUE(steamExe.size() >= suffix.size());
        ASSERT_EQ(steamExe.substr(steamExe.size() - suffix.size()), suffix);
    }
}

TEST_CASE(PlatformLauncherEmulatorDiscovery) {
    // Tests emulator path discovery for Dolphin and Project64
    const std::wstring dolphinExe = GameExplorer::PlatformLauncher::GetDolphinExecutablePath();
    if (!dolphinExe.empty()) {
        ASSERT_TRUE(dolphinExe.find(L"Dolphin.exe") != std::wstring::npos);
    }
    const std::wstring pj64Exe = GameExplorer::PlatformLauncher::GetProject64ExecutablePath();
    if (!pj64Exe.empty()) {
        ASSERT_TRUE(pj64Exe.find(L"Project64.exe") != std::wstring::npos);
    }
}
