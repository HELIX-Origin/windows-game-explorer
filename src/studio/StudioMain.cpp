#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <objbase.h>
#include <array>
#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>

#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/Localization.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/StudioPages.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

using namespace GameExplorer;

namespace {

constexpr int TITLE_BAR_HEIGHT = 38;
constexpr int TAB_STRIP_HEIGHT = 46;
constexpr int CONTENT_TOP = TITLE_BAR_HEIGHT + TAB_STRIP_HEIGHT;
constexpr int TAB_COUNT = 3;
constexpr int TAB_TOP_PADDING = 7;
constexpr int TAB_HEIGHT = 32;
constexpr int TAB_PADDING_X = 18;
constexpr int TAB_GAP = 6;
constexpr int TAB_STRIP_LEFT = 20;

constexpr int MIN_WINDOW_WIDTH = 1200;
// 84px of chrome (title bar + tab strip) plus the tallest page content: the Configuration page's
// fluid card stack ends with the save row at y = 799, so 836px of content (920px window height)
// keeps every control fully visible with the same breathing room as the card gaps.
constexpr int MIN_WINDOW_HEIGHT = 920;

constexpr const wchar_t* STUDIO_WINDOW_CLASS = L"GameExplorerStudioWnd";
constexpr const wchar_t* STUDIO_WINDOW_TITLE = L"Game Explorer Studio";

enum class TitleButton { None, Min, Max, Close };

HWND g_hwnd = nullptr;
std::array<HWND, TAB_COUNT> g_pages{};
int g_activeTab = 0;
int g_initialTab = 0;
int g_hoverTab = -1;
TitleButton g_hoverBtn = TitleButton::None;
TitleButton g_pressedBtn = TitleButton::None;
bool g_trackingMouse = false;
HICON g_hAppIcon = nullptr;
HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
bool g_isDarkMode = false;
ThemePalette g_palette{};
HBRUSH g_hBgBrush = nullptr;

const wchar_t* TabLabel(int index) {
    const auto& strings = Localization::GetStrings();
    switch (index) {
        case 0: return strings.studioTabManager;
        case 1: return strings.studioTabCatalogue;
        case 2: return strings.studioTabConfig;
        default: return L"";
    }
}

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
    const POINT pt{ x, y };
    RECT rClose = GetCloseButtonRect(w);
    if (PtInRect(&rClose, pt) == TRUE) return TitleButton::Close;
    RECT rMax = GetMaxButtonRect(w);
    if (PtInRect(&rMax, pt) == TRUE) return TitleButton::Max;
    RECT rMin = GetMinButtonRect(w);
    if (PtInRect(&rMin, pt) == TRUE) return TitleButton::Min;
    return TitleButton::None;
}

/**
 * Tab rectangles are recomputed from the live font so the strip always matches what is painted.
 * The caller supplies the HDC so measuring and painting cannot diverge.
 */
void GetTabRects(HDC hdc, std::array<RECT, TAB_COUNT>& rects) {
    HGDIOBJ previous = SelectObject(hdc, g_hFontBold);
    int x = TAB_STRIP_LEFT;
    for (int i = 0; i < TAB_COUNT; ++i) {
        const wchar_t* label = TabLabel(i);
        SIZE size{};
        GetTextExtentPoint32W(hdc, label, static_cast<int>(std::wcslen(label)), &size);
        const int width = size.cx + TAB_PADDING_X * 2;
        rects[static_cast<size_t>(i)] = RECT{ x, TITLE_BAR_HEIGHT + TAB_TOP_PADDING,
                                              x + width, TITLE_BAR_HEIGHT + TAB_TOP_PADDING + TAB_HEIGHT };
        x += width + TAB_GAP;
    }
    SelectObject(hdc, previous);
}

int HitTestTab(int x, int y) {
    if (y < TITLE_BAR_HEIGHT || y >= CONTENT_TOP) return -1;
    HDC hdc = GetDC(g_hwnd);
    if (hdc == nullptr) return -1;
    std::array<RECT, TAB_COUNT> rects{};
    GetTabRects(hdc, rects);
    ReleaseDC(g_hwnd, hdc);

    const POINT pt{ x, y };
    for (int i = 0; i < TAB_COUNT; ++i) {
        if (PtInRect(&rects[static_cast<size_t>(i)], pt) == TRUE) return i;
    }
    return -1;
}

