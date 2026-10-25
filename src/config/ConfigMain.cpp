#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <thread>
#include <atomic>
#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Localization.h"

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

namespace fs = std::filesystem;
using namespace GameExplorer;

constexpr int TITLE_BAR_HEIGHT = 38;

enum class TitleButton { None, Min, Max, Close };
TitleButton g_hoverBtn = TitleButton::None;
TitleButton g_pressedBtn = TitleButton::None;
bool g_trackingMouse = false;
HICON g_hAppIcon = nullptr;

RECT GetPillRect(int w) {
    return RECT{ w - 96, 6, w - 8, 32 };
}

RECT GetMinButtonRect(int w) {
    RECT pill = GetPillRect(w);
    return RECT{ pill.left + 2, pill.top + 2, pill.left + 28, pill.bottom - 2 };
}

RECT GetMaxButtonRect(int w) {
    RECT pill = GetPillRect(w);
    return RECT{ pill.left + 30, pill.top + 2, pill.left + 56, pill.bottom - 2 };
}

RECT GetCloseButtonRect(int w) {
    RECT pill = GetPillRect(w);
    return RECT{ pill.left + 58, pill.top + 2, pill.left + 84, pill.bottom - 2 };
}

TitleButton HitTestTitleButtons(int x, int y, int w) {
    POINT pt{ x, y };
    RECT rClose = GetCloseButtonRect(w);
    if (PtInRect(&rClose, pt)) return TitleButton::Close;
    RECT rMax = GetMaxButtonRect(w);
    if (PtInRect(&rMax, pt)) return TitleButton::Max;
    RECT rMin = GetMinButtonRect(w);
    if (PtInRect(&rMin, pt)) return TitleButton::Min;
    return TitleButton::None;
}

constexpr int IDC_LBL_INTEGRATION_STATUS = 2001;
constexpr int IDC_BTN_OPEN_EXPLORER      = 2002;
constexpr int IDC_BTN_SCAN_LIBRARY       = 2003;
constexpr int WM_APP_SCAN_COMPLETE       = WM_APP + 50;

constexpr int IDC_RAD_LAYOUT_COVER       = 2005;
constexpr int IDC_RAD_LAYOUT_HERO        = 2006;
constexpr int IDC_RAD_LAYOUT_DETAILS     = 2007;

constexpr int IDC_BTN_LAUNCH_MANAGER     = 2011;
constexpr int IDC_BTN_OPEN_METADATA_DIR  = 2013;
constexpr int IDC_BTN_OPEN_ONEDRIVE_DIR  = 2014;
constexpr int IDC_BTN_SYNC_ONEDRIVE      = 2015;

constexpr int IDC_CHK_ONEDRIVE           = 2030;
constexpr int IDC_BTN_SAVE               = 2031;

constexpr int IDC_EDT_STEAM_KEY          = 2040;
constexpr int IDC_EDT_STEAM_ID           = 2041;
constexpr int IDC_EDT_RA_USER            = 2042;
constexpr int IDC_EDT_RA_KEY             = 2043;
constexpr int IDC_EDT_SGDB_KEY           = 2044;
constexpr int IDC_EDT_IGDB_CLIENT_ID     = 2045;
constexpr int IDC_EDT_IGDB_CLIENT_SECRET = 2046;
constexpr int IDC_BTN_RESET_CACHE        = 2047;

HWND g_hwndLblIntegrationStatus = nullptr;
HWND g_hwndBtnScanLibrary       = nullptr;
std::atomic<bool> g_isScanning{ false };

HWND g_hwndRadLayoutCover   = nullptr;
HWND g_hwndRadLayoutHero    = nullptr;
HWND g_hwndRadLayoutDetails = nullptr;

HWND g_hwndBtnLaunchManager   = nullptr;
HWND g_hwndBtnOpenMetadataDir = nullptr;
HWND g_hwndChkOneDrive        = nullptr;

HWND g_hwndEdtSteamKey          = nullptr;
HWND g_hwndEdtSteamId           = nullptr;
HWND g_hwndEdtRaUser            = nullptr;
HWND g_hwndEdtRaKey             = nullptr;
HWND g_hwndEdtSgdbKey           = nullptr;
HWND g_hwndEdtIgdbClientId      = nullptr;
HWND g_hwndEdtIgdbClientSecret  = nullptr;

HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
bool g_isDarkMode = false;
ThemePalette g_palette{};
HBRUSH g_hBgBrush = nullptr;
HBRUSH g_hCardBrush = nullptr;
HBRUSH g_hEditBrush = nullptr;

