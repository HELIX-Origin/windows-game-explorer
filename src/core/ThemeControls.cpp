#include "GameExplorerCore/ThemeControls.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <new>
#include <string>

#include "GameExplorerCore/ThemeFieldScroll.h"
#include "GameExplorerCore/ThemeScroll.h"

namespace GameExplorer::ThemeControls {

namespace {

constexpr UINT_PTR kComboSubclassId = 0x47454331;  // 'GEC1'
constexpr UINT_PTR kCheckSubclassId = 0x47454332;  // 'GEC2'
constexpr UINT_PTR kFieldSubclassId = 0x47454333;  // 'GEC3'

constexpr const wchar_t* kComboProperty = L"GameExplorer.ThemeCombo";
constexpr const wchar_t* kCheckProperty = L"GameExplorer.ThemeCheck";
constexpr const wchar_t* kRadioProperty = L"GameExplorer.ThemeRadio";

constexpr int kFieldRadius = 5;

ThemePalette g_palette{};
HFONT g_font = nullptr;
bool g_darkMode = true;

struct HoverState {
    bool hot = false;
    bool tracking = false;
    LRESULT checkState = BST_UNCHECKED;
};

COLORREF FieldBackground() {
    // Palette-driven, and deliberately the same token the combo surface uses.
    return g_palette.fieldBg;
}

COLORREF FieldBorderColor() {
    return g_palette.fieldBorder;
}

void PaintCombo(HWND combo, HDC dc, const HoverState& state) {
    RECT rc{};
    GetClientRect(combo, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;

    const bool enabled = IsWindowEnabled(combo) != FALSE;
    const bool focused = GetFocus() == combo;
    const bool dropped = SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != 0;

    COLORREF surface;
    COLORREF border;
    COLORREF text;
    if (!enabled) {
        surface = g_darkMode ? RGB(38, 38, 38) : RGB(247, 247, 247);
        border = g_darkMode ? RGB(52, 52, 52) : RGB(226, 226, 226);
        text = g_palette.textSecondary;
    } else if (dropped) {
        surface = g_darkMode ? RGB(58, 58, 58) : RGB(240, 240, 240);
        border = g_darkMode ? RGB(96, 96, 96) : RGB(150, 150, 150);
        text = g_palette.textPrimary;
    } else if (state.hot) {
        surface = g_darkMode ? RGB(50, 50, 50) : RGB(249, 249, 249);
        border = g_darkMode ? RGB(90, 90, 90) : RGB(160, 160, 160);
        text = g_palette.textPrimary;
    } else {
        surface = FieldBackground();
        border = FieldBorderColor();
        text = g_palette.textPrimary;
    }
    if (focused && enabled) border = g_palette.accent;

    // A combo box owns its whole rectangular window but paints a rounded surface inset by one
    // pixel, and its WM_ERASEBKGND is suppressed. Without an explicit surround fill the outermost
    // row/column and the four corners keep whatever was underneath before the control moved or was
    // resized - stale card borders or fragments of earlier text ("artifacts around the edges").
    HBRUSH surround = CreateSolidBrush(g_palette.cardBg);
    FillRect(dc, &rc, surround);
    DeleteObject(surround);

    HBRUSH brush = CreateSolidBrush(surface);
    HPEN pen = CreatePen(PS_SOLID, (focused && enabled) ? 2 : 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, 1, 1, rc.right - 1, rc.bottom - 1, kFieldRadius * 2, kFieldRadius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    wchar_t label[256]{};
    const int selected = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (selected != CB_ERR) {
        SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(selected),
                     reinterpret_cast<LPARAM>(label));
    }

    RECT textRect{kFieldPaddingX + 2, 0, rc.right - 28, rc.bottom};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    HGDIOBJ oldFont = SelectObject(dc, g_font != nullptr ? g_font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc, label, -1, &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Vector chevron so the field does not depend on an icon font being installed.
    const int chevronX = rc.right - 17;
    const int chevronY = (rc.top + rc.bottom) / 2;
    POINT points[3]{
        {chevronX - 4, chevronY - 2},
        {chevronX, chevronY + 2},
        {chevronX + 4, chevronY - 2},
    };
    HPEN chevronPen = CreatePen(PS_SOLID, 2, enabled ? g_palette.textSecondary : g_palette.cardBorder);
    HGDIOBJ previousPen = SelectObject(dc, chevronPen);
    Polyline(dc, points, 3);
    SelectObject(dc, previousPen);
    DeleteObject(chevronPen);
    SelectObject(dc, oldFont);
}

bool IsMultilineField(HWND field) {
    return (GetWindowLongPtrW(field, GWL_STYLE) & ES_MULTILINE) != 0;
}

void PaintFieldBorder(HWND field, HDC dc, bool fillSurface) {
    // Single-line fields paint the frame over the whole window (the client rect is inset inside
    // it); multi-line fields paint the frame around the client area.
    RECT rc{};
    if (IsMultilineField(field)) {
        GetClientRect(field, &rc);
    } else {
        GetWindowRect(field, &rc);
    }

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    const bool focused = GetFocus() == field;
    const COLORREF border = focused ? g_palette.accent : FieldBorderColor();

    // Erase the control rectangle to the card before drawing the rounded frame. The window region
    // is rounded, so the pixels between the region edge and the rounded outline would otherwise
    // keep whatever used to be painted there - fragments of an earlier layout or of the control
    // that previously occupied the slot.
    if (fillSurface) {
        RECT full{0, 0, width, height};
        HBRUSH surround = CreateSolidBrush(g_palette.cardBg);
        FillRect(dc, &full, surround);
        DeleteObject(surround);
    }

    HBRUSH surface = fillSurface ? CreateSolidBrush(FieldBackground()) : nullptr;
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, surface != nullptr ? surface
                                                          : GetStockObject(NULL_BRUSH));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, 0, 0, width, height, kFieldRadius * 2, kFieldRadius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    if (surface != nullptr) DeleteObject(surface);
    DeleteObject(pen);
}

/**
 * Single-line EDIT controls place their line of text against the top of the client rect instead of
 * centring it, so a tall Fluent field would show the glyphs crushed against the top border. Inset
 * the client rect symmetrically (and by one pixel at the sides for the rounded outline) to optically
 * centre the line inside the frame. Multi-line editors use their formatting rect instead.
 */
struct FieldInsets {
    int left;
    int top;
    int right;
    int bottom;
};

FieldInsets ComputeFieldInsets(HWND field, int height) {
    if (IsMultilineField(field)) return FieldInsets{0, 0, 0, 0};

    wchar_t className[32]{};
    GetClassNameW(field, className, static_cast<int>(ARRAYSIZE(className)));
    if (lstrcmpiW(className, L"Edit") != 0) return FieldInsets{0, 0, 0, 0};

    const int vertical = (height - FieldLineHeight(field)) / 2;
    if (vertical <= 0 || height <= vertical * 2 + 8) return FieldInsets{1, 0, 1, 0};
    return FieldInsets{1, vertical, 1, vertical};
}

void InvalidateField(HWND field) {
    RedrawWindow(field, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
}

void UpdateFieldRegion(HWND field) {
    // The region must cover the window, not the (inset) client rect.
    RECT rc{};
    GetWindowRect(field, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                     kFieldRadius * 2, kFieldRadius * 2);
    if (region == nullptr) return;
    if (SetWindowRgn(field, region, TRUE) == 0) DeleteObject(region);
}

/**
 * Inset the formatting rectangle of a multi-line editor so its first line sits below the painted
 * frame instead of on top of it. Single-line editors are centred through WM_NCCALCSIZE and only
 * need EM_SETMARGINS for the left edge (Windows ignores the right margin there).
 */
void UpdateFieldTextRect(HWND field) {
    if (field == nullptr) return;
    if ((GetWindowLongPtrW(field, GWL_STYLE) & ES_MULTILINE) == 0) return;

    RECT rc{};
    GetClientRect(field, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;

    // A themed scroll bar overlay covers the field's right edge, so the text must stop before it.
    const int rightInset = kFieldPaddingX + (ThemeFieldScroll::IsAttached(field)
                                                  ? ThemeScroll::kBarWidth
                                                  : 0);

    RECT text{ kFieldPaddingX, kFieldPaddingY, rc.right - rightInset,
               rc.bottom - kFieldPaddingY };
    if (text.right <= text.left || text.bottom <= text.top) return;

    SendMessageW(field, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&text));
}

void TrackLeave(HWND hwnd, HoverState& state) {
    if (state.tracking) return;
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
    if (TrackMouseEvent(&track) != FALSE) state.tracking = true;
}

LRESULT CALLBACK ComboSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR idSubclass, DWORD_PTR refData) {
    auto* state = reinterpret_cast<HoverState*>(refData);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        if (state != nullptr) PaintCombo(hwnd, dc, *state);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (state != nullptr) PaintCombo(hwnd, reinterpret_cast<HDC>(wParam), *state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (state != nullptr && !state->hot) {
            state->hot = true;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        if (state != nullptr) TrackLeave(hwnd, *state);
        break;
    case WM_MOUSELEAVE:
        if (state != nullptr) {
            state->hot = false;
            state->tracking = false;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    case WM_THEMECHANGED:
        SetWindowTheme(hwnd, L"", L"");
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    case CB_SETCURSEL:
    case CB_SHOWDROPDOWN: {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return result;
    }
    case WM_NCDESTROY:
        delete state;
        RemoveWindowSubclass(hwnd, ComboSubclassProc, idSubclass);
        break;
    default:
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CheckSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR idSubclass, DWORD_PTR refData) {
    auto* state = reinterpret_cast<HoverState*>(refData);

    switch (msg) {
    case BM_SETCHECK:
        if (state != nullptr) {
            state->checkState = wParam;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        break;

    case BM_GETCHECK:
        if (state != nullptr) {
            return state->checkState;
        }
        break;

    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (state != nullptr && !state->hot) {
            state->hot = true;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        if (state != nullptr) TrackLeave(hwnd, *state);
        break;
    case WM_MOUSELEAVE:
        if (state != nullptr) {
            state->hot = false;
            state->tracking = false;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    case WM_NCDESTROY:
        delete state;
        RemoveWindowSubclass(hwnd, CheckSubclassProc, idSubclass);
        break;
    default:
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FieldSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR idSubclass, DWORD_PTR refData) {
    (void)refData;

    // The themed scroll bar owns the field's trailing gutter: pointer, wheel and cursor messages
    // that land there never reach the editor.
    {
        LRESULT consumed = 0;
        if (ThemeFieldScroll::HandleMessage(hwnd, msg, wParam, lParam, consumed)) return consumed;
    }

    switch (msg) {
    case WM_NCCALCSIZE: {
        if (wParam != TRUE || lParam == 0) break;
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        const int height = params->rgrc[0].bottom - params->rgrc[0].top;
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        const FieldInsets insets = ComputeFieldInsets(hwnd, height);
        RECT& client = params->rgrc[0];
        client.left += insets.left;
        client.right -= insets.right;
        client.top += insets.top;
        client.bottom -= insets.bottom;
        return result;
    }
    case WM_NCPAINT: {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (!IsMultilineField(hwnd)) {
            HDC dc = GetWindowDC(hwnd);
            if (dc != nullptr) {
                // Paint the frame band only: a non-client paint can arrive without a client repaint,
                // and filling the whole window here would erase the text.
                POINT origin{0, 0};
                ClientToScreen(hwnd, &origin);
                RECT window{};
                GetWindowRect(hwnd, &window);
                RECT client{};
                GetClientRect(hwnd, &client);
                const int clientLeft = origin.x - window.left;
                const int clientTop = origin.y - window.top;
                ExcludeClipRect(dc, clientLeft, clientTop,
                                clientLeft + (client.right - client.left),
                                clientTop + (client.bottom - client.top));
                PaintFieldBorder(hwnd, dc, true);
                ReleaseDC(hwnd, dc);
            }
        }
        return result;
    }
    case WM_PAINT: {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (IsMultilineField(hwnd)) {
            // The client is not inset for multi-line editors, so the frame is drawn over it.
            HDC dc = GetDC(hwnd);
            if (dc != nullptr) {
                PaintFieldBorder(hwnd, dc, false);
                ThemeFieldScroll::Paint(hwnd, dc);
                ReleaseDC(hwnd, dc);
            }
        }
        return result;
    }
    case WM_SIZE:
        // The editor's own WM_SIZE recomputes its formatting rect, so the inset is applied after
        // DefSubclassProc below instead of here.
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateField(hwnd);
        break;
    case WM_NCDESTROY:
        ThemeFieldScroll::Detach(hwnd);
        RemoveWindowSubclass(hwnd, FieldSubclassProc, idSubclass);
        break;
    default:
        break;
    }

    const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

    if (msg == WM_SIZE) {
        UpdateFieldRegion(hwnd);
        UpdateFieldTextRect(hwnd);
        InvalidateField(hwnd);
    }

    // The editor scrolls itself (caret movement, wheel, text edits) without telling the bar, so
    // any message that can move the view refreshes the gutter.
    if (IsMultilineField(hwnd)) {
        switch (msg) {
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_KEYDOWN:
        case WM_CHAR:
        case WM_MOUSEWHEEL:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_SETTEXT:
            ThemeFieldScroll::NotifyChanged(hwnd);
            break;
        default:
            break;
        }
    }

    return result;
}

bool HasProperty(HWND control, const wchar_t* name) {
    return control != nullptr && GetPropW(control, name) != nullptr;
}

} // namespace

void SetTheme(const ThemePalette& palette, HFONT font, bool darkMode) {
    g_palette = palette;
    g_font = font;
    g_darkMode = darkMode;
}

void AttachCombo(HWND combo) {
    if (combo == nullptr || HasProperty(combo, kComboProperty)) return;

    auto* state = new (std::nothrow) HoverState();
    if (state == nullptr) return;

    // Drop the native theme so the field is painted here instead of by uxtheme.
    SetWindowTheme(combo, L"", L"");

    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info)) {
        if (info.hwndItem != nullptr) DarkMode::ApplyWindowTheme(info.hwndItem, g_darkMode);
        if (info.hwndList != nullptr) DarkMode::ApplyWindowTheme(info.hwndList, g_darkMode);
    }

    if (SetWindowSubclass(combo, ComboSubclassProc, kComboSubclassId,
                          reinterpret_cast<DWORD_PTR>(state)) == FALSE) {
        delete state;
        return;
    }
    SetPropW(combo, kComboProperty, reinterpret_cast<HANDLE>(1));
    InvalidateRect(combo, nullptr, TRUE);
}

bool IsThemedCombo(HWND control) {
    return HasProperty(control, kComboProperty);
}

void DrawComboItem(const DRAWITEMSTRUCT& item) {
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;

    HBRUSH background = CreateSolidBrush(FieldBackground());
    FillRect(item.hDC, &item.rcItem, background);
    DeleteObject(background);

    const int index = static_cast<int>(item.itemID);
    if (index < 0) return;

    const int length = static_cast<int>(SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, index, 0));
    if (length <= 0) return;

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    SendMessageW(item.hwndItem, CB_GETLBTEXT, static_cast<WPARAM>(index),
                 reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(length));

    if (selected) {
        RECT pill = item.rcItem;
        InflateRect(&pill, -4, -2);
        HBRUSH accent = CreateSolidBrush(g_palette.accent);
        HGDIOBJ oldBrush = SelectObject(item.hDC, accent);
        HGDIOBJ oldPen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        RoundRect(item.hDC, pill.left, pill.top, pill.right, pill.bottom, 8, 8);
        SelectObject(item.hDC, oldPen);
        SelectObject(item.hDC, oldBrush);
        DeleteObject(accent);
    }

    RECT textRect = item.rcItem;
    textRect.left += kFieldPaddingX + 2;
    textRect.right -= 8;

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, selected ? g_palette.accentText
                                    : (disabled ? g_palette.textSecondary : g_palette.textPrimary));
    HGDIOBJ oldFont = SelectObject(item.hDC, g_font != nullptr ? g_font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(item.hDC, oldFont);
}

void AttachToggle(HWND control, const wchar_t* property) {
    if (control == nullptr || HasProperty(control, property)) return;

    auto* state = new (std::nothrow) HoverState();
    if (state == nullptr) return;

    if (SetWindowSubclass(control, CheckSubclassProc, kCheckSubclassId,
                          reinterpret_cast<DWORD_PTR>(state)) == FALSE) {
        delete state;
        return;
    }
    SetPropW(control, property, reinterpret_cast<HANDLE>(1));
    InvalidateRect(control, nullptr, TRUE);
}

void AttachCheck(HWND check) {
    AttachToggle(check, kCheckProperty);
}

bool IsThemedCheck(HWND control) {
    return HasProperty(control, kCheckProperty);
}

void AttachRadio(HWND radio) {
    AttachToggle(radio, kRadioProperty);
}

bool IsThemedRadio(HWND control) {
    return HasProperty(control, kRadioProperty);
}

void SelectRadio(HWND radio) {
    if (!IsThemedRadio(radio)) return;

    // Walk back to the first button of the group (WS_GROUP), then forward through its siblings.
    HWND first = radio;
    for (;;) {
        HWND previous = GetWindow(first, GW_HWNDPREV);
        if (previous == nullptr) break;
        if ((GetWindowLongPtrW(previous, GWL_STYLE) & WS_GROUP) != 0) break;
        if (!IsThemedRadio(previous)) break;
        first = previous;
    }

    for (HWND current = first; current != nullptr; current = GetWindow(current, GW_HWNDNEXT)) {
        if (!IsThemedRadio(current)) break;
        SendMessageW(current, BM_SETCHECK, current == radio ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(current, nullptr, TRUE);
        if (current != first && (GetWindowLongPtrW(current, GWL_STYLE) & WS_GROUP) != 0) break;
    }
}

void ToggleCheck(HWND check) {
    if (!IsThemedCheck(check)) return;
    const LRESULT checked = SendMessageW(check, BM_GETCHECK, 0, 0);
    SendMessageW(check, BM_SETCHECK, checked == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
    InvalidateRect(check, nullptr, TRUE);
}

void DrawCheck(const DRAWITEMSTRUCT& item) {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool checked = SendMessageW(item.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;

    HBRUSH background = CreateSolidBrush(g_palette.cardBg);
    FillRect(item.hDC, &item.rcItem, background);
    DeleteObject(background);

    RECT box = item.rcItem;
    box.left += 1;
    box.top = item.rcItem.top + (item.rcItem.bottom - item.rcItem.top - kCheckBoxSize) / 2;
    box.right = box.left + kCheckBoxSize;
    box.bottom = box.top + kCheckBoxSize;

    COLORREF boxFill;
    COLORREF boxBorder;
    if (!disabled && checked) {
        boxFill = pressed ? g_palette.accentPressed : g_palette.accent;
        boxBorder = boxFill;
    } else {
        boxFill = FieldBackground();
        boxBorder = disabled ? FieldBorderColor()
                             : (g_darkMode ? RGB(96, 96, 96) : RGB(150, 150, 150));
    }

    HBRUSH boxBrush = CreateSolidBrush(boxFill);
    HPEN boxPen = CreatePen(PS_SOLID, 1, boxBorder);
    HGDIOBJ oldBrush = SelectObject(item.hDC, boxBrush);
    HGDIOBJ oldPen = SelectObject(item.hDC, boxPen);
    RoundRect(item.hDC, box.left, box.top, box.right, box.bottom, 8, 8);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(boxBrush);
    DeleteObject(boxPen);

    if (checked) {
        POINT tick[3]{
            {box.left + 4, box.top + 9},
            {box.left + 8, box.top + 13},
            {box.left + 14, box.top + 5},
        };
        HPEN tickPen = CreatePen(PS_SOLID, 2, disabled ? g_palette.textSecondary : g_palette.accentText);
        HGDIOBJ previousPen = SelectObject(item.hDC, tickPen);
        Polyline(item.hDC, tick, 3);
        SelectObject(item.hDC, previousPen);
        DeleteObject(tickPen);
    }

    const int length = GetWindowTextLengthW(item.hwndItem);
    std::wstring text;
    if (length > 0) {
        text.resize(static_cast<size_t>(length) + 1);
        const int written = GetWindowTextW(item.hwndItem, text.data(), length + 1);
        text.resize(written > 0 ? static_cast<size_t>(written) : 0);
    }

    RECT textRect = item.rcItem;
    textRect.left = box.right + 10;
    textRect.right -= 4;

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? g_palette.textSecondary : g_palette.textPrimary);
    HGDIOBJ oldFont = SelectObject(item.hDC, g_font != nullptr ? g_font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(item.hDC, oldFont);
}

void DrawRadio(const DRAWITEMSTRUCT& item) {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool checked = SendMessageW(item.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;

    HBRUSH background = CreateSolidBrush(g_palette.cardBg);
    FillRect(item.hDC, &item.rcItem, background);
    DeleteObject(background);

    RECT circle = item.rcItem;
    circle.left += 1;
    circle.top = item.rcItem.top + (item.rcItem.bottom - item.rcItem.top - kCheckBoxSize) / 2;
    circle.right = circle.left + kCheckBoxSize;
    circle.bottom = circle.top + kCheckBoxSize;

    const COLORREF ring = checked && !disabled
        ? (pressed ? g_palette.accentPressed : g_palette.accent)
        : (disabled ? (g_darkMode ? RGB(52, 52, 52) : RGB(226, 226, 226))
                    : (g_darkMode ? RGB(140, 140, 140) : RGB(120, 120, 120)));

    HBRUSH surface = CreateSolidBrush(FieldBackground());
    HPEN ringPen = CreatePen(PS_SOLID, checked ? 2 : 1, ring);
    HGDIOBJ oldBrush = SelectObject(item.hDC, surface);
    HGDIOBJ oldPen = SelectObject(item.hDC, ringPen);
    Ellipse(item.hDC, circle.left, circle.top, circle.right, circle.bottom);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(surface);
    DeleteObject(ringPen);

    if (checked) {
        RECT dot = circle;
        InflateRect(&dot, -5, -5);
        HBRUSH dotBrush = CreateSolidBrush(disabled ? g_palette.textSecondary : g_palette.accent);
        HGDIOBJ previousBrush = SelectObject(item.hDC, dotBrush);
        HGDIOBJ previousPen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        Ellipse(item.hDC, dot.left, dot.top, dot.right, dot.bottom);
        SelectObject(item.hDC, previousPen);
        SelectObject(item.hDC, previousBrush);
        DeleteObject(dotBrush);
    }

    const int length = GetWindowTextLengthW(item.hwndItem);
    std::wstring text;
    if (length > 0) {
        text.resize(static_cast<size_t>(length) + 1);
        const int written = GetWindowTextW(item.hwndItem, text.data(), length + 1);
        text.resize(written > 0 ? static_cast<size_t>(written) : 0);
    }

    RECT textRect = item.rcItem;
    textRect.left = circle.right + 10;
    textRect.right -= 4;

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? g_palette.textSecondary : g_palette.textPrimary);
    HGDIOBJ oldFont = SelectObject(item.hDC, g_font != nullptr ? g_font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(item.hDC, oldFont);
}

void ApplyFieldFrame(HWND field) {
    if (field == nullptr) return;

    // Install the subclass first: the client-rect centring below depends on WM_NCCALCSIZE.
    SetWindowSubclass(field, FieldSubclassProc, kFieldSubclassId, 0);

    // Client-edge borders are square and double up with the painted rounded frame.
    const LONG_PTR exStyle = GetWindowLongPtrW(field, GWL_EXSTYLE);
    if ((exStyle & WS_EX_CLIENTEDGE) != 0) {
        SetWindowLongPtrW(field, GWL_EXSTYLE, exStyle & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE));
    }

    // Dark-mode theming gives the editor a dark caret/selection instead of the classic white
    // highlight. The scroll bar is painted by ThemeFieldScroll, not by this theme.
    if (IsMultilineField(field) && g_darkMode) {
        SetWindowTheme(field, L"DarkMode_Explorer", nullptr);
    } else {
        SetWindowTheme(field, L"", L"");
    }

    wchar_t className[32]{};
    GetClassNameW(field, className, static_cast<int>(ARRAYSIZE(className)));
    if (lstrcmpiW(className, L"Edit") == 0) {
        SendMessageW(field, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(kFieldPaddingX, kFieldPaddingX));
    }

    UpdateFieldRegion(field);
    UpdateFieldTextRect(field);
    InvalidateField(field);

    // Recalculate the client rect so the vertical centring applies without waiting for a resize.
    SetWindowPos(field, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

COLORREF FieldSurfaceColor() {
    return FieldBackground();
}

const ThemePalette& CurrentPalette() {
    return g_palette;
}

int FieldLineHeight(HWND field) {
    HDC dc = GetDC(field);
    if (dc == nullptr) return 16;

    TEXTMETRICW metrics{};
    HGDIOBJ oldFont = SelectObject(dc, g_font != nullptr ? g_font : GetStockObject(DEFAULT_GUI_FONT));
    const int height = GetTextMetricsW(dc, &metrics) ? metrics.tmHeight : 16;
    SelectObject(dc, oldFont);
    ReleaseDC(field, dc);
    return height;
}

void UpdateFieldLayout(HWND field) {
    if (field == nullptr) return;
    UpdateFieldRegion(field);
    UpdateFieldTextRect(field);
    InvalidateField(field);
}

} // namespace GameExplorer::ThemeControls
