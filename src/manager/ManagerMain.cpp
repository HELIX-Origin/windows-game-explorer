#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <set>
#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/Localization.h"
#include "GameExplorerCore/ImageValidator.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")

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
ULONG_PTR g_gdiplusToken = 0;

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

// Control IDs
constexpr int IDC_LIST_GAMES          = 3001;
constexpr int IDC_BTN_ADD_GAME        = 3002;
constexpr int IDC_BTN_REMOVE_GAME     = 3003;
constexpr int IDC_BTN_REFRESH_LIST    = 3004;

constexpr int IDC_EDIT_TITLE          = 3010;
constexpr int IDC_EDIT_DEVELOPER      = 3011;
constexpr int IDC_EDIT_PUBLISHER      = 3012;
constexpr int IDC_EDIT_RELEASE_DATE   = 3013;
constexpr int IDC_EDIT_GENRES         = 3014;
constexpr int IDC_EDIT_RATING         = 3015;
constexpr int IDC_EDIT_EXE_PATH       = 3016;
constexpr int IDC_BTN_BROWSE_EXE      = 3017;
constexpr int IDC_EDIT_LAUNCH_ARGS    = 3018;
constexpr int IDC_EDIT_WORK_DIR       = 3019;
constexpr int IDC_EDIT_COVER_PATH     = 3020;
constexpr int IDC_BTN_BROWSE_COVER    = 3021;
constexpr int IDC_EDIT_HERO_PATH      = 3022;
constexpr int IDC_BTN_BROWSE_HERO     = 3023;
constexpr int IDC_EDIT_DETAILS_PATH   = 3024;
constexpr int IDC_BTN_BROWSE_DETAILS  = 3025;
constexpr int IDC_EDIT_ICON_PATH      = 3026;
constexpr int IDC_BTN_BROWSE_ICON     = 3027;
constexpr int IDC_EDIT_SYNOPSIS       = 3028;

constexpr int IDC_BTN_SAVE_METADATA   = 3030;
constexpr int IDC_BTN_LAUNCH_SELECTED = 3031;
constexpr int IDC_BTN_OPEN_SHELL      = 3032;
constexpr int IDC_BTN_RESET_CACHE      = 3033;

// Artwork Discovery & Gallery Controls
constexpr int IDC_EDIT_GALLERY_QUERY    = 3040;
constexpr int IDC_BTN_GALLERY_SEARCH    = 3041;
constexpr int IDC_BTN_GALLERY_WEB       = 3042;
constexpr int IDC_COMBO_GALLERY_TYPE    = 3043;
constexpr int IDC_COMBO_GALLERY_RES     = 3044;
constexpr int IDC_GALLERY_CANVAS        = 3045;
constexpr int IDC_BTN_APPLY_ARTWORK     = 3046;
constexpr int IDC_STATIC_GALLERY_STATUS = 3047;
constexpr int IDC_EDIT_WEB_QUERY        = 3048;
constexpr int IDC_COMBO_WEB_FILTER      = 3049;
constexpr int IDC_CHK_SAFE_SEARCH       = 3050;

constexpr int IDM_APPLY_COVER   = 5001;
constexpr int IDM_APPLY_HERO    = 5002;
constexpr int IDM_APPLY_DETAILS = 5003;
constexpr int IDM_APPLY_ICON    = 5004;

constexpr UINT WM_ARTWORK_SEARCH_DONE  = WM_APP + 60;
constexpr UINT WM_ARTWORK_THUMB_LOADED = WM_APP + 61;
constexpr UINT WM_ARTWORK_RESET_CACHE  = WM_APP + 62;

// Globals
HWND g_hwndListGames       = nullptr;
HWND g_hwndEditTitle       = nullptr;
HWND g_hwndEditDeveloper   = nullptr;
HWND g_hwndEditPublisher   = nullptr;
HWND g_hwndEditRelease     = nullptr;
HWND g_hwndEditGenres      = nullptr;
HWND g_hwndEditRating      = nullptr;
HWND g_hwndEditExePath     = nullptr;
HWND g_hwndEditLaunchArgs  = nullptr;
HWND g_hwndEditWorkDir     = nullptr;
HWND g_hwndEditCoverPath   = nullptr;
HWND g_hwndEditHeroPath    = nullptr;
HWND g_hwndEditDetailsPath = nullptr;
HWND g_hwndEditIconPath    = nullptr;
HWND g_hwndEditSynopsis    = nullptr;

HWND g_hwndGalleryQuery    = nullptr;
HWND g_hwndComboType       = nullptr;
HWND g_hwndComboRes        = nullptr;
HWND g_hwndGalleryCanvas   = nullptr;
HWND g_hwndBtnApplyArt     = nullptr;
HWND g_hwndGalleryStatus   = nullptr;
HWND g_hwndWebSearchQuery  = nullptr;
HWND g_hwndComboWebFilter  = nullptr;
HWND g_hwndChkSafeSearch   = nullptr;

// Control Handles for Responsive Layout
HWND g_hwndBtnAddGame          = nullptr;
HWND g_hwndBtnRemoveGame       = nullptr;
HWND g_hwndBtnRefreshList      = nullptr;

HWND g_hwndLblTitle            = nullptr;
HWND g_hwndLblDeveloper        = nullptr;
HWND g_hwndLblPublisher        = nullptr;
HWND g_hwndLblRelease          = nullptr;
HWND g_hwndLblGenres           = nullptr;
HWND g_hwndLblRating           = nullptr;

HWND g_hwndLblExe              = nullptr;
HWND g_hwndBtnBrowseExe        = nullptr;
HWND g_hwndLblArgs             = nullptr;
HWND g_hwndLblWorkDir          = nullptr;
HWND g_hwndLblCover            = nullptr;
HWND g_hwndBtnBrowseCover      = nullptr;
HWND g_hwndLblHero             = nullptr;
HWND g_hwndBtnBrowseHero       = nullptr;
HWND g_hwndLblDetails          = nullptr;
HWND g_hwndBtnBrowseDetails    = nullptr;
HWND g_hwndLblIcon             = nullptr;
HWND g_hwndBtnBrowseIcon       = nullptr;
HWND g_hwndLblSynopsis         = nullptr;

HWND g_hwndBtnSave             = nullptr;
HWND g_hwndBtnLaunch           = nullptr;
HWND g_hwndBtnOpenShell        = nullptr;

HWND g_hwndBtnSearchProviders  = nullptr;
HWND g_hwndBtnResetCache       = nullptr;
HWND g_hwndLblType             = nullptr;
HWND g_hwndLblRes              = nullptr;
HWND g_hwndBtnWebSearch        = nullptr;

std::vector<GameItem> g_games;
int g_selectedIndex = -1;
std::wstring g_initialSelectAppId;

std::vector<OnlineArtworkCandidate> g_galleryCandidates;
int g_selectedCandidateIdx = -1;
int g_galleryScrollY = 0;
bool g_isSearchingArt = false;
std::wstring g_lastSearchedTitle;

HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
HFONT g_hFontSmall = nullptr;
bool g_isDarkMode = false;
ThemePalette g_palette{};
HBRUSH g_hBgBrush = nullptr;
HBRUSH g_hCardBrush = nullptr;
HBRUSH g_hEditBrush = nullptr;

constexpr int PREVIEW_BOX_X = 286;
constexpr int PREVIEW_BOX_Y = TITLE_BAR_HEIGHT + 44;
constexpr int PREVIEW_BOX_W = 126;
constexpr int PREVIEW_BOX_H = 189;

// Safely load image from disk without locking file handles
std::unique_ptr<Gdiplus::Image> LoadImageFromFileUnlocked(const std::wstring& path) {
    if (path.empty() || !fs::exists(path)) return nullptr;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return nullptr;
    const std::streamsize size = file.tellg();
    if (size <= 0) return nullptr;
    file.seekg(0, std::ios::beg);

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(size));
    if (!hMem) return nullptr;

    void* pMem = GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return nullptr;
    }
    file.read(static_cast<char*>(pMem), size);
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
        GlobalFree(hMem);
        return nullptr;
    }

    auto img = std::make_unique<Gdiplus::Image>(pStream);
    pStream->Release();
    if (img->GetLastStatus() != Gdiplus::Ok || img->GetWidth() == 0 || img->GetHeight() == 0) {
        return nullptr;
    }
    return img;
}