bool IsIntegrationEnabled() {
    HKEY hKey = nullptr;
    const std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_EXPLORER;
    const std::wstring nsKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\") + SZ_CLSID_GAME_EXPLORER;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, clsidKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, nsKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, clsidKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, nsKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

void UpdateIntegrationStatusUI() {
    const bool enabled = IsIntegrationEnabled();
    if (g_hwndLblIntegrationStatus != nullptr) {
        SetWindowTextW(
            g_hwndLblIntegrationStatus,
            enabled ? L"Integration Status: \u25CF Registered in File Explorer (Testing: admin 'regsvr32 /u GameExplorerShell.dll')"
                    : L"Integration Status: \u25CB Not Registered (Testing: admin 'regsvr32 GameExplorerShell.dll')"
        );
        InvalidateRect(g_hwndLblIntegrationStatus, nullptr, TRUE);
    }
}

void LaunchGameExplorerManager(HWND hwndParent) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path dir = fs::path(exePath).parent_path();
    const fs::path managerPath = dir / L"GameExplorerManager.exe";

    if (!fs::exists(managerPath)) {
        MessageBoxW(hwndParent, L"GameExplorerManager.exe not found in the application directory.", L"Windows Game Explorer", MB_OK | MB_ICONERROR);
        return;
    }

    ShellExecuteW(hwndParent, L"open", managerPath.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

void LoadConfigValues() {
    const auto s = SettingsManager::Load();

    SendMessageW(g_hwndRadLayoutCover,   BM_SETCHECK, (s.layoutMode == LayoutMode::CoverArt)    ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hwndRadLayoutHero,    BM_SETCHECK, (s.layoutMode == LayoutMode::HeroBanner)  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hwndRadLayoutDetails, BM_SETCHECK, (s.layoutMode == LayoutMode::DetailsView) ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(g_hwndChkOneDrive, BM_SETCHECK, s.enableOneDriveBackup ? BST_CHECKED : BST_UNCHECKED, 0);

    if (g_hwndEdtSteamKey != nullptr) SetWindowTextW(g_hwndEdtSteamKey, s.steamWebApiKey.c_str());
    if (g_hwndEdtSteamId != nullptr)  SetWindowTextW(g_hwndEdtSteamId, s.steamId64.c_str());
    if (g_hwndEdtRaUser != nullptr)   SetWindowTextW(g_hwndEdtRaUser, s.retroAchievementsUser.c_str());
    if (g_hwndEdtRaKey != nullptr)    SetWindowTextW(g_hwndEdtRaKey, s.retroAchievementsApiKey.c_str());
    if (g_hwndEdtSgdbKey != nullptr)  SetWindowTextW(g_hwndEdtSgdbKey, s.steamGridDbApiKey.c_str());
    if (g_hwndEdtIgdbClientId != nullptr)     SetWindowTextW(g_hwndEdtIgdbClientId, s.igdbClientId.c_str());
    if (g_hwndEdtIgdbClientSecret != nullptr) SetWindowTextW(g_hwndEdtIgdbClientSecret, s.igdbClientSecret.c_str());

    UpdateIntegrationStatusUI();
}

BOOL CALLBACK BroadcastRefreshToShellViews(HWND hwnd, LPARAM lParam) {
    wchar_t cls[128] = { 0 };
    if (GetClassNameW(hwnd, cls, 128) > 0 && (wcscmp(cls, L"GameExplorerXamlHostViewClass") == 0 || wcscmp(cls, L"GameExplorerNativeView") == 0)) {
        PostMessageW(hwnd, WM_APP + 42, 0, 0);
    }
    EnumChildWindows(hwnd, BroadcastRefreshToShellViews, lParam);
    return TRUE;
}

void ScanAndPopulateLibrary(HWND hwndParent) {
    if (g_isScanning.load()) {
        return;
    }
    g_isScanning.store(true);
    EnableWindow(g_hwndBtnScanLibrary, FALSE);
    InvalidateRect(g_hwndBtnScanLibrary, nullptr, TRUE);

    std::thread([hwndParent]() {
        auto& db = Database::Instance();
        if (!db.IsOpen()) {
            db.Open(Storage::GetDatabasePath());
        }

        const size_t count = StorefrontScanner::ScanAll(db);

        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
        EnumWindows(BroadcastRefreshToShellViews, 0);

        PostMessageW(hwndParent, WM_APP_SCAN_COMPLETE, static_cast<WPARAM>(count), 0);
    }).detach();
}

void SaveConfigValues(HWND hwndParent) {
    AppSettings s = SettingsManager::Load();

    if (SendMessageW(g_hwndRadLayoutDetails, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        s.layoutMode = LayoutMode::DetailsView;
    } else if (SendMessageW(g_hwndRadLayoutHero, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        s.layoutMode = LayoutMode::HeroBanner;
    } else {
        s.layoutMode = LayoutMode::CoverArt;
    }

    s.enableOneDriveBackup = (SendMessageW(g_hwndChkOneDrive, BM_GETCHECK, 0, 0) == BST_CHECKED);

    wchar_t buf[512] = { 0 };
    if (g_hwndEdtSteamKey != nullptr) {
        GetWindowTextW(g_hwndEdtSteamKey, buf, 512);
        s.steamWebApiKey = buf;
    }
    if (g_hwndEdtSteamId != nullptr) {
        GetWindowTextW(g_hwndEdtSteamId, buf, 512);
        s.steamId64 = buf;
    }
    if (g_hwndEdtRaUser != nullptr) {
        GetWindowTextW(g_hwndEdtRaUser, buf, 512);
        s.retroAchievementsUser = buf;
    }
    if (g_hwndEdtRaKey != nullptr) {
        GetWindowTextW(g_hwndEdtRaKey, buf, 512);
        s.retroAchievementsApiKey = buf;
    }
    if (g_hwndEdtSgdbKey != nullptr) {
        GetWindowTextW(g_hwndEdtSgdbKey, buf, 512);
        s.steamGridDbApiKey = buf;
    }
    if (g_hwndEdtIgdbClientId != nullptr) {
        GetWindowTextW(g_hwndEdtIgdbClientId, buf, 512);
        s.igdbClientId = buf;
    }
    if (g_hwndEdtIgdbClientSecret != nullptr) {
        GetWindowTextW(g_hwndEdtIgdbClientSecret, buf, 512);
        s.igdbClientSecret = buf;
    }

    if (SettingsManager::Save(s)) {
        if (s.enableOneDriveBackup) {
            Storage::SyncToOneDrive();
        }
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
        EnumWindows(BroadcastRefreshToShellViews, 0);
        MessageBoxW(hwndParent, L"Integration settings successfully saved to Windows Game Explorer.", L"Windows Game Explorer", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwndParent, L"Failed to save settings. Please ensure write access.", L"Error", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_hFontBold = CreateFontIndirectW(&ncm.lfMessageFont);

        g_hAppIcon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(100), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR
        ));

        const wchar_t* themeName = g_isDarkMode ? L"DarkMode_Explorer" : L"Explorer";

        // Card 0: File Explorer Shell Integration
        constexpr int card0Y = TITLE_BAR_HEIGHT + 12;
        g_hwndLblIntegrationStatus = CreateWindowExW(0, L"STATIC", L"Integration Status: Checking...",
            WS_CHILD | WS_VISIBLE,
            36, card0Y + 34, 540, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_INTEGRATION_STATUS)), nullptr, nullptr);
        SendMessageW(g_hwndLblIntegrationStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hBtnOpen = CreateWindowExW(0, L"BUTTON", L"Open Games in File Explorer",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            36, card0Y + 58, 260, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OPEN_EXPLORER)), nullptr, nullptr);
        SendMessageW(hBtnOpen, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndBtnScanLibrary = CreateWindowExW(0, L"BUTTON", L"Scan & Populate Library",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            310, card0Y + 58, 260, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SCAN_LIBRARY)), nullptr, nullptr);
        SendMessageW(g_hwndBtnScanLibrary, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Card 1: Default WinUI XAML Viewport Layout & Game Explorer Manager
        constexpr int card1Y = card0Y + 105 + 12;
        g_hwndRadLayoutCover = CreateWindowExW(0, L"BUTTON",
            L"Box Art Grid (~2:3 Vertical Retail Box Art Cards \u2014 Default)",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            36, card1Y + 32, 540, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RAD_LAYOUT_COVER)), nullptr, nullptr);
        SendMessageW(g_hwndRadLayoutCover, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        SetWindowTheme(g_hwndRadLayoutCover, themeName, nullptr);

        g_hwndRadLayoutHero = CreateWindowExW(0, L"BUTTON",
            L"Hero Banner Grid (~16:9 Wide Banner Cards)",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            36, card1Y + 56, 540, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RAD_LAYOUT_HERO)), nullptr, nullptr);
        SendMessageW(g_hwndRadLayoutHero, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        SetWindowTheme(g_hwndRadLayoutHero, themeName, nullptr);

        g_hwndRadLayoutDetails = CreateWindowExW(0, L"BUTTON",
            L"Details List (Structured Table with Icon, Title, Provider, Size && Rating)",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            36, card1Y + 80, 540, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RAD_LAYOUT_DETAILS)), nullptr, nullptr);
        SendMessageW(g_hwndRadLayoutDetails, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        SetWindowTheme(g_hwndRadLayoutDetails, themeName, nullptr);

        g_hwndBtnLaunchManager = CreateWindowExW(0, L"BUTTON", Localization::GetStrings().configManageMetadataBtn,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            36, card1Y + 112, 235, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_LAUNCH_MANAGER)), nullptr, nullptr);
        SendMessageW(g_hwndBtnLaunchManager, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Card 2: Cloud Sync & Local Metadata Storage
        constexpr int card2Y = card1Y + 158 + 12;
        g_hwndChkOneDrive = CreateWindowExW(0, L"BUTTON",
            L"Synchronize per-game metadata overrides to local OneDrive folder",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            36, card2Y + 34, 540, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_ONEDRIVE)), nullptr, nullptr);
        SendMessageW(g_hwndChkOneDrive, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        SetWindowTheme(g_hwndChkOneDrive, themeName, nullptr);

        g_hwndBtnOpenMetadataDir = CreateWindowExW(0, L"BUTTON", L"Open Metadata Folder",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            36, card2Y + 64, 170, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OPEN_METADATA_DIR)), nullptr, nullptr);
        SendMessageW(g_hwndBtnOpenMetadataDir, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hBtnOpenOneDrive = CreateWindowExW(0, L"BUTTON", L"Open OneDrive Folder",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            214, card2Y + 64, 175, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OPEN_ONEDRIVE_DIR)), nullptr, nullptr);
        SendMessageW(hBtnOpenOneDrive, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hBtnSyncOneDrive = CreateWindowExW(0, L"BUTTON", L"Sync to OneDrive Now",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            397, card2Y + 64, 180, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SYNC_ONEDRIVE)), nullptr, nullptr);
        SendMessageW(hBtnSyncOneDrive, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Card 3: Universal Achievement Credentials & API Keys
        constexpr int card3Y = card2Y + 106 + 12;

        HWND hLblSteamKey = CreateWindowExW(0, L"STATIC", L"Steam Web API Key:",
            WS_CHILD | WS_VISIBLE,
            36, card3Y + 34, 136, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblSteamKey, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEdtSteamKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
            176, card3Y + 32, 216, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_STEAM_KEY)), nullptr, nullptr);
        SendMessageW(g_hwndEdtSteamKey, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblSteamId = CreateWindowExW(0, L"STATIC", L"SteamID64:",
            WS_CHILD | WS_VISIBLE,
            404, card3Y + 34, 76, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblSteamId, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEdtSteamId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            484, card3Y + 32, 116, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_STEAM_ID)), nullptr, nullptr);
        SendMessageW(g_hwndEdtSteamId, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblRaUser = CreateWindowExW(0, L"STATIC", L"RetroAchievements User:",
            WS_CHILD | WS_VISIBLE,
            36, card3Y + 66, 148, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblRaUser, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEdtRaUser = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            188, card3Y + 64, 130, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_RA_USER)), nullptr, nullptr);
        SendMessageW(g_hwndEdtRaUser, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblRaKey = CreateWindowExW(0, L"STATIC", L"RetroAchievements API Key:",
            WS_CHILD | WS_VISIBLE,
            326, card3Y + 66, 166, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblRaKey, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEdtRaKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
            496, card3Y + 64, 104, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_RA_KEY)), nullptr, nullptr);
        SendMessageW(g_hwndEdtRaKey, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblAchHelp = CreateWindowExW(0, L"STATIC",
            L"Free API keys unlock global progress, rarity stats, and high-res badges across stores and emulators.",
            WS_CHILD | WS_VISIBLE,
            36, card3Y + 96, 560, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblAchHelp, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Card 4: Fallback Artwork & Metadata Providers (Optional)
        constexpr int card4Y = card3Y + 126 + 12;

        HWND hLblSgdbKey = CreateWindowExW(0, L"STATIC", L"SteamGridDB API Key:",
            WS_CHILD | WS_VISIBLE,
            36, card4Y + 34, 150, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblSgdbKey, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEdtSgdbKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
            190, card4Y + 32, 410, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_SGDB_KEY)), nullptr, nullptr);
        SendMessageW(g_hwndEdtSgdbKey, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblIgdbId = CreateWindowExW(0, L"STATIC", L"IGDB Client ID:",
            WS_CHILD | WS_VISIBLE,
            36, card4Y + 66, 116, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblIgdbId, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEdtIgdbClientId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            156, card4Y + 64, 160, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_IGDB_CLIENT_ID)), nullptr, nullptr);
        SendMessageW(g_hwndEdtIgdbClientId, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblIgdbSecret = CreateWindowExW(0, L"STATIC", L"IGDB Client Secret:",
            WS_CHILD | WS_VISIBLE,
            324, card4Y + 66, 130, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblIgdbSecret, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEdtIgdbClientSecret = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
            458, card4Y + 64, 142, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_IGDB_CLIENT_SECRET)), nullptr, nullptr);
        SendMessageW(g_hwndEdtIgdbClientSecret, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblFallbackHelp = CreateWindowExW(0, L"STATIC",
            L"Optional fallbacks when storefronts lack native box art. Keys are stored locally.",
            WS_CHILD | WS_VISIBLE,
            36, card4Y + 96, 560, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblFallbackHelp, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Save Button & Reset Cache Button
        constexpr int saveY = card4Y + 126 + 14;
        HWND hBtnSave = CreateWindowExW(0, L"BUTTON", Localization::GetStrings().configSaveAndRefreshBtn,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            20, saveY, 190, 38, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SAVE)), nullptr, nullptr);
        SendMessageW(hBtnSave, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnResetCache = CreateWindowExW(0, L"BUTTON", L"\U0001F9F9 Reset Cache",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            220, saveY, 190, 38, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_RESET_CACHE)), nullptr, nullptr);
        SendMessageW(hBtnResetCache, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        LoadConfigValues();
        return 0;
    }

    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd, WM_NCACTIVATE, wParam, -1);

    case WM_NCPAINT:
        return 0;

    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);

        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (pt.y >= 0 && pt.y < TITLE_BAR_HEIGHT) {
            RECT rcPill = GetPillRect(w);
            if (PtInRect(&rcPill, pt)) return HTCLIENT;
            return HTCAPTION;
        }

        constexpr int border = 6;
        if (pt.y >= h - border) {
            if (pt.x <= border) return HTBOTTOMLEFT;
            if (pt.x >= w - border) return HTBOTTOMRIGHT;
            return HTBOTTOM;
        }
        if (pt.x <= border) return HTLEFT;
        if (pt.x >= w - border) return HTRIGHT;

        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;

        const TitleButton newHover = HitTestTitleButtons(x, y, w);
        if (newHover != g_hoverBtn) {
            g_hoverBtn = newHover;
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);
        }

        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            g_trackingMouse = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_trackingMouse = false;
        if (g_hoverBtn != TitleButton::None) {
            g_hoverBtn = TitleButton::None;
            RECT rc;
            GetClientRect(hwnd, &rc);
            RECT rcPill = GetPillRect(rc.right - rc.left);
            InvalidateRect(hwnd, &rcPill, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;

        const TitleButton btn = HitTestTitleButtons(x, y, w);
        if (btn != TitleButton::None) {
            g_pressedBtn = btn;
            SetCapture(hwnd);
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (g_pressedBtn != TitleButton::None) {
            ReleaseCapture();
            const TitleButton clicked = g_pressedBtn;
            g_pressedBtn = TitleButton::None;

            RECT rc;
            GetClientRect(hwnd, &rc);
            const int w = rc.right - rc.left;
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);

            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (HitTestTitleButtons(x, y, w) == clicked) {
                if (clicked == TitleButton::Close) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                }
                if (clicked == TitleButton::Max) {
                    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                    return 0;
                }
                if (clicked == TitleButton::Min) {
                    ShowWindow(hwnd, SW_MINIMIZE);
                    return 0;
                }
            }
            return 0;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        const int w = rcClient.right - rcClient.left;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, rcClient.bottom);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        FillRect(memDC, &rcClient, g_hBgBrush);

        RECT rcTitleBar = { 0, 0, w, TITLE_BAR_HEIGHT };
        HBRUSH titleBrush = CreateSolidBrush(g_palette.drawerBg);
        FillRect(memDC, &rcTitleBar, titleBrush);
        DeleteObject(titleBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, g_palette.divider);
        HGDIOBJ oldPen = SelectObject(memDC, borderPen);
        MoveToEx(memDC, 0, TITLE_BAR_HEIGHT - 1, nullptr);
        LineTo(memDC, w, TITLE_BAR_HEIGHT - 1);
        SelectObject(memDC, oldPen);
        DeleteObject(borderPen);

        if (g_hAppIcon != nullptr) {
            DrawIconEx(memDC, 12, 11, g_hAppIcon, 16, 16, 0, nullptr, DI_NORMAL);
        }

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, g_palette.textPrimary);
        SelectObject(memDC, g_hFontBold);
        RECT rcText = { 36, 10, w - 110, TITLE_BAR_HEIGHT };
        DrawTextW(memDC, L"Windows Game Explorer \u2014 Integration Settings", -1, &rcText, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT rcPill = GetPillRect(w);
        HBRUSH pillBg = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(236, 236, 236));
        HPEN pillPen = CreatePen(PS_SOLID, 1, g_palette.divider);
        HGDIOBJ prevBr = SelectObject(memDC, pillBg);
        HGDIOBJ prevPen = SelectObject(memDC, pillPen);
        RoundRect(memDC, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 6, 6);
        SelectObject(memDC, prevPen);
        SelectObject(memDC, prevBr);
        DeleteObject(pillPen);
        DeleteObject(pillBg);

        const RECT rMin = GetMinButtonRect(w);
        const RECT rMax = GetMaxButtonRect(w);
        const RECT rClose = GetCloseButtonRect(w);

        if (g_hoverBtn == TitleButton::Min || g_pressedBtn == TitleButton::Min) {
            HBRUSH hBr = CreateSolidBrush(g_isDarkMode ? RGB(52, 52, 52) : RGB(220, 220, 220));
            HPEN hP = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(memDC, hBr);
            HGDIOBJ oP = SelectObject(memDC, hP);
            RoundRect(memDC, rMin.left, rMin.top, rMin.right, rMin.bottom, 4, 4);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(hP);
            DeleteObject(hBr);
        }
        {
            const int cx = (rMin.left + rMin.right) / 2;
            const int cy = (rMin.top + rMin.bottom) / 2;
            HPEN glyphPen = CreatePen(PS_SOLID, 1, g_palette.textPrimary);
            HGDIOBJ oP = SelectObject(memDC, glyphPen);
            MoveToEx(memDC, cx - 4, cy, nullptr);
            LineTo(memDC, cx + 5, cy);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }

        if (g_hoverBtn == TitleButton::Max || g_pressedBtn == TitleButton::Max) {
            HBRUSH hBr = CreateSolidBrush(g_isDarkMode ? RGB(52, 52, 52) : RGB(220, 220, 220));
            HPEN hP = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(memDC, hBr);
            HGDIOBJ oP = SelectObject(memDC, hP);
            RoundRect(memDC, rMax.left, rMax.top, rMax.right, rMax.bottom, 4, 4);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(hP);
            DeleteObject(hBr);
        }
        {
            const int cx = (rMax.left + rMax.right) / 2;
            const int cy = (rMax.top + rMax.bottom) / 2;
            HPEN glyphPen = CreatePen(PS_SOLID, 1, g_palette.textPrimary);
            HGDIOBJ oP = SelectObject(memDC, glyphPen);
            HGDIOBJ oB = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Rectangle(memDC, cx - 4, cy - 4, cx + 5, cy + 5);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }

        const bool closeHover = (g_hoverBtn == TitleButton::Close);
        const bool closePressed = (g_pressedBtn == TitleButton::Close);
        if (closeHover || closePressed) {
            HBRUSH hBr = CreateSolidBrush(closePressed ? RGB(158, 33, 21) : RGB(196, 43, 28));
            HPEN hP = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(memDC, hBr);
            HGDIOBJ oP = SelectObject(memDC, hP);
            RoundRect(memDC, rClose.left, rClose.top, rClose.right, rClose.bottom, 4, 4);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(hP);
            DeleteObject(hBr);
        }
        {
            const int cx = (rClose.left + rClose.right) / 2;
            const int cy = (rClose.top + rClose.bottom) / 2;
            COLORREF cColor = (closeHover || closePressed) ? RGB(255, 255, 255) : g_palette.textPrimary;
            HPEN glyphPen = CreatePen(PS_SOLID, 1, cColor);
            HGDIOBJ oP = SelectObject(memDC, glyphPen);
            MoveToEx(memDC, cx - 4, cy - 4, nullptr);
            LineTo(memDC, cx + 5, cy + 5);
            MoveToEx(memDC, cx + 4, cy - 4, nullptr);
            LineTo(memDC, cx - 5, cy + 5);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }

        // Section Cards (No Storefront Provider Section — Auto Storefront Discovery)
        constexpr int card0Y = TITLE_BAR_HEIGHT + 12;
        constexpr int card1Y = card0Y + 105 + 12;
        constexpr int card2Y = card1Y + 158 + 12;
        constexpr int card3Y = card2Y + 106 + 12;
        constexpr int card4Y = card3Y + 126 + 12;

        const struct CardSpec {
            int y, h;
            const wchar_t* title;
        } cards[] = {
            { card0Y, 105, L"File Explorer Shell Integration" },
            { card1Y, 158, L"Default WinUI XAML Viewport Layout & Game Explorer Manager" },
            { card2Y, 106, L"Cloud Sync & Per-Game Metadata Storage" },
            { card3Y, 126, L"Universal Multi-Source Achievement Credentials & API Keys" },
            { card4Y, 126, L"Fallback Artwork & Metadata Providers (Optional)" }
        };

        for (const auto& c : cards) {
            RECT rcCard = { 20, c.y, w - 20, c.y + c.h };
            HBRUSH cardBrush = CreateSolidBrush(g_palette.cardBg);
            HPEN cardPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oB = SelectObject(memDC, cardBrush);
            HGDIOBJ oP = SelectObject(memDC, cardPen);

            RoundRect(memDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 8, 8);

            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(cardPen);
            DeleteObject(cardBrush);

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, g_palette.textPrimary);
            SelectObject(memDC, g_hFontBold);
            RECT rcCardTitle = { 36, c.y + 10, w - 40, c.y + 28 };
            DrawTextW(memDC, c.title, -1, &rcCardTitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        }

        BitBlt(hdc, 0, 0, w, rcClient.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        auto* pDis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (pDis == nullptr) break;

        HDC hdcBtn = pDis->hDC;
        RECT rcBtn = pDis->rcItem;
        const bool isPressed = (pDis->itemState & ODS_SELECTED) != 0;

        auto drawStyledButton = [&](const wchar_t* text, bool primaryAccent, bool boldFont) {
            COLORREF btnBg;
            COLORREF txtColor;
            COLORREF borderCol;
            if (primaryAccent) {
                btnBg = isPressed ? g_palette.accentPressed : g_palette.accent;
                txtColor = g_palette.accentText;
                borderCol = isPressed ? g_palette.accentPressed : g_palette.accentHover;
            } else {
                btnBg = isPressed ? (g_isDarkMode ? RGB(65, 65, 65) : RGB(210, 210, 210))
                                  : (g_isDarkMode ? RGB(52, 52, 52) : RGB(230, 230, 230));
                txtColor = g_palette.textPrimary;
                borderCol = g_palette.cardBorder;
            }
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, txtColor);
            SelectObject(hdcBtn, boldFont ? g_hFontBold : g_hFont);
            DrawTextW(hdcBtn, text, -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        if (pDis->CtlID == IDC_BTN_OPEN_EXPLORER) {
            drawStyledButton(L"Open Games in File Explorer", false, false);
            return TRUE;
        }
        if (pDis->CtlID == IDC_BTN_SCAN_LIBRARY) {
            const bool scanning = g_isScanning.load();
            drawStyledButton(scanning ? L"Scanning..." : L"Scan & Populate Library", scanning, true);
            return TRUE;
        }
        if (pDis->CtlID == IDC_BTN_LAUNCH_MANAGER) {
            drawStyledButton(L"Open Game Explorer Manager...", true, true);
            return TRUE;
        }
        if (pDis->CtlID == IDC_BTN_OPEN_METADATA_DIR) {
            drawStyledButton(L"Open Metadata Folder", false, false);
            return TRUE;
        }
        if (pDis->CtlID == IDC_BTN_OPEN_ONEDRIVE_DIR) {
            drawStyledButton(L"Open OneDrive Folder", false, false);
            return TRUE;
        }
        if (pDis->CtlID == IDC_BTN_SYNC_ONEDRIVE) {
            drawStyledButton(L"Sync to OneDrive Now", true, true);
            return TRUE;
        }
        if (pDis->CtlID == IDC_BTN_SAVE) {
            drawStyledButton(L"Save Configuration", true, true);
            return TRUE;
        }
        if (pDis->CtlID == IDC_BTN_RESET_CACHE) {
            drawStyledButton(L"\U0001F9F9 Reset Cache", false, false);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BTN_RESET_CACHE) {
            const int ret = MessageBoxW(hwnd,
                L"Are you sure you want to completely reset and repopulate the library?\n\n"
                L"This will purge cached artwork, refresh all storefront metadata, and repopulate authentic covers and details from installed stores.",
                L"Reset & Repopulate Library",
                MB_YESNO | MB_ICONQUESTION
            );
            if (ret == IDYES) {
                HCURSOR hOldCursor = SetCursor(LoadCursor(nullptr, IDC_WAIT));
                StorefrontScanner::ResetArtworkCache();
                SetCursor(hOldCursor);
                MessageBoxW(hwnd, L"Library cache and metadata have been successfully reset and repopulated.", L"Reset & Repopulate Library", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        if (id == IDC_BTN_SAVE) {
            SaveConfigValues(hwnd);
            return 0;
        }
        if (id == IDC_BTN_OPEN_EXPLORER) {
            const std::wstring cmd = std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER;
            ShellExecuteW(hwnd, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == IDC_BTN_SCAN_LIBRARY) {
            ScanAndPopulateLibrary(hwnd);
            return 0;
        }
        if (id == IDC_BTN_LAUNCH_MANAGER) {
            LaunchGameExplorerManager(hwnd);
            return 0;
        }
        if (id == IDC_BTN_OPEN_METADATA_DIR) {
            Storage::EnsureDirectoriesCreated();
            const fs::path metaDir = Storage::GetMetadataPath();
            ShellExecuteW(hwnd, L"open", metaDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == IDC_BTN_OPEN_ONEDRIVE_DIR) {
            Storage::EnsureOneDriveDirectoriesCreated();
            const fs::path oneDriveDir = Storage::GetOneDrivePath();
            if (!oneDriveDir.empty() && fs::exists(oneDriveDir)) {
                ShellExecuteW(hwnd, L"open", oneDriveDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                MessageBoxW(hwnd, L"No local OneDrive installation detected on this system.", L"OneDrive Sync", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        if (id == IDC_BTN_SYNC_ONEDRIVE) {
            if (Storage::SyncToOneDrive()) {
                MessageBoxW(hwnd, L"Library metadata, database backups, and artwork successfully synced to OneDrive.", L"OneDrive Sync", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"Failed to sync to OneDrive. Please ensure OneDrive is running and accessible.", L"OneDrive Sync", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        break;
    }

    case WM_APP_SCAN_COMPLETE: {
        g_isScanning.store(false);
        EnableWindow(g_hwndBtnScanLibrary, TRUE);
        InvalidateRect(g_hwndBtnScanLibrary, nullptr, TRUE);
        const size_t count = static_cast<size_t>(wParam);
        std::wstring msg = L"Scan complete!\n\nDiscovered " + std::to_wstring(count) +
                           L" game(s) across installed storefronts.\nYour Windows Game Explorer library has been updated.";
        MessageBoxW(hwnd, msg.c_str(), L"Windows Game Explorer", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        auto hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, g_palette.cardBg);
        SetTextColor(hdc, g_palette.textPrimary);
        return reinterpret_cast<LRESULT>(g_hCardBrush);
    }

    case WM_CTLCOLOREDIT: {
        auto hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255));
        SetTextColor(hdc, g_palette.textPrimary);
        return reinterpret_cast<LRESULT>(g_hEditBrush);
    }

    case WM_DESTROY:
        if (g_hFont != nullptr) DeleteObject(g_hFont);
        if (g_hFontBold != nullptr) DeleteObject(g_hFontBold);
        if (g_hBgBrush != nullptr) DeleteObject(g_hBgBrush);
        if (g_hCardBrush != nullptr) DeleteObject(g_hCardBrush);
        if (g_hEditBrush != nullptr) DeleteObject(g_hEditBrush);
        if (g_hAppIcon != nullptr) DestroyIcon(g_hAppIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool IsRunningElevated() {
    BOOL isElevated = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation{};
        DWORD cbSize = sizeof(elevation);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            isElevated = elevation.TokenIsElevated != 0;
        }
        CloseHandle(hToken);
    }
    return isElevated != FALSE;
}

bool ElevateSelf(const wchar_t* argument) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = argument;
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess != nullptr) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
        }
        return true;
    }
    return false;
}

fs::path FindShellDllPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path dir = fs::path(exePath).parent_path();
    if (fs::exists(dir / L"GameExplorerShell.dll")) {
        return dir / L"GameExplorerShell.dll";
    }
    if (fs::exists(dir / L"build" / L"Release" / L"GameExplorerShell.dll")) {
        return dir / L"build" / L"Release" / L"GameExplorerShell.dll";
    }
    if (fs::exists(dir / L"Release" / L"GameExplorerShell.dll")) {
        return dir / L"Release" / L"GameExplorerShell.dll";
    }
    return {};
}

int HandleShellRegistrationCLI(bool doRegister) {
    if (!IsRunningElevated()) {
        const wchar_t* arg = doRegister ? L"--register" : L"--unregister";
        return ElevateSelf(arg) ? 0 : 1;
    }

    const fs::path dllPath = FindShellDllPath();
    if (dllPath.empty()) {
        return 2;
    }

    std::wstring params = doRegister ? L"/s \"" : L"/u /s \"";
    params += dllPath.wstring() + L"\"";

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"open";
    sei.lpFile = L"regsvr32.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess != nullptr) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*lpCmdLine*/, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view arg = argv[i];
            if (arg == L"--register" || arg == L"/register" || arg == L"-register") {
                LocalFree(argv);
                const int ret = HandleShellRegistrationCLI(true);
                CoUninitialize();
                return ret;
            }
            if (arg == L"--unregister" || arg == L"/unregister" || arg == L"-unregister") {
                LocalFree(argv);
                const int ret = HandleShellRegistrationCLI(false);
                CoUninitialize();
                return ret;
            }
            if (arg == L"--reset-cache" || arg == L"/reset-cache" || arg == L"-reset-cache") {
                LocalFree(argv);
                const int ret = StorefrontScanner::ResetArtworkCache() ? 0 : 1;
                CoUninitialize();
                return ret;
            }
        }
        LocalFree(argv);
    }
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    DarkMode::Initialize();
    g_isDarkMode = DarkMode::IsSystemDarkMode();
    g_palette = DarkMode::GetPalette(g_isDarkMode);
    g_hBgBrush = CreateSolidBrush(g_palette.canvasBg);
    g_hCardBrush = CreateSolidBrush(g_palette.cardBg);
    g_hEditBrush = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255));

    const wchar_t* CLASS_NAME = L"GameExplorerConfigWnd";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ConfigWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, Localization::GetStrings().configWindowTitle,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 820,
        nullptr, nullptr, hInstance, nullptr
    );

    if (hwnd == nullptr) return 1;

    const int roundPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPreference, sizeof(roundPreference));

    BOOL isDark = g_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDark, sizeof(isDark));

    const COLORREF noneBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &noneBorder, sizeof(noneBorder));

    MARGINS margins = { 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    DarkMode::ApplyWindowTheme(hwnd, g_isDarkMode);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
