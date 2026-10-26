#include "GameExplorerCore/ThemeFieldScroll.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <map>

#include "GameExplorerCore/ThemeControls.h"
#include "GameExplorerCore/ThemeScroll.h"

namespace GameExplorer::ThemeFieldScroll {

namespace {

// Pixels the wheel moves per notch, matching the shell's own multi-line editors.
constexpr int kWheelLines = 3;

struct FieldState {
    bool hovered = false;
    bool tracking = false;
    bool dragging = false;
    int grabOffset = 0;
};

// Attachment registry. The field itself is the only handle the callers know about, so the bar's
// interaction state lives beside it rather than in an extra window.
std::map<HWND, FieldState>& Registry() {
    static std::map<HWND, FieldState> registry;
    return registry;
}

bool IsRegistered(HWND field) {
    const auto& registry = Registry();
    return registry.find(field) != registry.end();
}

int ClampInt(int value, int lower, int upper) {
    if (upper < lower) return lower;
    return (std::max)(lower, (std::min)(value, upper));
}

/**
 * The editor scrolls in whole lines while ThemeScroll works in pixels, so the bar is fed the
 * document in line units: content height is the line count and page height is how many whole
 * lines fit inside the formatting rect. Both share one unit, which is all the geometry needs.
 */
struct LineView {
    int lineCount = 1;
    int visibleLines = 1;
    int firstVisible = 0;
};

LineView ReadLineView(HWND field) {
    LineView view{};

    int lineHeight = ThemeControls::FieldLineHeight(field);
    if (lineHeight <= 0) lineHeight = 16;

    RECT format{};
    SendMessageW(field, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
    const int textHeight = format.bottom - format.top;

    view.lineCount = static_cast<int>(SendMessageW(field, EM_GETLINECOUNT, 0, 0));
    if (view.lineCount < 1) view.lineCount = 1;

    view.visibleLines = textHeight > 0 ? textHeight / lineHeight : 1;
    if (view.visibleLines < 1) view.visibleLines = 1;
    if (view.visibleLines > view.lineCount) view.visibleLines = view.lineCount;

    view.firstVisible = static_cast<int>(SendMessageW(field, EM_GETFIRSTVISIBLELINE, 0, 0));
    view.firstVisible = ClampInt(view.firstVisible, 0, view.lineCount - view.visibleLines);
    return view;
}

// The gutter the bar paints into: the trailing kBarWidth pixels of the field's client area.
RECT GutterRect(HWND field) {
    RECT client{};
    GetClientRect(field, &client);
    RECT gutter{client.right - ThemeScroll::kBarWidth, client.top, client.right, client.bottom};
    if (gutter.left < client.left) gutter.left = client.left;
    return gutter;
}

ThemeScroll::Metrics MetricsFor(HWND field) {
    const RECT gutter = GutterRect(field);
    const LineView view = ReadLineView(field);
    return ThemeScroll::Compute(gutter, view.lineCount, view.visibleLines, view.firstVisible);
}

bool InGutter(HWND field, int x) {
    RECT client{};
    GetClientRect(field, &client);
    return x >= client.right - ThemeScroll::kBarWidth && x < client.right;
}

void ScrollToLine(HWND field, const LineView& view, int targetLine) {
    const int target = ClampInt(targetLine, 0, view.lineCount - view.visibleLines);
    const int delta = target - view.firstVisible;
    if (delta == 0) return;
    SendMessageW(field, EM_LINESCROLL, 0, delta);
    NotifyChanged(field);
}

} // namespace

bool Attach(HWND field) {
    if (field == nullptr) return false;
    if ((GetWindowLongPtrW(field, GWL_STYLE) & ES_MULTILINE) == 0) return false;
    if (IsRegistered(field)) return true;

    // The native bar is what this module replaces; drop it before the client rect is measured so
    // the editor's text area runs out to the frame.
    const LONG_PTR style = GetWindowLongPtrW(field, GWL_STYLE);
    if ((style & WS_VSCROLL) != 0) {
        SetWindowLongPtrW(field, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_VSCROLL));
        SetWindowPos(field, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    Registry()[field] = FieldState{};

    // Reserve the gutter in the formatting rect once the field is known to be attached.
    ThemeControls::UpdateFieldLayout(field);
    return true;
}

bool IsAttached(HWND field) {
    return field != nullptr && IsRegistered(field);
}

void Detach(HWND field) {
    if (field == nullptr) return;
    auto& registry = Registry();
    const auto it = registry.find(field);
    if (it == registry.end()) return;
    registry.erase(it);

    // Put the editor's text back under the frame once the gutter is gone.
    if (IsWindow(field)) ThemeControls::UpdateFieldLayout(field);
}

void Paint(HWND field, HDC dc) {
    if (dc == nullptr || !IsAttached(field)) return;

    const ThemeScroll::Metrics metrics = MetricsFor(field);
    if (!metrics.needed) return;

    const auto& registry = Registry();
    const auto it = registry.find(field);
    const bool hovered = it != registry.end() && it->second.hovered;
    const bool pressed = it != registry.end() && it->second.dragging;

    // The editor has already painted its surface underneath, so the bar only draws rail and thumb.
    ThemeScroll::Paint(dc, metrics, ThemeControls::CurrentPalette(), hovered, pressed);
}

void NotifyChanged(HWND field) {
    if (field == nullptr || !IsAttached(field)) return;
    const RECT gutter = GutterRect(field);
    InvalidateRect(field, &gutter, FALSE);
}

bool HandleMessage(HWND field, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (field == nullptr || !IsAttached(field)) return false;

    auto& registry = Registry();
    auto it = registry.find(field);
    if (it == registry.end()) return false;
    FieldState& state = it->second;

    switch (msg) {
    case WM_SETCURSOR: {
        // The editor shows an I-beam over its whole client; the gutter is not a text area.
        if (LOWORD(lParam) != HTCLIENT) return false;
        POINT pointer{};
        GetCursorPos(&pointer);
        ScreenToClient(field, &pointer);
        if (!InGutter(field, pointer.x)) return false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        result = TRUE;
        return true;
    }

    case WM_MOUSEMOVE: {
        if (state.dragging) {
            const int y = GET_Y_LPARAM(lParam);
            const LineView view = ReadLineView(field);
            const ThemeScroll::Metrics metrics = MetricsFor(field);
            const int target = ThemeScroll::PositionForThumbDrag(
                metrics, y, state.grabOffset, view.lineCount, view.visibleLines);
            ScrollToLine(field, view, target);
            result = 0;
            return true;
        }

        const int x = GET_X_LPARAM(lParam);
        const bool overBar = InGutter(field, x);
        if (overBar != state.hovered) {
            state.hovered = overBar;
            NotifyChanged(field);
        }
        if (!state.tracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, field, 0};
            if (TrackMouseEvent(&track) != FALSE) state.tracking = true;
        }
        return false;
    }

    case WM_MOUSELEAVE:
        state.tracking = false;
        if (state.hovered && !state.dragging) {
            state.hovered = false;
            NotifyChanged(field);
        }
        return false;

    case WM_LBUTTONDOWN: {
        if (!InGutter(field, GET_X_LPARAM(lParam))) return false;
        const ThemeScroll::Metrics metrics = MetricsFor(field);
        if (!metrics.needed) return false;

        SetFocus(field);
        const int y = GET_Y_LPARAM(lParam);
        const LineView view = ReadLineView(field);
        if (y >= metrics.thumb.top && y < metrics.thumb.bottom) {
            state.dragging = true;
            state.hovered = true;
            state.grabOffset = y - metrics.thumb.top;
            SetCapture(field);
        } else if (y < metrics.thumb.top) {
            ScrollToLine(field, view, view.firstVisible - view.visibleLines);
        } else {
            ScrollToLine(field, view, view.firstVisible + view.visibleLines);
        }
        NotifyChanged(field);
        result = 0;
        return true;
    }

    case WM_LBUTTONUP:
        if (!state.dragging) return false;
        state.dragging = false;
        if (GetCapture() == field) ReleaseCapture();
        NotifyChanged(field);
        result = 0;
        return true;

    case WM_MOUSEWHEEL: {
        // The wheel arrives for the focused field anywhere in it; only take it over the gutter,
        // where the editor cannot see it because the pointer is outside its text area.
        POINT pointer{};
        pointer.x = GET_X_LPARAM(lParam);
        pointer.y = GET_Y_LPARAM(lParam);
        ScreenToClient(field, &pointer);
        if (!InGutter(field, pointer.x)) return false;

        const LineView view = ReadLineView(field);
        const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        ScrollToLine(field, view, view.firstVisible - notches * kWheelLines);
        result = 0;
        return true;
    }

    default:
        break;
    }

    return false;
}

} // namespace GameExplorer::ThemeFieldScroll
