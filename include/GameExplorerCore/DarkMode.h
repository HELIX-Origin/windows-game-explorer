#pragma once

#include <windows.h>

namespace GameExplorer {

struct ThemePalette {
    COLORREF canvasBg;
    COLORREF drawerBg;
    COLORREF cardBg;
    COLORREF cardBorder;
    // Editable-control surface. Text boxes and combo boxes must share these two tokens: a field
    // painted with a different fill than a combo reads as a dark hole with a light rim, which is
    // what looked like rendering debris around the control edges.
    COLORREF fieldBg;
    COLORREF fieldBorder;
    COLORREF cardSelectedBg;
    COLORREF cardSelectedBorder;
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF divider;
    COLORREF buttonBg;
    COLORREF buttonText;
    COLORREF accent;
    COLORREF accentHover;
    COLORREF accentPressed;
    COLORREF accentText;

    // Window-chrome tokens shared by every GUI window (title bar, caption buttons, tab strip).
    // They live here so chrome colours are never hardcoded per window procedure.
    COLORREF chromePillBg;
    COLORREF chromeHoverBg;
    COLORREF chromeCloseHoverBg;
    COLORREF chromeClosePressedBg;
    COLORREF chromeOnCloseText;

    // Themed scrollbar tokens used by ThemeScroll for every scrolling surface (artwork gallery,
    // owned-games catalogue, future lists) so custom-painted bars share one look.
    COLORREF scrollTrack;
    COLORREF scrollThumb;
    COLORREF scrollThumbHover;
    COLORREF scrollThumbPressed;
};

class DarkMode {
public:
    static void Initialize();
    static bool IsSystemDarkMode();
    static COLORREF GetSystemAccentColor();
    static void ApplyWindowTheme(HWND hwnd, bool darkMode);
    static ThemePalette GetPalette(bool isDarkMode);
};

} // namespace GameExplorer