void LayoutPages(int w, int h) {
    const int contentHeight = h - CONTENT_TOP;
    if (w <= 0 || contentHeight <= 0) return;

    // Every page owns a fluid layout and fills the whole content area, so resizing the Studio
    // window re-flows Manager, Catalogue and Configuration identically.
    for (HWND page : g_pages) {
        if (page != nullptr && IsWindow(page)) {
            SetWindowPos(page, nullptr, 0, CONTENT_TOP, w, contentHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

void SwitchTab(int index, bool moveFocus) {
    if (index < 0 || index >= TAB_COUNT) return;
    g_activeTab = index;

    for (int i = 0; i < TAB_COUNT; ++i) {
        HWND page = g_pages[static_cast<size_t>(i)];
        if (page == nullptr || !IsWindow(page)) continue;
        ShowWindow(page, i == index ? SW_SHOW : SW_HIDE);
    }

    if (moveFocus) {
        HWND page = g_pages[static_cast<size_t>(index)];
        if (page != nullptr && IsWindow(page)) {
            SetFocus(page);
        }
    }

    RECT rcTabStrip = { 0, TITLE_BAR_HEIGHT, 4096, CONTENT_TOP };
    InvalidateRect(g_hwnd, &rcTabStrip, FALSE);
}

void DrawTitleBarButtons(HDC hdc, int w) {
    const RECT rcPill = GetPillRect(w);
    HBRUSH pillBg = CreateSolidBrush(g_palette.chromePillBg);
    HPEN pillPen = CreatePen(PS_SOLID, 1, g_palette.divider);
    HGDIOBJ previousBrush = SelectObject(hdc, pillBg);
    HGDIOBJ previousPen = SelectObject(hdc, pillPen);
    RoundRect(hdc, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 6, 6);
    SelectObject(hdc, previousPen);
    SelectObject(hdc, previousBrush);
    DeleteObject(pillPen);
    DeleteObject(pillBg);

    const RECT rMin = GetMinButtonRect(w);
    const RECT rMax = GetMaxButtonRect(w);
    const RECT rClose = GetCloseButtonRect(w);

    auto drawHoverBackground = [&](const RECT& rect, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    };

    if (g_hoverBtn == TitleButton::Min || g_pressedBtn == TitleButton::Min) {
        drawHoverBackground(rMin, g_palette.chromeHoverBg);
    }
    {
        const int cx = (rMin.left + rMin.right) / 2;
        const int cy = (rMin.top + rMin.bottom) / 2;
        HPEN glyphPen = CreatePen(PS_SOLID, 1, g_palette.textPrimary);
        HGDIOBJ oldPen = SelectObject(hdc, glyphPen);
        MoveToEx(hdc, cx - 4, cy, nullptr);
        LineTo(hdc, cx + 5, cy);
        SelectObject(hdc, oldPen);
        DeleteObject(glyphPen);
    }

    const bool isMaximized = IsZoomed(g_hwnd) == TRUE;
    if (g_hoverBtn == TitleButton::Max || g_pressedBtn == TitleButton::Max) {
        drawHoverBackground(rMax, g_palette.chromeHoverBg);
    }
    {
        const int cx = (rMax.left + rMax.right) / 2;
        const int cy = (rMax.top + rMax.bottom) / 2;
        HPEN glyphPen = CreatePen(PS_SOLID, 1, g_palette.textPrimary);
        HGDIOBJ oldPen = SelectObject(hdc, glyphPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        if (isMaximized) {
            // Restore glyph: two offset squares.
            Rectangle(hdc, cx - 4, cy - 4, cx + 3, cy + 3);
            Rectangle(hdc, cx - 1, cy - 1, cx + 6, cy + 6);
        } else {
            Rectangle(hdc, cx - 4, cy - 4, cx + 5, cy + 5);
        }
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(glyphPen);
    }

    const bool closeHover = (g_hoverBtn == TitleButton::Close);
    const bool closePressed = (g_pressedBtn == TitleButton::Close);
    if (closeHover || closePressed) {
        drawHoverBackground(rClose, closePressed ? g_palette.chromeClosePressedBg : g_palette.chromeCloseHoverBg);
    }
    {
        const int cx = (rClose.left + rClose.right) / 2;
        const int cy = (rClose.top + rClose.bottom) / 2;
        const COLORREF glyphColor = (closeHover || closePressed) ? g_palette.chromeOnCloseText : g_palette.textPrimary;
        HPEN glyphPen = CreatePen(PS_SOLID, 1, glyphColor);
        HGDIOBJ oldPen = SelectObject(hdc, glyphPen);
        MoveToEx(hdc, cx - 4, cy - 4, nullptr);
        LineTo(hdc, cx + 5, cy + 5);
        MoveToEx(hdc, cx + 4, cy - 4, nullptr);
        LineTo(hdc, cx - 5, cy + 5);
        SelectObject(hdc, oldPen);
        DeleteObject(glyphPen);
    }
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc == nullptr) return;

    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int w = rcClient.right - rcClient.left;
    const int h = rcClient.bottom - rcClient.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    FillRect(memDC, &rcClient, g_hBgBrush);

    // ---- Title bar -------------------------------------------------------
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
    DrawTextW(memDC, STUDIO_WINDOW_TITLE, -1, &rcText,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    DrawTitleBarButtons(memDC, w);

    // ---- Tab strip -------------------------------------------------------
    RECT rcTabStrip = { 0, TITLE_BAR_HEIGHT, w, CONTENT_TOP };
    HBRUSH stripBrush = CreateSolidBrush(g_palette.drawerBg);
    FillRect(memDC, &rcTabStrip, stripBrush);
    DeleteObject(stripBrush);

    HPEN stripDivider = CreatePen(PS_SOLID, 1, g_palette.divider);
    oldPen = SelectObject(memDC, stripDivider);
    MoveToEx(memDC, 0, CONTENT_TOP - 1, nullptr);
    LineTo(memDC, w, CONTENT_TOP - 1);
    SelectObject(memDC, oldPen);
    DeleteObject(stripDivider);

    std::array<RECT, TAB_COUNT> rects{};
    GetTabRects(memDC, rects);

    for (int i = 0; i < TAB_COUNT; ++i) {
        const RECT rc = rects[static_cast<size_t>(i)];
        const bool isActive = (i == g_activeTab);
        const bool isHover = (i == g_hoverTab);

        if (isActive) {
            HBRUSH activeBrush = CreateSolidBrush(g_palette.cardBg);
            HPEN activePen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oldBrush = SelectObject(memDC, activeBrush);
            HGDIOBJ old = SelectObject(memDC, activePen);
            RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            SelectObject(memDC, old);
            SelectObject(memDC, oldBrush);
            DeleteObject(activePen);
            DeleteObject(activeBrush);

            HBRUSH accentBrush = CreateSolidBrush(g_palette.accent);
            RECT rcIndicator = { rc.left + 10, rc.bottom - 5, rc.right - 10, rc.bottom - 2 };
            FillRect(memDC, &rcIndicator, accentBrush);
            DeleteObject(accentBrush);
        } else if (isHover) {
            HBRUSH hoverBrush = CreateSolidBrush(g_palette.buttonBg);
            HGDIOBJ oldBrush = SelectObject(memDC, hoverBrush);
            HPEN hoverPen = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ old = SelectObject(memDC, hoverPen);
            RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            SelectObject(memDC, old);
            SelectObject(memDC, oldBrush);
            DeleteObject(hoverPen);
            DeleteObject(hoverBrush);
        }

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, isActive ? g_palette.textPrimary : g_palette.textSecondary);
        SelectObject(memDC, isActive ? g_hFontBold : g_hFont);
        RECT rcLabel = rc;
        DrawTextW(memDC, TabLabel(i), -1, &rcLabel,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK StudioWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hwnd = hwnd;

        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_hFontBold = CreateFontIndirectW(&ncm.lfMessageFont);

        g_hAppIcon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(100), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));

        g_pages[0] = Studio::CreateManagerPage(hwnd);
        g_pages[1] = Studio::CreateCataloguePage(hwnd);
        g_pages[2] = Studio::CreateConfigPage(hwnd);

        for (int i = 0; i < TAB_COUNT; ++i) {
            if (g_pages[static_cast<size_t>(i)] == nullptr) {
                MessageBoxW(hwnd, L"A Game Explorer Studio page could not be created.", STUDIO_WINDOW_TITLE,
                            MB_OK | MB_ICONERROR);
                return -1;
            }
        }

        SwitchTab(g_initialTab, false);
        return 0;
    }

    case WM_SIZE: {
        const int w = LOWORD(lParam);
        const int h = HIWORD(lParam);
        if (w > 0 && h > 0) {
            LayoutPages(w, h);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* pmmi = reinterpret_cast<MINMAXINFO*>(lParam);

        // Both pages have fixed minimum geometry, so the frame refuses to shrink past it:
        // the Manager needs 1200px of horizontal column space and the Configuration page needs
        // its 801px content column plus a bottom margin.
        pmmi->ptMinTrackSize.x = MIN_WINDOW_WIDTH;
        pmmi->ptMinTrackSize.y = MIN_WINDOW_HEIGHT;

        // Clamp maximise to the work area: the window is a WS_POPUP with a custom caption, so
        // without this it would cover the taskbar and clip the tab strip on smaller displays.
        RECT work{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) != FALSE) {
            pmmi->ptMaxPosition.x = work.left;
            pmmi->ptMaxPosition.y = work.top;
            pmmi->ptMaxSize.x = work.right - work.left;
            pmmi->ptMaxSize.y = work.bottom - work.top;
        }
        return 0;
    }

    case Studio::WM_SELECT_TAB:
        SwitchTab(static_cast<int>(wParam), true);
        return 0;

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

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (pt.y >= 0 && pt.y < TITLE_BAR_HEIGHT) {
            RECT rcPill = GetPillRect(w);
            if (PtInRect(&rcPill, pt) == TRUE) return HTCLIENT;
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

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;

        const TitleButton newHover = HitTestTitleButtons(x, y, w);
        if (newHover != g_hoverBtn) {
            g_hoverBtn = newHover;
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);
        }

        const int newHoverTab = HitTestTab(x, y);
        if (newHoverTab != g_hoverTab) {
            g_hoverTab = newHoverTab;
            RECT rcTabStrip = { 0, TITLE_BAR_HEIGHT, w, CONTENT_TOP };
            InvalidateRect(hwnd, &rcTabStrip, FALSE);
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
        bool changed = false;
        if (g_hoverBtn != TitleButton::None) {
            g_hoverBtn = TitleButton::None;
            changed = true;
        }
        if (g_hoverTab != -1) {
            g_hoverTab = -1;
            changed = true;
        }
        if (changed) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;

        const TitleButton button = HitTestTitleButtons(x, y, w);
        if (button != TitleButton::None) {
            g_pressedBtn = button;
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);
            return 0;
        }

        const int tab = HitTestTab(x, y);
        if (tab >= 0) {
            SwitchTab(tab, true);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;

        const TitleButton pressed = g_pressedBtn;
        if (pressed != TitleButton::None) {
            g_pressedBtn = TitleButton::None;
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);

            if (HitTestTitleButtons(x, y, w) == pressed) {
                if (pressed == TitleButton::Close) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                }
                if (pressed == TitleButton::Max) {
                    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                    return 0;
                }
                if (pressed == TitleButton::Min) {
                    ShowWindow(hwnd, SW_MINIMIZE);
                    return 0;
                }
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_hFont != nullptr) DeleteObject(g_hFont);
        if (g_hFontBold != nullptr) DeleteObject(g_hFontBold);
        if (g_hBgBrush != nullptr) DeleteObject(g_hBgBrush);
        if (g_hAppIcon != nullptr) DestroyIcon(g_hAppIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int TabFromSwitch(const std::wstring& value) {
    if (value == L"manager" || value == L"0") return 0;
    if (value == L"catalogue" || value == L"catalog" || value == L"1") return 1;
    if (value == L"config" || value == L"configuration" || value == L"2") return 2;
    return -1;
}

/** Command-line actions that never open the window (kept for installers and shortcuts). */
enum class CliAction { None, Register, Unregister, ResetCache };

struct CommandLineOptions {
    int tab = 0;
    std::wstring gameAppId;
    CliAction action = CliAction::None;
};

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

std::filesystem::path FindShellDllPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path dir = std::filesystem::path(exePath).parent_path();
    if (std::filesystem::exists(dir / L"GameExplorerShell.dll")) {
        return dir / L"GameExplorerShell.dll";
    }
    if (std::filesystem::exists(dir / L"build" / L"Release" / L"GameExplorerShell.dll")) {
        return dir / L"build" / L"Release" / L"GameExplorerShell.dll";
    }
    if (std::filesystem::exists(dir / L"Release" / L"GameExplorerShell.dll")) {
        return dir / L"Release" / L"GameExplorerShell.dll";
    }
    return {};
}

/**
 * Registers or unregisters the shell namespace extension. Shell registration is machine-wide, so
 * the process relaunches itself elevated once and waits for that child to finish.
 */
int RunShellRegistration(bool doRegister) {
    if (!IsRunningElevated()) {
        const wchar_t* arg = doRegister ? L"--register" : L"--unregister";
        return ElevateSelf(arg) ? 0 : 1;
    }

    const std::filesystem::path dllPath = FindShellDllPath();
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

CommandLineOptions ParseCommandLine() {
    CommandLineOptions options;

    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return options;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i];
        constexpr std::wstring_view tabPrefix = L"--tab=";
        constexpr std::wstring_view gamePrefix = L"--game=";
        if (arg == L"--register" || arg == L"/register" || arg == L"-register") {
            options.action = CliAction::Register;
        } else if (arg == L"--unregister" || arg == L"/unregister" || arg == L"-unregister") {
            options.action = CliAction::Unregister;
        } else if (arg == L"--reset-cache" || arg == L"/reset-cache" || arg == L"-reset-cache") {
            options.action = CliAction::ResetCache;
        } else if (arg == L"--manager") {
            options.tab = 0;
        } else if (arg == L"--catalogue" || arg == L"--catalog") {
            options.tab = 1;
        } else if (arg == L"--config") {
            options.tab = 2;
        } else if (arg.size() > tabPrefix.size() && arg.substr(0, tabPrefix.size()) == tabPrefix) {
            const int tab = TabFromSwitch(std::wstring(arg.substr(tabPrefix.size())));
            if (tab >= 0) options.tab = tab;
        } else if (arg.size() > gamePrefix.size() && arg.substr(0, gamePrefix.size()) == gamePrefix) {
            // Shell context-menu deep link: open the Library Manager on one specific game.
            options.gameAppId = std::wstring(arg.substr(gamePrefix.size()));
            options.tab = 0;
        } else if (!arg.empty() && arg.front() != L'-') {
            // Legacy direct launch (`Studio.exe <appId>`) used by older shell builds.
            options.gameAppId = std::wstring(arg);
            options.tab = 0;
        }
    }

    LocalFree(argv);
    return options;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*lpCmdLine*/, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const CommandLineOptions options = ParseCommandLine();

    // Registration and cache maintenance never create a window: they are installer/shortcut
    // actions that must work even when no desktop session is interactive.
    if (options.action != CliAction::None) {
        int exitCode = 0;
        if (options.action == CliAction::ResetCache) {
            exitCode = StorefrontScanner::ResetArtworkCache() ? 0 : 1;
        } else {
            exitCode = RunShellRegistration(options.action == CliAction::Register);
        }
        CoUninitialize();
        return exitCode;
    }

    g_initialTab = options.tab;
    if (!options.gameAppId.empty()) {
        Studio::SetInitialManagerSelection(options.gameAppId.c_str());
    }

    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    DarkMode::Initialize();
    g_isDarkMode = DarkMode::IsSystemDarkMode();
    g_palette = DarkMode::GetPalette(g_isDarkMode);
    g_hBgBrush = CreateSolidBrush(g_palette.canvasBg);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StudioWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = STUDIO_WINDOW_CLASS;
    if (RegisterClassExW(&wc) == 0) {
        CoUninitialize();
        return 1;
    }

    constexpr int desiredW = 1360;
    // Matches the enforced minimum: the tallest page (Configuration) needs 836px of content on
    // top of the 84px chrome, and the initial size must never open below that.
    constexpr int desiredH = MIN_WINDOW_HEIGHT;

    RECT work{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // Never open larger than the usable desktop, otherwise the tab strip or the page action
    // buttons would start off-screen on small or taskbar-heavy displays.
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int winW = (std::min)(desiredW, workW);
    const int winH = (std::min)(desiredH, workH);
    const int posX = work.left + ((workW - winW) / 2);
    const int posY = work.top + ((workH - winH) / 2);

    HWND hwnd = CreateWindowExW(
        WS_EX_CONTROLPARENT, STUDIO_WINDOW_CLASS, STUDIO_WINDOW_TITLE,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        posX, posY, winW, winH,
        nullptr, nullptr, hInstance, nullptr);

    if (hwnd == nullptr) {
        CoUninitialize();
        return 1;
    }

    const int roundPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPreference, sizeof(roundPreference));

    const BOOL isDark = g_isDarkMode ? TRUE : FALSE;
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
        if (msg.hwnd != hwnd && IsDialogMessageW(hwnd, &msg) != FALSE) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
