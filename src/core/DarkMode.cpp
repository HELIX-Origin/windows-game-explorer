#include "GameExplorerCore/DarkMode.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace GameExplorer {

namespace {

COLORREF AdjustColor(COLORREF c, float factor) {
    int r = static_cast<int>(GetRValue(c) * factor);
    int g = static_cast<int>(GetGValue(c) * factor);
    int b = static_cast<int>(GetBValue(c) * factor);
    return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
}

} // namespace

using PFN_ShouldAppsUseDarkMode = bool(WINAPI*)();
using PFN_SetPreferredAppMode = int(WINAPI*)(int);
using PFN_AllowDarkModeForWindow = bool(WINAPI*)(HWND, bool);

void DarkMode::Initialize() {
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxTheme == nullptr) {
        hUxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (hUxTheme != nullptr) {
        auto fnSetPreferredAppMode = reinterpret_cast<PFN_SetPreferredAppMode>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
        if (fnSetPreferredAppMode != nullptr) {
            fnSetPreferredAppMode(2); // 2 = AllowDark/ForceDark
        }
    }
}

bool DarkMode::IsSystemDarkMode() {
    // 1. Authoritative check: Windows Personalize registry key
    HKEY hKey = nullptr;
    const LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey
    );
    if (status == ERROR_SUCCESS) {
        DWORD appsUseLightTheme = 1;
        DWORD dataSize = sizeof(appsUseLightTheme);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<BYTE*>(&appsUseLightTheme), &dataSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (appsUseLightTheme == 0);
        }
        RegCloseKey(hKey);
    }

    // 2. Fallback to UxTheme ordinal 132
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxTheme != nullptr) {
        auto fnShouldAppsUseDarkMode = reinterpret_cast<PFN_ShouldAppsUseDarkMode>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(132)));
        if (fnShouldAppsUseDarkMode != nullptr) {
            return fnShouldAppsUseDarkMode();
        }
    }

    return false;
}

bool DarkMode::IsWindows11OrGreater() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[64]{};
        DWORD cb = sizeof(buf);
        if (RegQueryValueExW(hKey, L"CurrentBuildNumber", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &cb) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            const int build = _wtoi(buf);
            return build >= 22000;
        }
        RegCloseKey(hKey);
    }
    return true; // Default to modern Windows 11
}

void DarkMode::ApplyWindowTheme(HWND hwnd, bool darkMode) {
    if (hwnd == nullptr) return;

    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxTheme != nullptr) {
        auto fnAllowDarkMode = reinterpret_cast<PFN_AllowDarkModeForWindow>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        if (fnAllowDarkMode != nullptr) {
            fnAllowDarkMode(hwnd, darkMode);
        }
    }

    // Apply DWM immersive dark mode attribute
    BOOL value = darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

    // Apply UxTheme DarkMode_Explorer or Explorer theme
    SetWindowTheme(hwnd, darkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

COLORREF DarkMode::GetSystemAccentColor() {
    // 1. Authoritative check: Explorer AccentPalette (Windows 10/11 system accent palette, index 3 is control accent)
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        BYTE palette[32]{};
        DWORD dwSize = sizeof(palette);
        if (RegQueryValueExW(hKey, L"AccentPalette", nullptr, nullptr, palette, &dwSize) == ERROR_SUCCESS && dwSize >= 16) {
            RegCloseKey(hKey);
            return RGB(palette[12], palette[13], palette[14]);
        }
        RegCloseKey(hKey);
    }

    // 2. Fallback check: DWM AccentColor (format 0xAABBGGRR)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwColor = 0;
        DWORD dwSize = sizeof(dwColor);
        if (RegQueryValueExW(hKey, L"AccentColor", nullptr, nullptr, reinterpret_cast<BYTE*>(&dwColor), &dwSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            const BYTE r = static_cast<BYTE>(dwColor & 0xFF);
            const BYTE g = static_cast<BYTE>((dwColor >> 8) & 0xFF);
            const BYTE b = static_cast<BYTE>((dwColor >> 16) & 0xFF);
            return RGB(r, g, b);
        }
        RegCloseKey(hKey);
    }

    // 2. Fallback to DwmGetColorizationColor
    DWORD colorization = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))) {
        const BYTE r = static_cast<BYTE>((colorization >> 16) & 0xFF);
        const BYTE g = static_cast<BYTE>((colorization >> 8) & 0xFF);
        const BYTE b = static_cast<BYTE>(colorization & 0xFF);
        return RGB(r, g, b);
    }

    // 3. Fallback to Windows 11 default Fluent blue
    return RGB(0, 120, 215);
}

