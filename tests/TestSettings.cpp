#include "TestHarness.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Types.h"
#include "GameExplorerCore/ThemeControls.h"

#include <windows.h>

TEST_CASE(SettingsDefaultsAndLayoutModes) {
    GameExplorer::AppSettings s;
    ASSERT_EQ(s.layoutMode, GameExplorer::LayoutMode::CoverArt);
    ASSERT_FALSE(s.enableOneDriveBackup);

    // Layout mode enum values
    ASSERT_EQ(static_cast<int>(GameExplorer::LayoutMode::CoverArt), 0);
    ASSERT_EQ(static_cast<int>(GameExplorer::LayoutMode::HeroBanner), 1);
    ASSERT_EQ(static_cast<int>(GameExplorer::LayoutMode::DetailsView), 2);
}

TEST_CASE(ThemeControls_CheckAndRadioToggleStateRetention) {
    HWND hwndCheck = CreateWindowExW(0, L"BUTTON", L"Test Check", WS_POPUP | BS_OWNERDRAW,
                                     0, 0, 100, 30, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(hwndCheck != nullptr);

    GameExplorer::ThemeControls::AttachCheck(hwndCheck);
    ASSERT_TRUE(GameExplorer::ThemeControls::IsThemedCheck(hwndCheck));

    // Initially unchecked
    ASSERT_EQ(SendMessageW(hwndCheck, BM_GETCHECK, 0, 0), static_cast<LRESULT>(BST_UNCHECKED));

    // Direct BM_SETCHECK
    SendMessageW(hwndCheck, BM_SETCHECK, BST_CHECKED, 0);
    ASSERT_EQ(SendMessageW(hwndCheck, BM_GETCHECK, 0, 0), static_cast<LRESULT>(BST_CHECKED));

    // ToggleCheck helper
    GameExplorer::ThemeControls::ToggleCheck(hwndCheck);
    ASSERT_EQ(SendMessageW(hwndCheck, BM_GETCHECK, 0, 0), static_cast<LRESULT>(BST_UNCHECKED));

    GameExplorer::ThemeControls::ToggleCheck(hwndCheck);
    ASSERT_EQ(SendMessageW(hwndCheck, BM_GETCHECK, 0, 0), static_cast<LRESULT>(BST_CHECKED));

    DestroyWindow(hwndCheck);
}
