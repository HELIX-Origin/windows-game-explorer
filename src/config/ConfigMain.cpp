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
#include "GameExplorerCore/ThemeControls.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/AchievementEngine.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Localization.h"
#include "GameExplorerCore/StudioPages.h"
#include "GameExplorerCore/UpdateChecker.h"
#include "GameExplorerCore/UpdateStrings.h"

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

// The Configuration page of GameExplorerStudio.exe. The anonymous namespace keeps every symbol
// TU-local so this page and the Library Manager page can be linked into the same executable
// without collisions.
namespace {

constexpr const wchar_t* CONFIG_WINDOW_CLASS = L"GameExplorerConfigWnd";

constexpr int TITLE_BAR_HEIGHT = 38;

// Page mode: the Studio frame owns the title bar and message loop, so the page has no title bar
// band and lays its cards out from y = 12 instead of y = TITLE_BAR_HEIGHT + 12.
bool g_pageMode = false;
int g_titleBarHeight = TITLE_BAR_HEIGHT;
bool g_resourcesReady = false;

enum class TitleButton { None, Min, Max, Close };
TitleButton g_hoverBtn = TitleButton::None;
TitleButton g_pressedBtn = TitleButton::None;
bool g_trackingMouse = false;
HICON g_hAppIcon = nullptr;

HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
bool g_isDarkMode = false;
ThemePalette g_palette{};
HBRUSH g_hBgBrush = nullptr;
HBRUSH g_hCardBrush = nullptr;
HBRUSH g_hEditBrush = nullptr;

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

LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void EnsureConfigResources() {
    if (g_resourcesReady) return;

    DarkMode::Initialize();
    g_isDarkMode = DarkMode::IsSystemDarkMode();
    g_palette = DarkMode::GetPalette(g_isDarkMode);
    g_hBgBrush = CreateSolidBrush(g_palette.canvasBg);
    g_hCardBrush = CreateSolidBrush(g_palette.cardBg);

    // Publish the palette before deriving the edit brush: FieldSurfaceColor() reads the shared
    // palette, and a brush created from the zero-initialised one is solid black, which makes every
    // field erase its background to black. The real font is installed in WM_CREATE.
    ThemeControls::SetTheme(g_palette, g_hFont, g_isDarkMode);
    g_hEditBrush = CreateSolidBrush(ThemeControls::FieldSurfaceColor());
    g_resourcesReady = true;
}

void EnsureConfigWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ConfigWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = CONFIG_WINDOW_CLASS;
    RegisterClassExW(&wc);
    registered = true;
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
constexpr int IDC_CHK_AUTO_ACHIEVEMENTS  = 2032;
constexpr int IDC_BTN_SYNC_ACHIEVEMENTS  = 2033;

constexpr int WM_APP_ACHIEVEMENT_SYNC_PROGRESS = WM_APP + 51;
constexpr int WM_APP_ACHIEVEMENT_SYNC_COMPLETE = WM_APP + 52;
constexpr int WM_APP_UPDATE_CHECK_DONE         = WM_APP + 60;
constexpr int WM_APP_UPDATE_PROGRESS           = WM_APP + 61;
constexpr int WM_APP_UPDATE_APPLY_DONE         = WM_APP + 62;

constexpr int IDC_EDT_STEAM_KEY          = 2040;
constexpr int IDC_EDT_STEAM_ID           = 2041;
constexpr int IDC_EDT_RA_USER            = 2042;
constexpr int IDC_EDT_RA_KEY             = 2043;
constexpr int IDC_EDT_SGDB_KEY           = 2044;
constexpr int IDC_EDT_IGDB_CLIENT_ID     = 2045;
constexpr int IDC_EDT_IGDB_CLIENT_SECRET = 2046;
constexpr int IDC_BTN_RESET_CACHE        = 2047;

constexpr int IDC_LBL_UPDATE_STATUS      = 2050;
constexpr int IDC_BTN_CHECK_UPDATES      = 2051;
constexpr int IDC_BTN_INSTALL_UPDATE     = 2052;
constexpr int IDC_BTN_RELEASE_NOTES      = 2053;

HWND g_hwndLblIntegrationStatus = nullptr;
HWND g_hwndBtnScanLibrary       = nullptr;
std::atomic<bool> g_isScanning{ false };

HWND g_hwndRadLayoutCover   = nullptr;
HWND g_hwndRadLayoutHero    = nullptr;
HWND g_hwndRadLayoutDetails = nullptr;

HWND g_hwndBtnLaunchManager   = nullptr;
HWND g_hwndBtnOpenMetadataDir = nullptr;
HWND g_hwndChkOneDrive        = nullptr;

HWND g_hwndChkAutoAchievements  = nullptr;
HWND g_hwndBtnSyncAchievements  = nullptr;
std::atomic<bool> g_isSyncingAchievements{ false };
std::atomic<size_t> g_achSyncCompleted{ 0 };
std::atomic<size_t> g_achSyncTotal{ 0 };

HWND g_hwndEdtSteamKey          = nullptr;
HWND g_hwndEdtSteamId           = nullptr;
HWND g_hwndEdtRaUser            = nullptr;
HWND g_hwndEdtRaKey             = nullptr;
HWND g_hwndEdtSgdbKey           = nullptr;
HWND g_hwndEdtIgdbClientId      = nullptr;
HWND g_hwndEdtIgdbClientSecret  = nullptr;

// Update checker state
GameExplorer::UpdateInfo g_lastUpdateInfo{};
HWND g_hwndLblUpdateStatus  = nullptr;
HWND g_hwndBtnCheckUpdates  = nullptr;
HWND g_hwndBtnInstallUpdate = nullptr;
HWND g_hwndBtnReleaseNotes  = nullptr;

// Controls that only need to exist for layout (no command handling) still need a stable handle
// so the page can be re-flowed whenever the Studio frame resizes it.
HWND g_hwndBtnOpenExplorer     = nullptr;
HWND g_hwndBtnOpenOneDriveDir  = nullptr;
HWND g_hwndBtnSyncOneDrive     = nullptr;
HWND g_hwndBtnSave             = nullptr;
HWND g_hwndBtnResetCache       = nullptr;
HWND g_hwndLblSteamKey         = nullptr;
HWND g_hwndLblSteamId          = nullptr;
HWND g_hwndLblRaUser           = nullptr;
HWND g_hwndLblRaKey            = nullptr;
HWND g_hwndLblAchHelp          = nullptr;
HWND g_hwndLblSgdbKey          = nullptr;
HWND g_hwndLblIgdbId           = nullptr;
HWND g_hwndLblIgdbSecret       = nullptr;
HWND g_hwndLblFallbackHelp     = nullptr;

// Card geometry is computed on every resize and shared by the layout and paint paths, so the
// rounded card panels and the controls inside them can never drift apart.
constexpr int CARD_COUNT = 6;
constexpr int CARD_HEIGHTS[CARD_COUNT] = { 100, 148, 106, 186, 122, 102 };
constexpr int CARD_GAP = 12;
constexpr int PAGE_MARGIN = 20;
constexpr int CARD_PADDING = 16;
constexpr int MIN_CARD_WIDTH = 600;

RECT g_cardRects[CARD_COUNT]{};
RECT g_saveRect{};
RECT g_resetCacheRect{};

void ComputeConfigLayout(int w) {
    const int left = PAGE_MARGIN;
    const int right = (std::max)(left + MIN_CARD_WIDTH, w - PAGE_MARGIN);

    int y = g_titleBarHeight + 12;
    for (int i = 0; i < CARD_COUNT; ++i) {
        g_cardRects[i] = RECT{ left, y, right, y + CARD_HEIGHTS[i] };
        y += CARD_HEIGHTS[i] + CARD_GAP;
    }

    g_saveRect = RECT{ left, y, left + 190, y + 38 };
    g_resetCacheRect = RECT{ left + 200, y, left + 390, y + 38 };
}

/**
 * Re-flows every control into the card geometry for the current client width. Called after the
 * controls are created and again on every WM_SIZE, so the page stretches with the Studio frame
 * instead of staying pinned to its original 640px column.
 */
void LayoutConfigControls(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    if (width <= 0) return;

    ComputeConfigLayout(width);

    auto place = [](HWND control, int x, int y, int cx, int cy) {
        if (control == nullptr || !IsWindow(control)) return;
        SetWindowPos(control, nullptr, x, y, (std::max)(1, cx), (std::max)(1, cy),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };
    auto placeRect = [&place](HWND control, const RECT& rect) {
        place(control, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    };

    const RECT card0 = g_cardRects[0];
    const RECT card1 = g_cardRects[1];
    const RECT card2 = g_cardRects[2];
    const RECT card3 = g_cardRects[3];
    const RECT card4 = g_cardRects[4];

    const int innerLeft = card0.left + CARD_PADDING;
    const int innerWidth = (card0.right - CARD_PADDING) - innerLeft;
    const int half = (innerWidth - 12) / 2;

    // Card 0: shell integration.
    place(g_hwndLblIntegrationStatus, innerLeft, card0.top + 34, innerWidth, 20);
    place(g_hwndBtnOpenExplorer, innerLeft, card0.top + 58, half, 32);
    place(g_hwndBtnScanLibrary, innerLeft + half + 12, card0.top + 58, innerWidth - half - 12, 32);

    // Card 1: layout choice + Manager entry point.
    place(g_hwndRadLayoutCover, innerLeft, card1.top + 32, innerWidth, 22);
    place(g_hwndRadLayoutHero, innerLeft, card1.top + 56, innerWidth, 22);
    place(g_hwndRadLayoutDetails, innerLeft, card1.top + 80, innerWidth, 22);
    place(g_hwndBtnLaunchManager, innerLeft, card1.top + 112, 235, 32);

    // Card 2: OneDrive sync + metadata folder actions.
    place(g_hwndChkOneDrive, innerLeft, card2.top + 34, innerWidth, 22);
    const int actionWidth = (innerWidth - 16) / 3;
    place(g_hwndBtnOpenMetadataDir, innerLeft, card2.top + 64, actionWidth, 30);
    place(g_hwndBtnOpenOneDriveDir, innerLeft + actionWidth + 8, card2.top + 64, actionWidth, 30);
    place(g_hwndBtnSyncOneDrive, innerLeft + (actionWidth + 8) * 2, card2.top + 64,
          innerWidth - (actionWidth + 8) * 2, 30);

    // Card 3: achievement credentials (two label/field rows that share the row width).
    const int fieldH = ThemeControls::kFieldHeight;
    const int row1Y = card3.top + 32;
    const int steamIdLabelW = 76;
    const int steamIdEditW = 140;
    const int steamIdEditX = innerLeft + innerWidth - steamIdEditW;
    const int steamIdLabelX = steamIdEditX - 4 - steamIdLabelW;
    const int steamKeyEditX = innerLeft + 140;
    place(g_hwndLblSteamKey, innerLeft, row1Y + 4, 136, 20);
    place(g_hwndEdtSteamKey, steamKeyEditX, row1Y, steamIdLabelX - 8 - steamKeyEditX, fieldH);
    place(g_hwndLblSteamId, steamIdLabelX, row1Y + 4, steamIdLabelW, 20);
    place(g_hwndEdtSteamId, steamIdEditX, row1Y, steamIdEditW, fieldH);

    const int row2Y = card3.top + 64;
    const int raKeyEditX = innerLeft + 460;
    place(g_hwndLblRaUser, innerLeft, row2Y + 4, 148, 20);
    place(g_hwndEdtRaUser, innerLeft + 152, row2Y, 130, fieldH);
    place(g_hwndLblRaKey, innerLeft + 290, row2Y + 4, 166, 20);
    place(g_hwndEdtRaKey, raKeyEditX, row2Y, innerLeft + innerWidth - raKeyEditX, fieldH);

    place(g_hwndLblAchHelp, innerLeft, card3.top + 98, innerWidth, 20);
    place(g_hwndChkAutoAchievements, innerLeft, card3.top + 124, innerWidth, 22);
    place(g_hwndBtnSyncAchievements, innerLeft, card3.top + 152, 260, 32);

    // Card 4: optional fallback metadata providers.
    const int sgdbEditX = innerLeft + 154;
    place(g_hwndLblSgdbKey, innerLeft, card4.top + 36, 150, 20);
    place(g_hwndEdtSgdbKey, sgdbEditX, card4.top + 32, innerLeft + innerWidth - sgdbEditX, fieldH);

    const int igdbSecretEditX = innerLeft + 424;
    place(g_hwndLblIgdbId, innerLeft, card4.top + 68, 116, 20);
    place(g_hwndEdtIgdbClientId, innerLeft + 120, card4.top + 64, 160, fieldH);
    place(g_hwndLblIgdbSecret, innerLeft + 290, card4.top + 68, 130, 20);
    place(g_hwndEdtIgdbClientSecret, igdbSecretEditX, card4.top + 64,
          innerLeft + innerWidth - igdbSecretEditX, fieldH);
    place(g_hwndLblFallbackHelp, innerLeft, card4.top + 98, innerWidth, 20);

    // Card 5: updates and releases.
    const RECT card5 = g_cardRects[5];
    place(g_hwndLblUpdateStatus, innerLeft, card5.top + 34, innerWidth, 20);
    const int updateBtnW = (innerWidth - 16) / 3;
    place(g_hwndBtnCheckUpdates, innerLeft, card5.top + 58, updateBtnW, 32);
    place(g_hwndBtnInstallUpdate, innerLeft + updateBtnW + 8, card5.top + 58, updateBtnW, 32);
    place(g_hwndBtnReleaseNotes, innerLeft + (updateBtnW + 8) * 2, card5.top + 58, innerWidth - (updateBtnW + 8) * 2, 32);

    placeRect(g_hwndBtnSave, g_saveRect);
    placeRect(g_hwndBtnResetCache, g_resetCacheRect);
}

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
    // The Manager is a sibling page of this Configuration page inside the single Studio window,
    // so "Open Game Explorer Manager" is a tab switch, not a second process.
    const HWND studio = GetAncestor(hwndParent, GA_ROOT);
    if (studio == nullptr) return;
    PostMessageW(studio, Studio::WM_SELECT_TAB,
                 static_cast<WPARAM>(Studio::Tab::Manager), 0);
}

void LoadConfigValues() {
    const auto s = SettingsManager::Load();

    SendMessageW(g_hwndRadLayoutCover,   BM_SETCHECK, (s.layoutMode == LayoutMode::CoverArt)    ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hwndRadLayoutHero,    BM_SETCHECK, (s.layoutMode == LayoutMode::HeroBanner)  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hwndRadLayoutDetails, BM_SETCHECK, (s.layoutMode == LayoutMode::DetailsView) ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(g_hwndChkOneDrive, BM_SETCHECK, s.enableOneDriveBackup ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_hwndChkAutoAchievements != nullptr) {
        SendMessageW(g_hwndChkAutoAchievements, BM_SETCHECK, s.autoSyncAchievements ? BST_CHECKED : BST_UNCHECKED, 0);
    }

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

    if (g_hwndChkAutoAchievements != nullptr) {
        s.autoSyncAchievements = (SendMessageW(g_hwndChkAutoAchievements, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

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
        if (lParam != 0) {
            g_pageMode = true;
            g_titleBarHeight = 0;
        }

        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_hFontBold = CreateFontIndirectW(&ncm.lfMessageFont);

        ThemeControls::SetTheme(g_palette, g_hFont, g_isDarkMode);

        g_hAppIcon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(100), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR
        ));

        // Controls are created once then positioned by LayoutConfigControls(), so the initial
        // layout and every resize share exactly one geometry implementation.
        auto makeStatic = [&](const wchar_t* text, int id) -> HWND {
            HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd,
                id != 0 ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr, nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
            return control;
        };
        auto makeButton = [&](const wchar_t* text, int id, DWORD style, HFONT font) -> HWND {
            HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };
        auto makeEdit = [&](int id, bool password) -> HWND {
            HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | (password ? ES_PASSWORD : 0u), 0, 0, 10, 10, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
            ThemeControls::ApplyFieldFrame(control);
            return control;
        };
        auto makeCheck = [&](const wchar_t* text, int id, DWORD style) -> HWND {
            HWND control = makeButton(text, id, style | BS_OWNERDRAW, g_hFont);
            ThemeControls::AttachCheck(control);
            return control;
        };

        // Card 0: File Explorer Shell Integration
        g_hwndLblIntegrationStatus = makeStatic(L"Integration Status: Checking...", IDC_LBL_INTEGRATION_STATUS);
        g_hwndBtnOpenExplorer = makeButton(L"Open Games in File Explorer", IDC_BTN_OPEN_EXPLORER,
                                           BS_OWNERDRAW, g_hFont);
        g_hwndBtnScanLibrary = makeButton(L"Scan & Populate Library", IDC_BTN_SCAN_LIBRARY,
                                          BS_OWNERDRAW, g_hFontBold);

        // Card 1: Default WinUI XAML Viewport Layout & Game Explorer Manager
        g_hwndRadLayoutCover = makeButton(
            L"Box Art Grid (~2:3 Vertical Retail Box Art Cards \u2014 Default)",
            IDC_RAD_LAYOUT_COVER, BS_OWNERDRAW | WS_GROUP, g_hFont);
        ThemeControls::AttachRadio(g_hwndRadLayoutCover);
        g_hwndRadLayoutHero = makeButton(
            L"Hero Banner Grid (~16:9 Wide Banner Cards)",
            IDC_RAD_LAYOUT_HERO, BS_OWNERDRAW, g_hFont);
        ThemeControls::AttachRadio(g_hwndRadLayoutHero);
        g_hwndRadLayoutDetails = makeButton(
            L"Details List (Structured Table with Icon, Title, Provider, Size && Rating)",
            IDC_RAD_LAYOUT_DETAILS, BS_OWNERDRAW, g_hFont);
        ThemeControls::AttachRadio(g_hwndRadLayoutDetails);
        g_hwndBtnLaunchManager = makeButton(Localization::GetStrings().configManageMetadataBtn,
                                            IDC_BTN_LAUNCH_MANAGER, BS_OWNERDRAW, g_hFontBold);

        // Card 2: Cloud Sync & Local Metadata Storage
        g_hwndChkOneDrive = makeCheck(L"Synchronize per-game metadata overrides to local OneDrive folder",
                                      IDC_CHK_ONEDRIVE, 0);
        g_hwndBtnOpenMetadataDir = makeButton(L"Open Metadata Folder", IDC_BTN_OPEN_METADATA_DIR,
                                              BS_OWNERDRAW, g_hFont);
        g_hwndBtnOpenOneDriveDir = makeButton(L"Open OneDrive Folder", IDC_BTN_OPEN_ONEDRIVE_DIR,
                                               BS_OWNERDRAW, g_hFont);
        g_hwndBtnSyncOneDrive = makeButton(L"Sync to OneDrive Now", IDC_BTN_SYNC_ONEDRIVE,
                                            BS_OWNERDRAW, g_hFontBold);

        // Card 3: Universal Achievement Credentials & API Keys
        g_hwndLblSteamKey = makeStatic(L"Steam Web API Key:", 0);
        g_hwndEdtSteamKey = makeEdit(IDC_EDT_STEAM_KEY, true);
        g_hwndLblSteamId = makeStatic(L"SteamID64:", 0);
        g_hwndEdtSteamId = makeEdit(IDC_EDT_STEAM_ID, false);
        g_hwndLblRaUser = makeStatic(L"RetroAchievements User:", 0);
        g_hwndEdtRaUser = makeEdit(IDC_EDT_RA_USER, false);
        g_hwndLblRaKey = makeStatic(L"RetroAchievements API Key:", 0);
        g_hwndEdtRaKey = makeEdit(IDC_EDT_RA_KEY, true);
        g_hwndLblAchHelp = makeStatic(
            L"Free API keys unlock global progress, rarity stats, and high-res badges across stores and emulators.", 0);
        g_hwndChkAutoAchievements = makeCheck(
            L"Fetch missing achievements automatically in the background", IDC_CHK_AUTO_ACHIEVEMENTS, 0);
        g_hwndBtnSyncAchievements = makeButton(L"Sync Achievements for All Games", IDC_BTN_SYNC_ACHIEVEMENTS,
                                                BS_OWNERDRAW, g_hFontBold);

        // Card 4: Fallback Artwork & Metadata Providers (Optional)
        g_hwndLblSgdbKey = makeStatic(L"SteamGridDB API Key:", 0);
        g_hwndEdtSgdbKey = makeEdit(IDC_EDT_SGDB_KEY, true);
        g_hwndLblIgdbId = makeStatic(L"IGDB Client ID:", 0);
        g_hwndEdtIgdbClientId = makeEdit(IDC_EDT_IGDB_CLIENT_ID, false);
        g_hwndLblIgdbSecret = makeStatic(L"IGDB Client Secret:", 0);
        g_hwndEdtIgdbClientSecret = makeEdit(IDC_EDT_IGDB_CLIENT_SECRET, true);
        g_hwndLblFallbackHelp = makeStatic(
            L"Optional fallbacks when storefronts lack native box art. Keys are stored locally.", 0);

        // Card 5: Application Updates & Releases
        const std::wstring initialVer = std::wstring(UpdateStrings::kCurrentVersionLabel) + L" v" +
                                        UpdateChecker::GetCurrentVersion() + L" \u2014 " + UpdateStrings::kStatusUpToDate;
        g_hwndLblUpdateStatus = makeStatic(initialVer.c_str(), IDC_LBL_UPDATE_STATUS);
        g_hwndBtnCheckUpdates = makeButton(UpdateStrings::kBtnCheckUpdates, IDC_BTN_CHECK_UPDATES,
                                           BS_OWNERDRAW, g_hFontBold);
        g_hwndBtnInstallUpdate = makeButton(UpdateStrings::kBtnInstallUpdate, IDC_BTN_INSTALL_UPDATE,
                                            BS_OWNERDRAW, g_hFontBold);
        g_hwndBtnReleaseNotes = makeButton(UpdateStrings::kBtnReleaseNotes, IDC_BTN_RELEASE_NOTES,
                                           BS_OWNERDRAW, g_hFont);
        EnableWindow(g_hwndBtnInstallUpdate, FALSE);
        EnableWindow(g_hwndBtnReleaseNotes, FALSE);

        // Save Button & Reset Cache Button
        g_hwndBtnSave = makeButton(Localization::GetStrings().configSaveAndRefreshBtn, IDC_BTN_SAVE,
                                    BS_OWNERDRAW, g_hFontBold);
        g_hwndBtnResetCache = makeButton(L"\U0001F9F9 Reset Cache", IDC_BTN_RESET_CACHE,
                                          BS_OWNERDRAW, g_hFont);

        LayoutConfigControls(hwnd);
        LoadConfigValues();
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            LayoutConfigControls(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        if (g_pageMode) break;
        // The fluid card layout keeps every control inside its card down to this size; below it
        // the fixed label/field offsets in the credential rows would start to collide.
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        if (info == nullptr) break;
        info->ptMinTrackSize.x = 640;
        info->ptMinTrackSize.y = 880;
        return 0;
    }

    case WM_NCACTIVATE:
        if (g_pageMode) break;
        return DefWindowProcW(hwnd, WM_NCACTIVATE, wParam, -1);

    case WM_NCPAINT:
        if (g_pageMode) break;
        return 0;

    case WM_NCCALCSIZE:
        if (g_pageMode) break;
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        if (g_pageMode) break;
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
        if (g_pageMode) break;
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
        if (g_pageMode) break;
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
        if (g_pageMode) break;
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
        if (g_pageMode) break;
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

        if (!g_pageMode) {
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
        HBRUSH pillBg = CreateSolidBrush(g_palette.chromePillBg);
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
            HBRUSH hBr = CreateSolidBrush(g_palette.chromeHoverBg);
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
            HBRUSH hBr = CreateSolidBrush(g_palette.chromeHoverBg);
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
            HBRUSH hBr = CreateSolidBrush(closePressed ? g_palette.chromeClosePressedBg : g_palette.chromeCloseHoverBg);
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
            COLORREF cColor = (closeHover || closePressed) ? g_palette.chromeOnCloseText : g_palette.textPrimary;
            HPEN glyphPen = CreatePen(PS_SOLID, 1, cColor);
            HGDIOBJ oP = SelectObject(memDC, glyphPen);
            MoveToEx(memDC, cx - 4, cy - 4, nullptr);
            LineTo(memDC, cx + 5, cy + 5);
            MoveToEx(memDC, cx + 4, cy - 4, nullptr);
            LineTo(memDC, cx - 5, cy + 5);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }
        } // !g_pageMode

        // Section Cards (No Storefront Provider Section — Auto Storefront Discovery)
        ComputeConfigLayout(w);

        const struct CardSpec {
            const RECT* rect;
            const wchar_t* title;
        } cards[CARD_COUNT] = {
            { &g_cardRects[0], L"File Explorer Shell Integration" },
            { &g_cardRects[1], L"Default WinUI XAML Viewport Layout & Game Explorer Manager" },
            { &g_cardRects[2], L"Cloud Sync, Game Installs & Local Storage" },
            { &g_cardRects[3], L"Universal Multi-Source Achievement Credentials & API Keys" },
            { &g_cardRects[4], L"Fallback Artwork & Metadata Providers (Optional)" },
            { &g_cardRects[5], UpdateStrings::kCardTitle }
        };

        for (const auto& c : cards) {
            RECT rcCard = *c.rect;
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
            RECT rcCardTitle = { rcCard.left + CARD_PADDING, rcCard.top + 10,
                                 rcCard.right - CARD_PADDING, rcCard.top + 28 };
            DrawTextW(memDC, c.title, -1, &rcCardTitle,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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

        if (ThemeControls::IsThemedCheck(pDis->hwndItem)) {
            ThemeControls::DrawCheck(*pDis);
            return TRUE;
        }
        if (ThemeControls::IsThemedRadio(pDis->hwndItem)) {
            ThemeControls::DrawRadio(*pDis);
            return TRUE;
        }

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
        if (pDis->CtlID == IDC_BTN_SYNC_ACHIEVEMENTS) {
            std::wstring label = L"Sync Achievements for All Games";
            const bool syncing = g_isSyncingAchievements.load();
            if (syncing) {
                label = L"Syncing achievements " + std::to_wstring(g_achSyncCompleted.load()) +
                        L"/" + std::to_wstring(g_achSyncTotal.load()) + L"...";
            }
            drawStyledButton(label.c_str(), syncing, true);
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
        const int code = HIWORD(wParam);
        auto control = reinterpret_cast<HWND>(lParam);

        // Custom-drawn toggles do not update themselves; radios also clear the rest of the group.
        if (code == BN_CLICKED) {
            if (ThemeControls::IsThemedRadio(control)) {
                ThemeControls::SelectRadio(control);
            } else if (ThemeControls::IsThemedCheck(control)) {
                ThemeControls::ToggleCheck(control);
            }
        }

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

        if (id == IDC_BTN_SYNC_ACHIEVEMENTS) {
            if (g_isSyncingAchievements.exchange(true)) {
                return 0;
            }
            g_achSyncCompleted.store(0);
            g_achSyncTotal.store(0);
            EnableWindow(g_hwndBtnSyncAchievements, FALSE);
            InvalidateRect(g_hwndBtnSyncAchievements, nullptr, TRUE);

            const HWND hwndNotify = hwnd;
            std::thread([hwndNotify]() {
                auto& db = Database::Instance();
                if (!db.IsOpen()) {
                    db.Open(Storage::GetDatabasePath());
                }
                const std::vector<GameItem> games = db.GetAllGames();
                g_achSyncTotal.store(games.size());

                if (games.empty()) {
                    PostMessageW(hwndNotify, WM_APP_ACHIEVEMENT_SYNC_COMPLETE, 0, 0);
                    return;
                }

                AchievementEngine::Instance().RefreshAllAsync(games, [hwndNotify](size_t completed, size_t total) {
                    g_achSyncCompleted.store(completed);
                    g_achSyncTotal.store(total);
                    if (IsWindow(hwndNotify)) {
                        const UINT msg = (completed >= total) ? static_cast<UINT>(WM_APP_ACHIEVEMENT_SYNC_COMPLETE)
                                                              : static_cast<UINT>(WM_APP_ACHIEVEMENT_SYNC_PROGRESS);
                        PostMessageW(hwndNotify, msg, completed, total);
                    }
                });
            }).detach();
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

        if (id == IDC_BTN_CHECK_UPDATES) {
            SetWindowTextW(g_hwndLblUpdateStatus, UpdateStrings::kStatusChecking);
            EnableWindow(g_hwndBtnCheckUpdates, FALSE);
            EnableWindow(g_hwndBtnInstallUpdate, FALSE);
            EnableWindow(g_hwndBtnReleaseNotes, FALSE);
            InvalidateRect(hwnd, nullptr, FALSE);

            UpdateChecker::CheckForUpdatesAsync([hwnd](const UpdateInfo& info) {
                PostMessageW(hwnd, WM_APP_UPDATE_CHECK_DONE, 0, reinterpret_cast<LPARAM>(new UpdateInfo(info)));
            });
            return 0;
        }

        if (id == IDC_BTN_INSTALL_UPDATE) {
            if (!g_lastUpdateInfo.updateAvailable || g_lastUpdateInfo.downloadUrl.empty()) return 0;
            SetWindowTextW(g_hwndLblUpdateStatus, UpdateStrings::kStatusDownloading);
            EnableWindow(g_hwndBtnInstallUpdate, FALSE);
            EnableWindow(g_hwndBtnCheckUpdates, FALSE);
            InvalidateRect(hwnd, nullptr, FALSE);

            std::thread([hwnd]() {
                std::wstring err;
                const bool ok = UpdateChecker::DownloadAndApplyUpdate(
                    g_lastUpdateInfo,
                    [hwnd](uint64_t down, uint64_t total) {
                        PostMessageW(hwnd, WM_APP_UPDATE_PROGRESS,
                                     static_cast<WPARAM>(down / 1024),
                                     static_cast<LPARAM>(total / 1024));
                    },
                    err);
                PostMessageW(hwnd, WM_APP_UPDATE_APPLY_DONE, ok ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(err)));
            }).detach();
            return 0;
        }

        if (id == IDC_BTN_RELEASE_NOTES) {
            if (!g_lastUpdateInfo.releaseUrl.empty()) {
                ShellExecuteW(hwnd, L"open", g_lastUpdateInfo.releaseUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }
        break;
    }

    case WM_APP_UPDATE_CHECK_DONE: {
        std::unique_ptr<UpdateInfo> info(reinterpret_cast<UpdateInfo*>(lParam));
        if (info) {
            g_lastUpdateInfo = *info;
            if (info->updateAvailable) {
                const std::wstring status = std::wstring(UpdateStrings::kStatusUpdateAvailable) +
                                            L" (" + info->latestVersion + L")";
                SetWindowTextW(g_hwndLblUpdateStatus, status.c_str());
                EnableWindow(g_hwndBtnInstallUpdate, TRUE);
                EnableWindow(g_hwndBtnReleaseNotes, TRUE);
            } else if (info->error.empty()) {
                const std::wstring status = std::wstring(UpdateStrings::kStatusUpToDate) +
                                            L" (v" + UpdateChecker::GetCurrentVersion() + L")";
                SetWindowTextW(g_hwndLblUpdateStatus, status.c_str());
                EnableWindow(g_hwndBtnInstallUpdate, FALSE);
                EnableWindow(g_hwndBtnReleaseNotes, FALSE);
            } else {
                SetWindowTextW(g_hwndLblUpdateStatus, info->error.c_str());
                EnableWindow(g_hwndBtnInstallUpdate, FALSE);
                EnableWindow(g_hwndBtnReleaseNotes, FALSE);
            }
        }
        EnableWindow(g_hwndBtnCheckUpdates, TRUE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_UPDATE_PROGRESS: {
        const uint64_t downKb = static_cast<uint64_t>(wParam);
        const uint64_t totalKb = static_cast<uint64_t>(lParam);
        std::wstring progress = std::wstring(UpdateStrings::kStatusDownloading) +
                                std::to_wstring(downKb / 1024) + L" MB";
        if (totalKb > 0) {
            progress += L" / " + std::to_wstring(totalKb / 1024) + L" MB";
        }
        SetWindowTextW(g_hwndLblUpdateStatus, progress.c_str());
        return 0;
    }

    case WM_APP_UPDATE_APPLY_DONE: {
        std::unique_ptr<std::wstring> err(reinterpret_cast<std::wstring*>(lParam));
        if (wParam == 1) {
            SetWindowTextW(g_hwndLblUpdateStatus, UpdateStrings::kStatusInstalling);
        } else {
            const std::wstring msg = (err && !err->empty()) ? *err : UpdateStrings::kStatusDownloadFailed;
            SetWindowTextW(g_hwndLblUpdateStatus, msg.c_str());
            EnableWindow(g_hwndBtnInstallUpdate, TRUE);
            EnableWindow(g_hwndBtnCheckUpdates, TRUE);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_ACHIEVEMENT_SYNC_PROGRESS: {
        InvalidateRect(g_hwndBtnSyncAchievements, nullptr, TRUE);
        return 0;
    }

    case WM_APP_ACHIEVEMENT_SYNC_COMPLETE: {
        g_isSyncingAchievements.store(false);
        EnableWindow(g_hwndBtnSyncAchievements, TRUE);
        InvalidateRect(g_hwndBtnSyncAchievements, nullptr, TRUE);
        const size_t count = static_cast<size_t>(wParam);
        if (count == 0) {
            MessageBoxW(hwnd, L"No games are available to sync yet. Scan and populate the library first.",
                L"Achievement Sync", MB_OK | MB_ICONINFORMATION);
        } else {
            std::wstring msg = L"Achievement sync complete for " + std::to_wstring(count) + L" game(s).";
            MessageBoxW(hwnd, msg.c_str(), L"Achievement Sync", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
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
        SetBkColor(hdc, ThemeControls::FieldSurfaceColor());
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
        if (!g_pageMode) PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace

namespace GameExplorer::Studio {

HWND CreateConfigPage(HWND parent) noexcept {
    EnsureConfigResources();
    EnsureConfigWindowClass(GetModuleHandleW(nullptr));

    // The non-null creation parameter switches the window procedure into page mode: no title bar,
    // cards laid out from the top edge, and no standalone message-loop ownership.
    g_pageMode = true;
    g_titleBarHeight = 0;

    HWND page = CreateWindowExW(
        WS_EX_CONTROLPARENT, CONFIG_WINDOW_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 640, 760, parent, nullptr, GetModuleHandleW(nullptr),
        reinterpret_cast<LPVOID>(static_cast<INT_PTR>(1)));

    if (page == nullptr) {
        g_pageMode = false;
        g_titleBarHeight = TITLE_BAR_HEIGHT;
    }
    return page;
}

} // namespace GameExplorer::Studio