std::wstring ReadEditString(HWND hEdit) {
    if (hEdit == nullptr) return L"";
    const int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return L"";
    std::wstring str(static_cast<size_t>(len), L'\0');
    GetWindowTextW(hEdit, str.data(), len + 1);
    return str;
}

std::wstring GetThumbnailCachePath(const std::wstring& url) {
    uint32_t h = 2166136261u;
    for (wchar_t c : url) {
        h ^= static_cast<uint32_t>(c);
        h *= 16777619u;
    }
    wchar_t buf[32] = { 0 };
    wsprintfW(buf, L"thumb_%08X.jpg", h);
    fs::path dir = Storage::GetCachePath() / L"art" / L"thumbs";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / buf).wstring();
}

void TriggerArtworkSearch(HWND hwnd, bool includeWebSearch, const std::wstring& explicitQuery = L"", const std::wstring& explicitType = L"", const std::wstring& explicitRes = L"", bool disableSafeSearch = false);

void PopulateEditorFromSelected(HWND hwnd) {
    if (g_selectedIndex < 0 || static_cast<size_t>(g_selectedIndex) >= g_games.size()) {
        SetWindowTextW(g_hwndEditTitle, L"");
        SetWindowTextW(g_hwndEditDeveloper, L"");
        SetWindowTextW(g_hwndEditPublisher, L"");
        SetWindowTextW(g_hwndEditRelease, L"");
        SetWindowTextW(g_hwndEditGenres, L"");
        SetWindowTextW(g_hwndEditRating, L"");
        SetWindowTextW(g_hwndEditExePath, L"");
        SetWindowTextW(g_hwndEditLaunchArgs, L"");
        SetWindowTextW(g_hwndEditWorkDir, L"");
        SetWindowTextW(g_hwndEditCoverPath, L"");
        SetWindowTextW(g_hwndEditHeroPath, L"");
        SetWindowTextW(g_hwndEditDetailsPath, L"");
        SetWindowTextW(g_hwndEditIconPath, L"");
        SetWindowTextW(g_hwndEditSynopsis, L"");
        SetWindowTextW(g_hwndGalleryQuery, L"");
        SetWindowTextW(g_hwndWebSearchQuery, L"");
    } else {
        const auto& g = g_games[static_cast<size_t>(g_selectedIndex)];
        SetWindowTextW(g_hwndEditTitle, g.title.c_str());
        SetWindowTextW(g_hwndEditDeveloper, g.developer.c_str());
        SetWindowTextW(g_hwndEditPublisher, g.publisher.c_str());
        SetWindowTextW(g_hwndEditRelease, g.releaseDate.c_str());
        SetWindowTextW(g_hwndEditGenres, g.genres.c_str());
        SetWindowTextW(g_hwndEditRating, g.rating.c_str());
        SetWindowTextW(g_hwndEditExePath, g.executablePath.c_str());
        SetWindowTextW(g_hwndEditLaunchArgs, g.launchArguments.c_str());
        SetWindowTextW(g_hwndEditWorkDir, g.workingDirectory.c_str());
        SetWindowTextW(g_hwndEditCoverPath, g.coverArtPath.c_str());
        SetWindowTextW(g_hwndEditHeroPath, g.heroArtPath.c_str());
        SetWindowTextW(g_hwndEditDetailsPath, g.detailsArtPath.c_str());
        SetWindowTextW(g_hwndEditIconPath, g.iconPath.c_str());
        SetWindowTextW(g_hwndEditSynopsis, g.synopsis.c_str());
        SetWindowTextW(g_hwndGalleryQuery, g.title.c_str());
        SetWindowTextW(g_hwndWebSearchQuery, g.title.c_str());

        // Automatically load metadata provider artwork candidates for selected title
        if (g.title != g_lastSearchedTitle) {
            g_lastSearchedTitle = g.title;
            TriggerArtworkSearch(hwnd, false);
        }
    }

    RECT rcPreview = { PREVIEW_BOX_X - 4, PREVIEW_BOX_Y - 4, PREVIEW_BOX_X + PREVIEW_BOX_W + 4, PREVIEW_BOX_Y + PREVIEW_BOX_H + 4 };
    InvalidateRect(hwnd, &rcPreview, FALSE);
}

