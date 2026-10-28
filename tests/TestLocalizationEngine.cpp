#include "TestHarness.h"
#include "GameExplorerCore/LocalizationEngine.h"
#include "GameExplorerCore/ResourceIds.h"

TEST_CASE(LocalizationEngine_StringTableLoading) {
    using GameExplorer::LocalizationEngine;

    // Verify string loading from embedded STRINGTABLE
    const std::wstring appTitle = LocalizationEngine::GetString(IDS_APP_TITLE);
    ASSERT_FALSE(appTitle.empty());
    ASSERT_EQ(appTitle, L"Windows Game Explorer");

    const std::wstring gamesFolder = LocalizationEngine::GetString(IDS_FOLDER_GAMES);
    ASSERT_EQ(gamesFolder, L"Games");

    const std::wstring playBtn = LocalizationEngine::GetString(IDS_BTN_PLAY);
    ASSERT_EQ(playBtn, L"Play Game");

    const std::wstring colTitle = LocalizationEngine::GetString(IDS_COL_TITLE);
    ASSERT_EQ(colTitle, L"Title");

    // Non-existent ID returns empty string
    const std::wstring nonExistent = LocalizationEngine::GetString(99999);
    ASSERT_TRUE(nonExistent.empty());
}

TEST_CASE(LocalizationEngine_IndirectStringFormat) {
    using GameExplorer::LocalizationEngine;

    const std::wstring indirect = LocalizationEngine::GetIndirectString(IDS_FOLDER_GAMES, L"C:\\App\\GameExplorerShell.dll");
    ASSERT_EQ(indirect, L"@C:\\App\\GameExplorerShell.dll,-102");
}
