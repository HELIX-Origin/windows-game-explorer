#include "TestHarness.h"
#include "GameExplorerCore/UpdateChecker.h"
#include "GameExplorerCore/UpdateStrings.h"

TEST_CASE(UpdateChecker_SemVerComparison) {
    using GameExplorer::UpdateChecker;

    // Equal versions
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"2.5.0", L"2.5.0"), 0);
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"v2.5.0", L"2.5.0"), 0);
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"2.5.0", L"v2.5.0"), 0);
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"v2.5.0", L"v2.5.0"), 0);

    // Minor upgrade
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"2.6.0", L"2.5.0"), 1);
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"2.5.0", L"2.6.0"), -1);
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"v2.6.0", L"v2.5.0"), 1);

    // Patch upgrade
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"2.5.1", L"2.5.0"), 1);
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"2.5.0", L"2.5.1"), -1);

    // Major upgrade
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"3.0.0", L"2.9.9"), 1);
    ASSERT_EQ(UpdateChecker::CompareSemVer(L"2.9.9", L"3.0.0"), -1);

    // IsNewerVersion helper
    ASSERT_TRUE(UpdateChecker::IsNewerVersion(L"v2.6.0", L"v2.5.0"));
    ASSERT_TRUE(UpdateChecker::IsNewerVersion(L"v2.5.1", L"v2.5.0"));
    ASSERT_FALSE(UpdateChecker::IsNewerVersion(L"v2.5.0", L"v2.5.0"));
    ASSERT_FALSE(UpdateChecker::IsNewerVersion(L"v2.4.9", L"v2.5.0"));
}

TEST_CASE(UpdateChecker_ParseReleaseJsonWithValidAsset) {
    using GameExplorer::UpdateChecker;

    const std::string mockJson = R"({
        "tag_name": "v2.6.0",
        "name": "Windows Game Explorer v2.6.0",
        "body": "Major release with update checker and view improvements.",
        "html_url": "https://github.com/HELIX-Origin/windows-game-explorer/releases/tag/v2.6.0",
        "assets": [
            {
                "name": "windows-game-explorer-v2.6.0-win64.zip",
                "browser_download_url": "https://github.com/HELIX-Origin/windows-game-explorer/releases/download/v2.6.0/windows-game-explorer-v2.6.0-win64.zip",
                "size": 1500000
            },
            {
                "name": "windows-game-explorer-v2.6.0-setup.exe",
                "browser_download_url": "https://github.com/HELIX-Origin/windows-game-explorer/releases/download/v2.6.0/windows-game-explorer-v2.6.0-setup.exe",
                "size": 1850000
            }
        ]
    })";

    const auto info = UpdateChecker::ParseReleaseJson(mockJson, L"2.5.0");
    ASSERT_TRUE(info.updateAvailable);
    ASSERT_EQ(info.latestVersion, L"2.6.0");
    ASSERT_EQ(info.currentVersion, L"2.5.0");
    ASSERT_EQ(info.releaseTitle, L"Windows Game Explorer v2.6.0");
    ASSERT_EQ(info.releaseUrl, L"https://github.com/HELIX-Origin/windows-game-explorer/releases/tag/v2.6.0");
    ASSERT_EQ(info.downloadUrl, L"https://github.com/HELIX-Origin/windows-game-explorer/releases/download/v2.6.0/windows-game-explorer-v2.6.0-setup.exe");
    ASSERT_EQ(info.assetSizeBytes, 1850000ULL);
    ASSERT_TRUE(info.error.empty());
}

TEST_CASE(UpdateChecker_ParseReleaseJsonWhenUpToDate) {
    using GameExplorer::UpdateChecker;

    const std::string mockJson = R"({
        "tag_name": "v2.5.0",
        "name": "Windows Game Explorer v2.5.0",
        "body": "Current version.",
        "html_url": "https://github.com/HELIX-Origin/windows-game-explorer/releases/tag/v2.5.0",
        "assets": [
            {
                "name": "windows-game-explorer-v2.5.0-setup.exe",
                "browser_download_url": "https://github.com/HELIX-Origin/windows-game-explorer/releases/download/v2.5.0/windows-game-explorer-v2.5.0-setup.exe",
                "size": 1800000
            }
        ]
    })";

    const auto info = UpdateChecker::ParseReleaseJson(mockJson, L"2.5.0");
    ASSERT_FALSE(info.updateAvailable);
    ASSERT_EQ(info.latestVersion, L"2.5.0");
    ASSERT_EQ(info.currentVersion, L"2.5.0");
    ASSERT_TRUE(info.error.empty());
}

TEST_CASE(UpdateChecker_ParseReleaseJsonMalformed) {
    using GameExplorer::UpdateChecker;

    const std::string badJson = R"({ "message": "Not Found" })";
    const auto info = UpdateChecker::ParseReleaseJson(badJson, L"2.5.0");
    ASSERT_FALSE(info.updateAvailable);
    ASSERT_FALSE(info.error.empty());
}