ThemePalette DarkMode::GetPalette(bool isDarkMode) {
    const COLORREF accent = GetSystemAccentColor();
    const COLORREF accentHover = AdjustColor(accent, 1.15f);
    const COLORREF accentPressed = AdjustColor(accent, 0.85f);
    const double lum = 0.299 * GetRValue(accent) + 0.587 * GetGValue(accent) + 0.114 * GetBValue(accent);
    const COLORREF accentText = (lum > 155.0) ? RGB(0, 0, 0) : RGB(255, 255, 255);

    if (isDarkMode) {
        return ThemePalette{
            RGB(32, 32, 32),     // canvasBg (Windows 11 dark explorer #202020)
            RGB(40, 40, 40),     // drawerBg (#282828)
            RGB(45, 45, 45),     // cardBg (#2D2D2D)
            RGB(62, 62, 62),     // cardBorder (#3E3E3E)
            RGB(43, 43, 43),     // fieldBg
            RGB(72, 72, 72),     // fieldBorder
            RGB(38, 55, 78),     // cardSelectedBg (Fluent dark slate)
            accentHover,         // cardSelectedBorder (Accent highlight)
            RGB(240, 240, 240),  // textPrimary (#F0F0F0)
            RGB(160, 160, 160),  // textSecondary (#A0A0A0)
            RGB(52, 52, 52),     // divider (#343434)
            accent,              // buttonBg (System Accent Color)
            accentText,          // buttonText
            accent,              // accent
            accentHover,         // accentHover
            accentPressed,       // accentPressed
            accentText,          // accentText
            RGB(32, 32, 32),     // chromePillBg
            RGB(52, 52, 52),     // chromeHoverBg
            RGB(196, 43, 28),    // chromeCloseHoverBg
            RGB(158, 33, 21),    // chromeClosePressedBg
            RGB(255, 255, 255),  // chromeOnCloseText
            RGB(38, 38, 38),     // scrollTrack
            RGB(112, 112, 112),  // scrollThumb
            RGB(148, 148, 148),  // scrollThumbHover
            accent               // scrollThumbPressed
        };
    } else {
        return ThemePalette{
            RGB(255, 255, 255),  // canvasBg (Windows 11 light explorer #FFFFFF)
            RGB(243, 243, 243),  // drawerBg (#F3F3F3)
            RGB(248, 248, 248),  // cardBg (#F8F8F8)
            RGB(226, 226, 226),  // cardBorder (#E2E2E2)
            RGB(255, 255, 255),  // fieldBg
            RGB(198, 198, 198),  // fieldBorder
            RGB(225, 240, 255),  // cardSelectedBg (Fluent light blue tint)
            accent,              // cardSelectedBorder (Accent highlight)
            RGB(24, 24, 24),     // textPrimary (#181818)
            RGB(110, 110, 110),  // textSecondary (#6E6E6E)
            RGB(229, 229, 229),  // divider (#E5E5E5)
            accent,              // buttonBg (System Accent Color)
            accentText,          // buttonText
            accent,              // accent
            accentHover,         // accentHover
            accentPressed,       // accentPressed
            accentText,          // accentText
            RGB(236, 236, 236),  // chromePillBg
            RGB(220, 220, 220),  // chromeHoverBg
            RGB(196, 43, 28),    // chromeCloseHoverBg
            RGB(158, 33, 21),    // chromeClosePressedBg
            RGB(255, 255, 255),  // chromeOnCloseText
            RGB(234, 234, 234),  // scrollTrack
            RGB(176, 176, 176),  // scrollThumb
            RGB(140, 140, 140),  // scrollThumbHover
            accent               // scrollThumbPressed
        };
    }
}

} // namespace GameExplorer
