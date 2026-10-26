#pragma once

#include <windows.h>

namespace GameExplorer::ThemeFieldScroll {

// Replaces a multi-line EDIT control's native non-client scroll bar with the shared ThemeScroll
// bar, painted inside the field's rounded frame.
//
// The native bar is square, 17 px wide and ignores the Fluent palette (Windows can only darken it
// to "DarkMode_Explorer", which still leaves a visibly different trough). Attaching removes
// WS_VSCROLL, reserves a gutter in the editor's formatting rect and paints the rail/thumb into
// that gutter after the editor paints itself, so the field keeps one surface, one frame and one
// scrollbar style with the rest of the app. No extra window is created: the editor owns its own
// scroll position and the bar just reads and drives it.

// Attaches the themed bar to a multi-line field. Returns false when the control is not a
// multi-line EDIT.
bool Attach(HWND field);

// True when the field currently carries the themed bar.
bool IsAttached(HWND field);

// Removes the themed bar and gives the gutter back to the text.
void Detach(HWND field);

// Paints the rail/thumb into the field's gutter. Called by the field's own WM_PAINT handler after
// the editor has drawn its text and frame.
void Paint(HWND field, HDC dc);

// Handles pointer, wheel and cursor messages that land on the gutter. Called before the editor
// sees the message; returns true when the message was consumed by the bar.
bool HandleMessage(HWND field, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

// Repaints the gutter after the editor scrolled by itself (caret movement, wheel, text change)
// without repainting the text area.
void NotifyChanged(HWND field);

} // namespace GameExplorer::ThemeFieldScroll