void RefreshGameList(HWND hwnd, const std::wstring& selectAppId = L"") {
    g_games = StorefrontScanner::ScanAllInstalledGames();
    SendMessageW(g_hwndListGames, LB_RESETCONTENT, 0, 0);

    int targetIdx = 0;
    for (size_t i = 0; i < g_games.size(); ++i) {
        const auto& g = g_games[i];
        std::wstring label = L"[" + std::wstring(PlatformToString(g.platform)) + L"] " + g.title;
        SendMessageW(g_hwndListGames, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (!selectAppId.empty() && g.appId == selectAppId) {
            targetIdx = static_cast<int>(i);
        }
    }

    if (!g_games.empty()) {
        g_selectedIndex = targetIdx;
        SendMessageW(g_hwndListGames, LB_SETCURSEL, static_cast<WPARAM>(g_selectedIndex), 0);
    } else {
        g_selectedIndex = -1;
    }

    PopulateEditorFromSelected(hwnd);
}

BOOL CALLBACK BroadcastRefreshToShellViews(HWND hwnd, LPARAM lParam) {
    wchar_t cls[128] = { 0 };
    if (GetClassNameW(hwnd, cls, 128) > 0 && (wcscmp(cls, L"GameExplorerXamlHostViewClass") == 0 || wcscmp(cls, L"GameExplorerNativeView") == 0)) {
        PostMessageW(hwnd, WM_APP + 42, 0, 0);
    }
    EnumChildWindows(hwnd, BroadcastRefreshToShellViews, lParam);
    return TRUE;
}

void SaveCurrentGameEdits(HWND hwnd) {
    if (g_selectedIndex < 0 || static_cast<size_t>(g_selectedIndex) >= g_games.size()) {
        MessageBoxW(hwnd, L"Please select a game from the library list or add a local game first.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
        return;
    }

    auto& g = g_games[static_cast<size_t>(g_selectedIndex)];
    g.title            = ReadEditString(g_hwndEditTitle);
    g.developer        = ReadEditString(g_hwndEditDeveloper);
    g.publisher        = ReadEditString(g_hwndEditPublisher);
    g.releaseDate      = ReadEditString(g_hwndEditRelease);
    g.genres           = ReadEditString(g_hwndEditGenres);
    g.rating           = ReadEditString(g_hwndEditRating);
    g.executablePath   = ReadEditString(g_hwndEditExePath);
    g.launchArguments  = ReadEditString(g_hwndEditLaunchArgs);
    g.workingDirectory = ReadEditString(g_hwndEditWorkDir);
    g.coverArtPath     = ReadEditString(g_hwndEditCoverPath);
    g.heroArtPath      = ReadEditString(g_hwndEditHeroPath);
    g.detailsArtPath   = ReadEditString(g_hwndEditDetailsPath);
    g.iconPath         = ReadEditString(g_hwndEditIconPath);
    g.synopsis         = ReadEditString(g_hwndEditSynopsis);

    auto handleRemoteUrl = [&](std::wstring& path, const std::wstring& slot) {
        if (path.rfind(L"http://", 0) == 0 || path.rfind(L"https://", 0) == 0) {
            const std::wstring local = StorefrontScanner::CacheArtworkFromUrl(g.appId, slot, path);
            if (!local.empty()) path = local;
        }
    };
    handleRemoteUrl(g.coverArtPath, L"cover");
    handleRemoteUrl(g.heroArtPath, L"hero");
    handleRemoteUrl(g.detailsArtPath, L"details");
    handleRemoteUrl(g.iconPath, L"icon");

    SetWindowTextW(g_hwndEditCoverPath, g.coverArtPath.c_str());
    SetWindowTextW(g_hwndEditHeroPath, g.heroArtPath.c_str());
    SetWindowTextW(g_hwndEditDetailsPath, g.detailsArtPath.c_str());
    SetWindowTextW(g_hwndEditIconPath, g.iconPath.c_str());

    Database::Instance().UpsertGame(g);

    if (StorefrontScanner::SavePerGameMetadata(g)) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
        EnumWindows(BroadcastRefreshToShellViews, 0);
        const std::wstring currentAppId = g.appId;
        RefreshGameList(hwnd, currentAppId);
        MessageBoxW(hwnd, L"Game metadata and artwork successfully saved to your library.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd, L"Failed to write per-game metadata JSON.", L"Error", MB_OK | MB_ICONERROR);
    }
}

bool BrowseForFile(HWND hwnd, const wchar_t* title, const wchar_t* filter, std::wstring& outPath) {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = ARRAYSIZE(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameW(&ofn)) {
        outPath = szFile;
        return true;
    }
    return false;
}

void TriggerArtworkSearch(
    HWND hwnd,
    bool includeWebSearch,
    const std::wstring& explicitQuery,
    const std::wstring& explicitType,
    const std::wstring& explicitRes,
    bool disableSafeSearch
) {
    std::wstring q = explicitQuery;
    if (q.empty()) {
        q = ReadEditString(includeWebSearch ? g_hwndWebSearchQuery : g_hwndGalleryQuery);
    }
    if (q.empty() && g_selectedIndex >= 0 && static_cast<size_t>(g_selectedIndex) < g_games.size()) {
        q = g_games[static_cast<size_t>(g_selectedIndex)].title;
        if (includeWebSearch) {
            SetWindowTextW(g_hwndWebSearchQuery, q.c_str());
        } else {
            SetWindowTextW(g_hwndGalleryQuery, q.c_str());
        }
    }
    if (q.empty()) return;

    std::wstring typeF = explicitType.empty() ? L"All" : explicitType;
    if (explicitType.empty()) {
        HWND combo = includeWebSearch ? g_hwndComboWebFilter : g_hwndComboType;
        if (combo != nullptr) {
            wchar_t buf[64] = { 0 };
            GetWindowTextW(combo, buf, 64);
            std::wstring s = buf;
            if (s.find(L"Box Art") != std::wstring::npos) typeF = L"Cover";
            else if (s.find(L"Hero") != std::wstring::npos) typeF = L"Hero";
            else if (s.find(L"Patreon") != std::wstring::npos) typeF = L"Patreon";
            else if (s.find(L"Icon") != std::wstring::npos) typeF = L"Icon";
            else if (s.find(L"Details") != std::wstring::npos || s.find(L"Screenshot") != std::wstring::npos) typeF = L"Details";
        }
    }

    std::wstring resF = explicitRes.empty() ? L"All" : explicitRes;
    if (explicitRes.empty() && g_hwndComboRes != nullptr) {
        wchar_t buf[64] = { 0 };
        GetWindowTextW(g_hwndComboRes, buf, 64);
        std::wstring s = buf;
        if (s.find(L"600x900") != std::wstring::npos) resF = L"600x900";
        else if (s.find(L"300x450") != std::wstring::npos) resF = L"300x450";
        else if (s.find(L"1920x620") != std::wstring::npos) resF = L"1920x620";
        else if (s.find(L"1920x1080") != std::wstring::npos) resF = L"1920x1080";
        else if (s.find(L"256x256") != std::wstring::npos) resF = L"256x256+";
    }

    const bool safeOff = disableSafeSearch || (g_hwndChkSafeSearch != nullptr && SendMessageW(g_hwndChkSafeSearch, BM_GETCHECK, 0, 0) == BST_CHECKED);

    g_isSearchingArt = true;
    g_selectedCandidateIdx = -1;
    g_galleryScrollY = 0;
    if (g_hwndGalleryStatus != nullptr) {
        SetWindowTextW(g_hwndGalleryStatus, includeWebSearch ? (safeOff ? L"Searching web & NSFW images (SafeSearch OFF)..." : L"Searching web & Patreon images...") : (safeOff ? L"Searching metadata providers (NSFW unblocked)..." : L"Searching metadata providers..."));
    }
    if (g_hwndGalleryCanvas != nullptr) {
        InvalidateRect(g_hwndGalleryCanvas, nullptr, TRUE);
    }

    std::thread([hwnd, q, typeF, resF, includeWebSearch, safeOff]() {
        auto results = new std::vector<OnlineArtworkCandidate>(
            StorefrontScanner::SearchOnlineArtwork(q, typeF, resF, includeWebSearch, safeOff)
        );
        PostMessageW(hwnd, WM_ARTWORK_SEARCH_DONE, 0, reinterpret_cast<LPARAM>(results));
    }).detach();
}

void ApplySelectedArtwork(HWND hwnd, int targetSlotId) {
    if (g_selectedCandidateIdx < 0 || static_cast<size_t>(g_selectedCandidateIdx) >= g_galleryCandidates.size()) {
        MessageBoxW(hwnd, L"Please select an artwork image from the gallery first.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_selectedIndex < 0 || static_cast<size_t>(g_selectedIndex) >= g_games.size()) {
        MessageBoxW(hwnd, L"Please select a game from your library to apply artwork to.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const auto& cand = g_galleryCandidates[static_cast<size_t>(g_selectedCandidateIdx)];
    const auto& game = g_games[static_cast<size_t>(g_selectedIndex)];

    std::wstring slotName;
    HWND targetEdit = nullptr;
    switch (targetSlotId) {
    case IDM_APPLY_COVER:
        slotName = L"cover";
        targetEdit = g_hwndEditCoverPath;
        break;
    case IDM_APPLY_HERO:
        slotName = L"hero";
        targetEdit = g_hwndEditHeroPath;
        break;
    case IDM_APPLY_DETAILS:
        slotName = L"details";
        targetEdit = g_hwndEditDetailsPath;
        break;
    case IDM_APPLY_ICON:
        slotName = L"icon";
        targetEdit = g_hwndEditIconPath;
        break;
    default:
        return;
    }

    SetWindowTextW(g_hwndGalleryStatus, L"Downloading high-resolution artwork...");
    UpdateWindow(g_hwndGalleryStatus);

    const std::wstring cachedPath = StorefrontScanner::CacheArtworkFromUrl(game.appId, slotName, cand.highResUrl);
    if (!cachedPath.empty()) {
        SetWindowTextW(targetEdit, cachedPath.c_str());
        if (targetSlotId == IDM_APPLY_COVER) {
            RECT rcPreview = { PREVIEW_BOX_X - 4, PREVIEW_BOX_Y - 4, PREVIEW_BOX_X + PREVIEW_BOX_W + 4, PREVIEW_BOX_Y + PREVIEW_BOX_H + 4 };
            InvalidateRect(hwnd, &rcPreview, FALSE);
        }
        std::wstring msg = L"Applied to " + slotName + L"! Click 'Save Game Metadata' to persist.";
        SetWindowTextW(g_hwndGalleryStatus, msg.c_str());
    } else {
        SetWindowTextW(g_hwndGalleryStatus, L"Failed to download remote artwork.");
    }
}

// Custom Child Window for Gallery Canvas
LRESULT CALLBACK GalleryCanvasWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static std::set<std::wstring> s_downloadingUrls;
    static std::mutex s_dlMutex;

    switch (uMsg) {
    case WM_APP + 62: { // WM_ARTWORK_RESET_CACHE
        std::lock_guard<std::mutex> lock(s_dlMutex);
        s_downloadingUrls.clear();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        const int w = rcClient.right - rcClient.left;
        const int h = rcClient.bottom - rcClient.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        HBRUSH bgBrush = CreateSolidBrush(g_isDarkMode ? RGB(26, 26, 26) : RGB(244, 244, 244));
        FillRect(memDC, &rcClient, bgBrush);
        DeleteObject(bgBrush);

        if (g_isSearchingArt) {
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, g_palette.textSecondary);
            SelectObject(memDC, g_hFont);
            DrawTextW(memDC, L"Searching metadata providers for authentic artwork...", -1, &rcClient, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (g_galleryCandidates.empty()) {
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, g_palette.textSecondary);
            SelectObject(memDC, g_hFont);
            DrawTextW(memDC, L"No artwork found. Click 'Search Providers' or 'Web Search' to find images.", -1, &rcClient, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        } else {
            Gdiplus::Graphics graphics(memDC);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

            // Determine grid columns and card dimensions based on selected filter
            wchar_t typeBuf[64] = { 0 };
            GetWindowTextW(g_hwndComboType, typeBuf, 64);
            const std::wstring typeStr = typeBuf;

            int cols = 3;
            int cardW = 148;
            int cardH = 248;
            int imgH = 222;

            if (typeStr.find(L"Hero") != std::wstring::npos) {
                cols = 2;
                cardW = 226;
                imgH = 73;
                cardH = 99;
            } else if (typeStr.find(L"Icon") != std::wstring::npos) {
                cols = 4;
                cardW = 108;
                imgH = 108;
                cardH = 134;
            }

            constexpr int padX = 8;
            constexpr int padY = 8;

            const int totalRows = static_cast<int>((g_galleryCandidates.size() + cols - 1) / cols);
            const int totalHeight = totalRows * (cardH + padY) + padY;

            // Configure vertical scrollbar
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin = 0;
            si.nMax = totalHeight;
            si.nPage = static_cast<UINT>(h);
            si.nPos = g_galleryScrollY;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

            for (size_t i = 0; i < g_galleryCandidates.size(); ++i) {
                const auto& cand = g_galleryCandidates[i];
                const int row = static_cast<int>(i / cols);
                const int col = static_cast<int>(i % cols);

                const int x = padX + col * (cardW + padX);
                const int y = padY + row * (cardH + padY) - g_galleryScrollY;

                if (y + cardH < 0 || y > h) continue; // Out of view

                RECT rcCard = { x, y, x + cardW, y + cardH };
                const bool isSelected = (static_cast<int>(i) == g_selectedCandidateIdx);

                // Draw card container
                HBRUSH cBrush = CreateSolidBrush(g_palette.cardBg);
                HPEN cPen = CreatePen(PS_SOLID, isSelected ? 2 : 1, isSelected ? g_palette.accent : g_palette.cardBorder);
                HGDIOBJ oB = SelectObject(memDC, cBrush);
                HGDIOBJ oP = SelectObject(memDC, cPen);
                RoundRect(memDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 6, 6);
                SelectObject(memDC, oP);
                SelectObject(memDC, oB);
                DeleteObject(cPen);
                DeleteObject(cBrush);

                // Thumbnail image rendering
                const std::wstring thumbPath = GetThumbnailCachePath(cand.thumbUrl);
                bool drewImage = false;

                if (fs::exists(thumbPath)) {
                    Gdiplus::Image gImg(thumbPath.c_str());
                    if (gImg.GetLastStatus() == Gdiplus::Ok && gImg.GetWidth() > 0 && gImg.GetHeight() > 0) {
                        graphics.DrawImage(&gImg, x + 2, y + 2, cardW - 4, imgH);
                        drewImage = true;
                    }
                }

                if (!drewImage) {
                    // Placeholder box
                    RECT rcImg = { x + 2, y + 2, x + cardW - 2, y + 2 + imgH };
                    HBRUSH phBrush = CreateSolidBrush(g_isDarkMode ? RGB(38, 38, 38) : RGB(228, 228, 228));
                    FillRect(memDC, &rcImg, phBrush);
                    DeleteObject(phBrush);

                    SetBkMode(memDC, TRANSPARENT);
                    SetTextColor(memDC, g_palette.textSecondary);
                    SelectObject(memDC, g_hFontSmall);
                    DrawTextW(memDC, L"Loading...", -1, &rcImg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    // Dispatch async thumbnail download
                    {
                        std::lock_guard<std::mutex> lock(s_dlMutex);
                        if (s_downloadingUrls.find(cand.thumbUrl) == s_downloadingUrls.end()) {
                            s_downloadingUrls.insert(cand.thumbUrl);
                            std::wstring tUrl = cand.thumbUrl;
                            std::thread([hwnd, tUrl, thumbPath]() {
                                StorefrontScanner::DownloadRemoteFile(tUrl, thumbPath);
                                PostMessageW(hwnd, WM_ARTWORK_THUMB_LOADED, 0, 0);
                            }).detach();
                        }
                    }
                }

                // Source tag pill (top-left)
                RECT rcSource = { x + 6, y + 6, x + cardW - 6, y + 22 };
                HBRUSH srcBg = CreateSolidBrush(g_isDarkMode ? RGB(20, 20, 20) : RGB(245, 245, 245));
                HPEN srcPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
                HGDIOBJ oSb = SelectObject(memDC, srcBg);
                HGDIOBJ oSp = SelectObject(memDC, srcPen);
                RECT pillRect = { x + 4, y + 4, x + 4 + static_cast<int>(cand.source.size() * 7 + 10), y + 20 };
                RoundRect(memDC, pillRect.left, pillRect.top, pillRect.right, pillRect.bottom, 4, 4);
                SelectObject(memDC, oSp);
                SelectObject(memDC, oSb);
                DeleteObject(srcPen);
                DeleteObject(srcBg);

                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, g_palette.textPrimary);
                SelectObject(memDC, g_hFontSmall);
                DrawTextW(memDC, cand.source.c_str(), -1, &pillRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                // Resolution badge (bottom of image)
                wchar_t resStr[64] = { 0 };
                if (cand.width > 0 && cand.height > 0) {
                    wsprintfW(resStr, L"%dx%d", cand.width, cand.height);
                } else {
                    wcscpy_s(resStr, cand.type.c_str());
                }

                RECT rcRes = { x + 6, y + imgH + 6, x + cardW - 6, y + cardH - 2 };
                SetTextColor(memDC, isSelected ? g_palette.accent : g_palette.textPrimary);
                SelectObject(memDC, isSelected ? g_hFontBold : g_hFontSmall);
                DrawTextW(memDC, resStr, -1, &rcRes, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                if (isSelected) {
                    RECT rcCheck = { x + cardW - 24, y + imgH + 4, x + cardW - 6, y + cardH - 2 };
                    DrawTextW(memDC, L"\u2713", -1, &rcCheck, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ARTWORK_THUMB_LOADED:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_VSCROLL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);

        int curPos = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:   curPos -= 30; break;
        case SB_LINEDOWN: curPos += 30; break;
        case SB_PAGEUP:   curPos -= si.nPage; break;
        case SB_PAGEDOWN: curPos += si.nPage; break;
        case SB_THUMBTRACK: curPos = si.nTrackPos; break;
        }

        curPos = (std::max)(0, (std::min)(curPos, si.nMax - static_cast<int>(si.nPage)));
        if (curPos != si.nPos) {
            g_galleryScrollY = curPos;
            SetScrollPos(hwnd, SB_VERT, curPos, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        SendMessageW(hwnd, WM_VSCROLL, MAKEWPARAM(zDelta > 0 ? SB_LINEUP : SB_LINEDOWN, 0), 0);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int clickX = GET_X_LPARAM(lParam);
        const int clickY = GET_Y_LPARAM(lParam) + g_galleryScrollY;

        wchar_t typeBuf[64] = { 0 };
        GetWindowTextW(g_hwndComboType, typeBuf, 64);
        const std::wstring typeStr = typeBuf;

        int cols = 3;
        int cardW = 148;
        int cardH = 248;

        if (typeStr.find(L"Hero") != std::wstring::npos) {
            cols = 2;
            cardW = 226;
            cardH = 99;
        } else if (typeStr.find(L"Icon") != std::wstring::npos) {
            cols = 4;
            cardW = 108;
            cardH = 134;
        }

        constexpr int padX = 8;
        constexpr int padY = 8;

        for (size_t i = 0; i < g_galleryCandidates.size(); ++i) {
            const int row = static_cast<int>(i / cols);
            const int col = static_cast<int>(i % cols);
            const int x = padX + col * (cardW + padX);
            const int y = padY + row * (cardH + padY);

            if (clickX >= x && clickX <= x + cardW && clickY >= y && clickY <= y + cardH) {
                g_selectedCandidateIdx = static_cast<int>(i);
                InvalidateRect(hwnd, nullptr, FALSE);

                const auto& cand = g_galleryCandidates[i];
                wchar_t statusBuf[128] = { 0 };
                wsprintfW(statusBuf, L"Selected: %dx%d (%s) \u2022 Click 'Apply Selected Artwork \u25BE' to choose view",
                    cand.width, cand.height, cand.source.c_str());
                SetWindowTextW(g_hwndGalleryStatus, statusBuf);
                break;
            }
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (g_selectedCandidateIdx >= 0 && static_cast<size_t>(g_selectedCandidateIdx) < g_galleryCandidates.size()) {
            wchar_t typeBuf[64] = { 0 };
            GetWindowTextW(g_hwndComboType, typeBuf, 64);
            const std::wstring typeStr = typeBuf;

            int targetSlot = IDM_APPLY_COVER;
            if (typeStr.find(L"Hero") != std::wstring::npos) targetSlot = IDM_APPLY_HERO;
            else if (typeStr.find(L"Icon") != std::wstring::npos) targetSlot = IDM_APPLY_ICON;
            else if (typeStr.find(L"Details") != std::wstring::npos) targetSlot = IDM_APPLY_DETAILS;

            ApplySelectedArtwork(GetParent(hwnd), targetSlot);
        }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ManagerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_hFontBold = CreateFontIndirectW(&ncm.lfMessageFont);

        ncm.lfMessageFont.lfWeight = FW_NORMAL;
        ncm.lfMessageFont.lfHeight = -11;
        g_hFontSmall = CreateFontIndirectW(&ncm.lfMessageFont);

        g_hAppIcon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(100), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR
        ));

        // Column 1: Games List (Left)
        constexpr int leftX = 26;
        constexpr int topY  = TITLE_BAR_HEIGHT + 48;

        g_hwndListGames = CreateWindowExW(
            0, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            leftX, topY, 230, 560,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_GAMES)), nullptr, nullptr
        );
        SendMessageW(g_hwndListGames, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        DarkMode::ApplyWindowTheme(g_hwndListGames, g_isDarkMode);

        HWND hBtnAdd = CreateWindowExW(0, L"BUTTON", L"\U0001F4C1 Browse & Add...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            leftX, topY + 568, 130, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD_GAME)), nullptr, nullptr);
        SendMessageW(hBtnAdd, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnRemove = CreateWindowExW(0, L"BUTTON", L"Remove",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            leftX + 134, topY + 568, 56, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REMOVE_GAME)), nullptr, nullptr);
        SendMessageW(hBtnRemove, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"\u21BB",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            leftX + 194, topY + 568, 36, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REFRESH_LIST)), nullptr, nullptr);
        SendMessageW(hBtnRefresh, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Column 2: Editor controls next to the 2:3 Box Art Preview (Center)
        constexpr int formLabelX = 422;
        constexpr int formEditX  = 512;
        constexpr int formEditW  = 192;

        auto createField = [&](const wchar_t* label, HWND& outEdit, int id, int xLbl, int xEd, int wEd, int y) {
            HWND hLbl = CreateWindowExW(0, L"STATIC", label, WS_CHILD | WS_VISIBLE,
                xLbl, y + 3, xEd - xLbl - 4, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hLbl, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

            outEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                xEd, y + 2, wEd, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(outEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
            DarkMode::ApplyWindowTheme(outEdit, g_isDarkMode);
        };

        createField(L"Title:",        g_hwndEditTitle,     IDC_EDIT_TITLE,        formLabelX, formEditX, formEditW, topY + 2);
        createField(L"Developer:",    g_hwndEditDeveloper, IDC_EDIT_DEVELOPER,    formLabelX, formEditX, formEditW, topY + 34);
        createField(L"Publisher:",    g_hwndEditPublisher, IDC_EDIT_PUBLISHER,    formLabelX, formEditX, formEditW, topY + 66);
        createField(L"Release Date:", g_hwndEditRelease,   IDC_EDIT_RELEASE_DATE, formLabelX, formEditX, formEditW, topY + 98);
        createField(L"Genres:",       g_hwndEditGenres,    IDC_EDIT_GENRES,       formLabelX, formEditX, formEditW, topY + 130);
        createField(L"Box Rating:",   g_hwndEditRating,    IDC_EDIT_RATING,       formLabelX, formEditX, formEditW, topY + 162);

        // Full-width fields below the 2:3 Box Art Preview
        constexpr int fullLabelX = 288;
        constexpr int fullEditX  = 398;
        constexpr int fullEditW  = 238;

        createField(L"Executable:",   g_hwndEditExePath,    IDC_EDIT_EXE_PATH,    fullLabelX, fullEditX, fullEditW, topY + 204);
        HWND hBtnBrowseExe = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 6, topY + 201, 62, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_EXE)), nullptr, nullptr);
        SendMessageW(hBtnBrowseExe, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Arguments:",    g_hwndEditLaunchArgs, IDC_EDIT_LAUNCH_ARGS, fullLabelX, fullEditX, 306, topY + 236);
        createField(L"Working Dir:",  g_hwndEditWorkDir,    IDC_EDIT_WORK_DIR,    fullLabelX, fullEditX, 306, topY + 268);

        createField(L"Box Art (~2:3):", g_hwndEditCoverPath, IDC_EDIT_COVER_PATH, fullLabelX, fullEditX, fullEditW, topY + 300);
        HWND hBtnBrowseCover = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 6, topY + 297, 62, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_COVER)), nullptr, nullptr);
        SendMessageW(hBtnBrowseCover, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Hero Banner:",  g_hwndEditHeroPath,   IDC_EDIT_HERO_PATH,   fullLabelX, fullEditX, fullEditW, topY + 332);
        HWND hBtnBrowseHero = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 6, topY + 329, 62, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_HERO)), nullptr, nullptr);
        SendMessageW(hBtnBrowseHero, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Details Art:",  g_hwndEditDetailsPath, IDC_EDIT_DETAILS_PATH, fullLabelX, fullEditX, fullEditW, topY + 364);
        HWND hBtnBrowseDetails = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 6, topY + 361, 62, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_DETAILS)), nullptr, nullptr);
        SendMessageW(hBtnBrowseDetails, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Icon Path:",    g_hwndEditIconPath,   IDC_EDIT_ICON_PATH,   fullLabelX, fullEditX, fullEditW, topY + 396);
        HWND hBtnBrowseIcon = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 6, topY + 393, 62, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_ICON)), nullptr, nullptr);
        SendMessageW(hBtnBrowseIcon, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Synopsis:",     g_hwndEditSynopsis,   IDC_EDIT_SYNOPSIS,    fullLabelX, fullEditX, 306, topY + 428);

        // Bottom Action Buttons in Center Column
        HWND hBtnSave = CreateWindowExW(0, L"BUTTON", L"Save Game Metadata",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullLabelX, topY + 568, 138, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SAVE_METADATA)), nullptr, nullptr);
        SendMessageW(hBtnSave, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"\u25B6 Play Game",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullLabelX + 144, topY + 568, 102, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_LAUNCH_SELECTED)), nullptr, nullptr);
        SendMessageW(hBtnLaunch, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnOpenShell = CreateWindowExW(0, L"BUTTON", L"Open Folder",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullLabelX + 252, topY + 568, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OPEN_SHELL)), nullptr, nullptr);
        SendMessageW(hBtnOpenShell, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Column 3: Online Artwork Gallery & Discovery (Right)
        constexpr int rightX = 738;

        g_hwndGalleryQuery = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            rightX, topY + 2, 220, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_GALLERY_QUERY)), nullptr, nullptr);
        SendMessageW(g_hwndGalleryQuery, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        DarkMode::ApplyWindowTheme(g_hwndGalleryQuery, g_isDarkMode);

        HWND hBtnSearch = CreateWindowExW(0, L"BUTTON", L"\U0001F50D Search Providers",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            rightX + 226, topY + 1, 136, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_GALLERY_SEARCH)), nullptr, nullptr);
        SendMessageW(hBtnSearch, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnResetCache = CreateWindowExW(0, L"BUTTON", L"\U0001F9F9 Reset Cache",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            rightX + 368, topY + 1, 118, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_RESET_CACHE)), nullptr, nullptr);
        SendMessageW(hBtnResetCache, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Filters: Type and Resolution Dropdowns
        HWND hLblType = CreateWindowExW(0, L"STATIC", L"Type:", WS_CHILD | WS_VISIBLE,
            rightX, topY + 36, 38, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblType, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndComboType = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            rightX + 42, topY + 34, 178, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_GALLERY_TYPE)), nullptr, nullptr);
        SendMessageW(g_hwndComboType, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        SendMessageW(g_hwndComboType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Types"));
        SendMessageW(g_hwndComboType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Box Art (~2:3 Cover)"));
        SendMessageW(g_hwndComboType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hero Banner (~3.1:1)"));
        SendMessageW(g_hwndComboType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Icon / Logo (1:1)"));
        SendMessageW(g_hwndComboType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Details / Screenshots"));
        SendMessageW(g_hwndComboType, CB_SETCURSEL, 0, 0);
        DarkMode::ApplyWindowTheme(g_hwndComboType, g_isDarkMode);

        HWND hLblRes = CreateWindowExW(0, L"STATIC", L"Resolution:", WS_CHILD | WS_VISIBLE,
            rightX + 228, topY + 36, 72, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblRes, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndComboRes = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            rightX + 304, topY + 34, 182, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_GALLERY_RES)), nullptr, nullptr);
        SendMessageW(g_hwndComboRes, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        SendMessageW(g_hwndComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Resolutions"));
        SendMessageW(g_hwndComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"600x900 (High-Res Cover)"));
        SendMessageW(g_hwndComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"300x450 (Standard Cover)"));
        SendMessageW(g_hwndComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1920x620 (High-Res Hero)"));
        SendMessageW(g_hwndComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1920x1080 (Full HD)"));
        SendMessageW(g_hwndComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"256x256+ (Square Icon)"));
        SendMessageW(g_hwndComboRes, CB_SETCURSEL, 0, 0);
        DarkMode::ApplyWindowTheme(g_hwndComboRes, g_isDarkMode);

        // Viewport Gallery Canvas
        g_hwndGalleryCanvas = CreateWindowExW(0, L"GameExplorerGalleryCanvas", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            rightX, topY + 66, 486, 440, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GALLERY_CANVAS)), nullptr, nullptr);

        // Bottom Row 1: Status and Apply Button
        g_hwndGalleryStatus = CreateWindowExW(0, L"STATIC", L"Select an image from above to apply.",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
            rightX, topY + 514, 230, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC_GALLERY_STATUS)), nullptr, nullptr);
        SendMessageW(g_hwndGalleryStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontSmall), TRUE);

        g_hwndBtnApplyArt = CreateWindowExW(0, L"BUTTON", L"Apply Selected Artwork \u25BE",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            rightX + 236, topY + 510, 250, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_APPLY_ARTWORK)), nullptr, nullptr);
        SendMessageW(g_hwndBtnApplyArt, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Bottom Row 2: Dedicated Internal Web Image Search (Keyword: "{{game.title}}" {{content.type}})
        g_hwndWebSearchQuery = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            rightX, topY + 548, 186, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_WEB_QUERY)), nullptr, nullptr);
        SendMessageW(g_hwndWebSearchQuery, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        DarkMode::ApplyWindowTheme(g_hwndWebSearchQuery, g_isDarkMode);

        g_hwndComboWebFilter = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            rightX + 192, topY + 548, 150, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_WEB_FILTER)), nullptr, nullptr);
        SendMessageW(g_hwndComboWebFilter, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        SendMessageW(g_hwndComboWebFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Web Sources"));
        SendMessageW(g_hwndComboWebFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Box Art (~2:3 Cover)"));
        SendMessageW(g_hwndComboWebFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hero Banners"));
        SendMessageW(g_hwndComboWebFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Patreon Campaign Media"));
        SendMessageW(g_hwndComboWebFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Icons / Logos"));
        SendMessageW(g_hwndComboWebFilter, CB_SETCURSEL, 0, 0);
        DarkMode::ApplyWindowTheme(g_hwndComboWebFilter, g_isDarkMode);

        HWND hBtnWebSearch = CreateWindowExW(0, L"BUTTON", L"\U0001F310 Search Web",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            rightX + 348, topY + 547, 138, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_GALLERY_WEB)), nullptr, nullptr);
        SendMessageW(hBtnWebSearch, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Bottom Row 3: NSFW / SafeSearch Unblock Checkbox (Disables SafeSearch on web + unblocks NSFW in metadata providers)
        g_hwndChkSafeSearch = CreateWindowExW(0, L"BUTTON", L"Disable SafeSearch (Unblock NSFW)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, topY + 580, 280, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_SAFE_SEARCH)), nullptr, nullptr);
        SendMessageW(g_hwndChkSafeSearch, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontSmall), TRUE);
        DarkMode::ApplyWindowTheme(g_hwndChkSafeSearch, g_isDarkMode);

        RefreshGameList(hwnd, g_initialSelectAppId);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* pmmi = reinterpret_cast<MINMAXINFO*>(lParam);
        pmmi->ptMinTrackSize.x = 1200;
        pmmi->ptMinTrackSize.y = 720;
        return 0;
    }

    case WM_ARTWORK_SEARCH_DONE: {
        auto* pResults = reinterpret_cast<std::vector<OnlineArtworkCandidate>*>(lParam);
        g_isSearchingArt = false;
        if (pResults != nullptr) {
            g_galleryCandidates = std::move(*pResults);
            delete pResults;
        }

        wchar_t buf[128] = { 0 };
        wsprintfW(buf, L"%u artwork candidates found \u2022 Click to select", static_cast<unsigned int>(g_galleryCandidates.size()));
        SetWindowTextW(g_hwndGalleryStatus, buf);

        if (g_hwndGalleryCanvas != nullptr) {
            InvalidateRect(g_hwndGalleryCanvas, nullptr, TRUE);
        }
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
        const int h = rcClient.bottom - rcClient.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        FillRect(memDC, &rcClient, g_hBgBrush);

        // Title Bar
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
        DrawTextW(memDC, L"Windows Game Explorer \u2014 Game Explorer Manager", -1, &rcText, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Window Controls Pill Pod
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

        // 3 Cards: Left (Library), Center (Metadata), Right (Online Gallery)
        constexpr int cardTop = TITLE_BAR_HEIGHT + 14;
        const int cardBottom = h - 16;

        RECT rcCol1 = { 16, cardTop, 268, cardBottom };
        RECT rcCol2 = { 278, cardTop, 718, cardBottom };
        RECT rcCol3 = { 728, cardTop, w - 16, cardBottom };

        auto drawCard = [&](const RECT& rc, const wchar_t* heading) {
            HBRUSH cardBrush = CreateSolidBrush(g_palette.cardBg);
            HPEN cardPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oB = SelectObject(memDC, cardBrush);
            HGDIOBJ oP = SelectObject(memDC, cardPen);
            RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(cardPen);
            DeleteObject(cardBrush);

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, g_palette.textPrimary);
            SelectObject(memDC, g_hFontBold);
            RECT rcHdr = { rc.left + 14, rc.top + 10, rc.right - 14, rc.top + 28 };
            DrawTextW(memDC, heading, -1, &rcHdr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        };

        drawCard(rcCol1, L"Installed & Custom Games");
        drawCard(rcCol2, L"Game Properties & Launch Setup");
        drawCard(rcCol3, L"Online Artwork Discovery & Gallery");

        // Render 2:3 Tall Box Art Preview Frame
        RECT rcBoxFrame = { PREVIEW_BOX_X, PREVIEW_BOX_Y, PREVIEW_BOX_X + PREVIEW_BOX_W, PREVIEW_BOX_Y + PREVIEW_BOX_H };
        HBRUSH boxBgBrush = CreateSolidBrush(g_isDarkMode ? RGB(24, 24, 24) : RGB(242, 242, 242));
        HPEN boxPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
        HGDIOBJ oBoxB = SelectObject(memDC, boxBgBrush);
        HGDIOBJ oBoxP = SelectObject(memDC, boxPen);
        RoundRect(memDC, rcBoxFrame.left, rcBoxFrame.top, rcBoxFrame.right, rcBoxFrame.bottom, 6, 6);
        SelectObject(memDC, oBoxP);
        SelectObject(memDC, oBoxB);
        DeleteObject(boxPen);
        DeleteObject(boxBgBrush);

        bool drewCover = false;
        const std::wstring coverPath = ReadEditString(g_hwndEditCoverPath);
        if (!coverPath.empty() && fs::exists(coverPath)) {
            Gdiplus::Graphics graphics(memDC);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::Image img(coverPath.c_str());
            if (img.GetLastStatus() == Gdiplus::Ok && img.GetWidth() > 0 && img.GetHeight() > 0) {
                graphics.DrawImage(&img, PREVIEW_BOX_X + 2, PREVIEW_BOX_Y + 2, PREVIEW_BOX_W - 4, PREVIEW_BOX_H - 4);
                drewCover = true;
            }
        }

        if (!drewCover) {
            SetTextColor(memDC, g_palette.textSecondary);
            SelectObject(memDC, g_hFont);
            RECT rcNoArt = rcBoxFrame;
            DrawTextW(memDC, L"2:3 Box Art\nPreview", -1, &rcNoArt, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

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

        auto drawBtn = [&](const wchar_t* text, bool accent, bool bold) {
            COLORREF btnBg = accent
                ? (isPressed ? g_palette.accentPressed : g_palette.accent)
                : (isPressed ? (g_isDarkMode ? RGB(65, 65, 65) : RGB(210, 210, 210))
                             : (g_isDarkMode ? RGB(52, 52, 52) : RGB(230, 230, 230)));
            COLORREF txtColor = accent ? g_palette.accentText : g_palette.textPrimary;
            COLORREF borderCol = accent ? g_palette.accentHover : g_palette.cardBorder;

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
            SelectObject(hdcBtn, bold ? g_hFontBold : g_hFont);
            DrawTextW(hdcBtn, text, -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        };

        switch (pDis->CtlID) {
        case IDC_BTN_ADD_GAME:        drawBtn(L"\U0001F4C1 Browse...", true, true); return TRUE;
        case IDC_BTN_REMOVE_GAME:     drawBtn(L"Remove", false, false); return TRUE;
        case IDC_BTN_REFRESH_LIST:    drawBtn(L"\u21BB", false, true); return TRUE;
        case IDC_BTN_BROWSE_EXE:      drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_BROWSE_COVER:    drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_BROWSE_HERO:     drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_BROWSE_DETAILS:  drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_BROWSE_ICON:     drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_SAVE_METADATA:   drawBtn(L"Save Metadata", true, true); return TRUE;
        case IDC_BTN_LAUNCH_SELECTED: drawBtn(L"\u25B6 Play Game", false, true); return TRUE;
        case IDC_BTN_OPEN_SHELL:      drawBtn(L"Open Folder", false, false); return TRUE;
        case IDC_BTN_RESET_CACHE:     drawBtn(L"\U0001F9F9 Reset Cache", false, false); return TRUE;
        case IDC_BTN_GALLERY_SEARCH:  drawBtn(L"\U0001F50D Search Providers", true, true); return TRUE;
        case IDC_BTN_GALLERY_WEB:     drawBtn(L"\U0001F310 Search Web", false, true); return TRUE;
        case IDC_BTN_APPLY_ARTWORK:   drawBtn(L"Apply Selected Artwork \u25BE", true, true); return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_LIST_GAMES && code == LBN_SELCHANGE) {
            g_selectedIndex = static_cast<int>(SendMessageW(g_hwndListGames, LB_GETCURSEL, 0, 0));
            PopulateEditorFromSelected(hwnd);
            return 0;
        }

        if (id == IDC_COMBO_GALLERY_TYPE && code == CBN_SELCHANGE) {
            TriggerArtworkSearch(hwnd, false);
            return 0;
        }

        if (id == IDC_COMBO_GALLERY_RES && code == CBN_SELCHANGE) {
            TriggerArtworkSearch(hwnd, false);
            return 0;
        }

        if (id == IDC_COMBO_WEB_FILTER && code == CBN_SELCHANGE) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_GALLERY_WEB, BN_CLICKED), 0);
            return 0;
        }

        if (id == IDC_CHK_SAFE_SEARCH && code == BN_CLICKED) {
            const bool safeOff = (SendMessageW(g_hwndChkSafeSearch, BM_GETCHECK, 0, 0) == BST_CHECKED);
            TriggerArtworkSearch(hwnd, false, L"", L"", L"", safeOff);
            return 0;
        }

        if (id == IDC_BTN_RESET_CACHE) {
            const int ret = MessageBoxW(hwnd,
                L"Are you sure you want to reset and purge the artwork cache?\n\n"
                L"This will delete all cached covers, heroes, icons, and thumbnails in:\n"
                L"%USERPROFILE%\\Game Explorer\\cache\\art\\\n\n"
                L"Fresh authentic images will be fetched cleanly from providers.",
                L"Reset Artwork Cache",
                MB_YESNO | MB_ICONQUESTION
            );
            if (ret == IDYES) {
                StorefrontScanner::ResetArtworkCache();
                if (g_hwndGalleryCanvas) {
                    SendMessageW(g_hwndGalleryCanvas, WM_APP + 62, 0, 0);
                }
                g_galleryCandidates.clear();
                g_selectedCandidateIdx = -1;
                g_galleryScrollY = 0;
                RECT rcPreview = { PREVIEW_BOX_X - 4, PREVIEW_BOX_Y - 4, PREVIEW_BOX_X + PREVIEW_BOX_W + 4, PREVIEW_BOX_Y + PREVIEW_BOX_H + 4 };
                InvalidateRect(hwnd, &rcPreview, FALSE);
                if (g_selectedIndex >= 0 && static_cast<size_t>(g_selectedIndex) < g_games.size()) {
                    TriggerArtworkSearch(hwnd, false);
                }
                MessageBoxW(hwnd, L"Artwork cache has been successfully reset.", L"Reset Artwork Cache", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        if (id == IDC_BTN_GALLERY_SEARCH) {
            TriggerArtworkSearch(hwnd, false);
            return 0;
        }

        if (id == IDC_BTN_GALLERY_WEB) {
            std::wstring webQuery = ReadEditString(g_hwndWebSearchQuery);
            if (webQuery.empty()) {
                webQuery = ReadEditString(g_hwndGalleryQuery);
            }
            if (webQuery.empty() && g_selectedIndex >= 0 && static_cast<size_t>(g_selectedIndex) < g_games.size()) {
                webQuery = g_games[static_cast<size_t>(g_selectedIndex)].title;
                SetWindowTextW(g_hwndWebSearchQuery, webQuery.c_str());
            }
            if (webQuery.empty()) return 0;

            std::wstring webFilter = L"All";
            if (g_hwndComboWebFilter != nullptr) {
                wchar_t buf[64] = { 0 };
                GetWindowTextW(g_hwndComboWebFilter, buf, 64);
                std::wstring s = buf;
                if (s.find(L"Box Art") != std::wstring::npos) webFilter = L"Cover";
                else if (s.find(L"Hero") != std::wstring::npos) webFilter = L"Hero";
                else if (s.find(L"Patreon") != std::wstring::npos) webFilter = L"Patreon";
                else if (s.find(L"Icon") != std::wstring::npos) webFilter = L"Icon";
            }

            const bool safeOff = (g_hwndChkSafeSearch != nullptr && SendMessageW(g_hwndChkSafeSearch, BM_GETCHECK, 0, 0) == BST_CHECKED);
            TriggerArtworkSearch(hwnd, true, webQuery, webFilter, L"", safeOff);
            return 0;
        }

        if (id == IDC_BTN_APPLY_ARTWORK) {
            if (g_selectedCandidateIdx < 0 || static_cast<size_t>(g_selectedCandidateIdx) >= g_galleryCandidates.size()) {
                MessageBoxW(hwnd, L"Please select an artwork image from the gallery first.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            RECT rcBtn;
            GetWindowRect(g_hwndBtnApplyArt, &rcBtn);

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_APPLY_COVER,   L"Apply as Box Art (~2:3 Cover)");
            AppendMenuW(hMenu, MF_STRING, IDM_APPLY_HERO,    L"Apply as Hero Banner (~3.1:1 Widescreen)");
            AppendMenuW(hMenu, MF_STRING, IDM_APPLY_DETAILS, L"Apply as Details Art");
            AppendMenuW(hMenu, MF_STRING, IDM_APPLY_ICON,    L"Apply as Icon / Logo");

            const int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, rcBtn.left, rcBtn.bottom + 2, 0, hwnd, nullptr);
            DestroyMenu(hMenu);

            if (cmd >= IDM_APPLY_COVER && cmd <= IDM_APPLY_ICON) {
                ApplySelectedArtwork(hwnd, cmd);
            }
            return 0;
        }

        if (id == IDC_BTN_ADD_GAME) {
            std::wstring chosenExe;
            if (BrowseForFile(
                hwnd,
                L"Select Local Game Executable, Shortcut, or ROM",
                L"Game Executables & Shortcuts (*.exe;*.lnk;*.bat;*.cmd)\0*.exe;*.lnk;*.bat;*.cmd\0All Files (*.*)\0*.*\0",
                chosenExe
            )) {
                if (StorefrontScanner::AddCustomLocalGame(chosenExe)) {
                    EnumWindows(BroadcastRefreshToShellViews, 0);
                    RefreshGameList(hwnd);
                    if (!g_games.empty()) {
                        g_selectedIndex = static_cast<int>(g_games.size() - 1);
                        SendMessageW(g_hwndListGames, LB_SETCURSEL, static_cast<WPARAM>(g_selectedIndex), 0);
                        PopulateEditorFromSelected(hwnd);
                    }
                }
            }
            return 0;
        }

        if (id == IDC_BTN_REMOVE_GAME) {
            if (g_selectedIndex >= 0 && static_cast<size_t>(g_selectedIndex) < g_games.size()) {
                const auto& g = g_games[static_cast<size_t>(g_selectedIndex)];
                if (StorefrontScanner::RemoveCustomLocalGame(g.appId)) {
                    EnumWindows(BroadcastRefreshToShellViews, 0);
                    RefreshGameList(hwnd);
                } else {
                    MessageBoxW(hwnd, L"Storefront games are managed by their provider client. Only custom local games or saved metadata overrides can be removed.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }

        if (id == IDC_BTN_REFRESH_LIST) {
            RefreshGameList(hwnd);
            return 0;
        }

        if (id == IDC_BTN_BROWSE_EXE) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Game Executable", L"Executables (*.exe;*.bat;*.cmd;*.lnk)\0*.exe;*.bat;*.cmd;*.lnk\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditExePath, path.c_str());
                if (ReadEditString(g_hwndEditWorkDir).empty()) {
                    SetWindowTextW(g_hwndEditWorkDir, fs::path(path).parent_path().wstring().c_str());
                }
            }
            return 0;
        }

        if (id == IDC_BTN_BROWSE_COVER) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Vertical Box Art (~2:3 Cover Image)", L"Image Files (*.png;*.jpg;*.jpeg;*.webp;*.bmp)\0*.png;*.jpg;*.jpeg;*.webp;*.bmp\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditCoverPath, path.c_str());
                RECT rcPreview = { PREVIEW_BOX_X - 4, PREVIEW_BOX_Y - 4, PREVIEW_BOX_X + PREVIEW_BOX_W + 4, PREVIEW_BOX_Y + PREVIEW_BOX_H + 4 };
                InvalidateRect(hwnd, &rcPreview, FALSE);
            }
            return 0;
        }

        if (id == IDC_BTN_BROWSE_HERO) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Widescreen Hero Banner (~3.1:1 / 16:9 Banner)", L"Image Files (*.png;*.jpg;*.jpeg;*.webp;*.bmp)\0*.png;*.jpg;*.jpeg;*.webp;*.bmp\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditHeroPath, path.c_str());
            }
            return 0;
        }

        if (id == IDC_BTN_BROWSE_DETAILS) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Details Pane Preview Image or Animated GIF", L"Image Files (*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp)\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditDetailsPath, path.c_str());
            }
            return 0;
        }

        if (id == IDC_BTN_BROWSE_ICON) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Game Icon or Executable", L"Icons & Images (*.ico;*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.exe)\0*.ico;*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.exe\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditIconPath, path.c_str());
            }
            return 0;
        }

        if (id == IDC_BTN_SAVE_METADATA) {
            SaveCurrentGameEdits(hwnd);
            return 0;
        }

        if (id == IDC_BTN_LAUNCH_SELECTED) {
            if (g_selectedIndex >= 0 && static_cast<size_t>(g_selectedIndex) < g_games.size()) {
                PlatformLauncher::Launch(hwnd, g_games[static_cast<size_t>(g_selectedIndex)]);
            }
            return 0;
        }

        if (id == IDC_BTN_OPEN_SHELL) {
            const std::wstring cmd = std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER;
            ShellExecuteW(hwnd, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        auto hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, g_palette.cardBg);
        SetTextColor(hdc, g_palette.textPrimary);
        return reinterpret_cast<LRESULT>(g_hCardBrush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        auto hdc = reinterpret_cast<HDC>(wParam);
        const COLORREF editBg = g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255);
        SetBkColor(hdc, editBg);
        SetTextColor(hdc, g_palette.textPrimary);
        return reinterpret_cast<LRESULT>(g_hEditBrush);
    }

    case WM_DESTROY:
        if (g_hFont != nullptr) DeleteObject(g_hFont);
        if (g_hFontBold != nullptr) DeleteObject(g_hFontBold);
        if (g_hFontSmall != nullptr) DeleteObject(g_hFontSmall);
        if (g_hBgBrush != nullptr) DeleteObject(g_hBgBrush);
        if (g_hCardBrush != nullptr) DeleteObject(g_hCardBrush);
        if (g_hEditBrush != nullptr) DeleteObject(g_hEditBrush);
        if (g_hAppIcon != nullptr) DestroyIcon(g_hAppIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR lpCmdLine, int nCmdShow) {
    if (lpCmdLine != nullptr && lpCmdLine[0] != L'\0') {
        g_initialSelectAppId = lpCmdLine;
    }
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    DarkMode::Initialize();
    g_isDarkMode = DarkMode::IsSystemDarkMode();
    g_palette = DarkMode::GetPalette(g_isDarkMode);
    g_hBgBrush = CreateSolidBrush(g_palette.canvasBg);
    g_hCardBrush = CreateSolidBrush(g_palette.cardBg);
    g_hEditBrush = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255));

    // Register Gallery Canvas custom window class
    WNDCLASSEXW wcg{};
    wcg.cbSize = sizeof(wcg);
    wcg.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcg.lpfnWndProc = GalleryCanvasWndProc;
    wcg.hInstance = hInstance;
    wcg.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcg.hbrBackground = nullptr;
    wcg.lpszClassName = L"GameExplorerGalleryCanvas";
    RegisterClassExW(&wcg);

    const wchar_t* CLASS_NAME = L"GameExplorerManagerWnd";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ManagerWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    constexpr int winW = 1260;
    constexpr int winH = 760;
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (screenW > winW) ? ((screenW - winW) / 2) : CW_USEDEFAULT;
    const int posY = (screenH > winH) ? ((screenH - winH) / 2) : CW_USEDEFAULT;

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, Localization::GetStrings().managerWindowTitle,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        posX, posY, winW, winH,
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

    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
    }

    return static_cast<int>(msg.wParam);
}
