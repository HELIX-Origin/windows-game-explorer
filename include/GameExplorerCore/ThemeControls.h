#pragma once

#include <windows.h>

#include "GameExplorerCore/DarkMode.h"

namespace GameExplorer::ThemeControls {

// Fluent primitives shared by every page of Game Explorer Studio.
//
// Native Win32 controls keep their classic square borders, 3D frames and system highlight brush,
// which clashes with the rounded card layout of the rest of the UI. These helpers replace that
// chrome with palette-driven, rounded surfaces while leaving the controls fully functional
// (keyboard handling, drop-down lists and auto-repeat are still done by the native control).

// Field metrics. A closed combo box is drawn one item height plus six pixels of frame, so a
// 22px item yields exactly kFieldHeight and sits flush with an edit field on the same row.
constexpr int kFieldHeight = 28;
constexpr int kFieldPaddingX = 10;
constexpr int kFieldPaddingY = 6;
constexpr int kComboItemHeight = 22;
constexpr int kCheckBoxSize = 18;

// Palette + font used by every attached control. Call once before creating controls and again
// whenever the system light/dark theme changes.
void SetTheme(const ThemePalette& palette, HFONT font, bool darkMode);

// Replaces a CBS_DROPDOWNLIST combo box frame with a rounded Fluent field: palette surface,
// hover/pressed/focus states and a vector chevron. The drop-down list keeps native behaviour;
// its items are painted by DrawComboItem.
void AttachCombo(HWND combo);

// Paints one item of a themed combo's drop-down list. Call from WM_DRAWITEM when
// CtlType == ODT_COMBOBOX and IsThemedCombo(hwndItem).
void DrawComboItem(const DRAWITEMSTRUCT& item);

bool IsThemedCombo(HWND control);

// Converts an existing check box button to the Fluent look. The button must have been created
// with BS_OWNERDRAW; the owning window toggles it with ToggleCheck on BN_CLICKED.
void AttachCheck(HWND check);
void DrawCheck(const DRAWITEMSTRUCT& item);
void ToggleCheck(HWND check);
bool IsThemedCheck(HWND control);

// Fluent radio button. Create the button with BS_OWNERDRAW (WS_GROUP marks the first button of a
// group) and paint it with DrawRadio; SelectRadio keeps the rest of the group unchecked.
void AttachRadio(HWND radio);
void DrawRadio(const DRAWITEMSTRUCT& item);
void SelectRadio(HWND radio);
bool IsThemedRadio(HWND control);

// Gives an EDIT control rounded corners, palette border and comfortable text margins. Works for
// single-line and multiline editors.
void ApplyFieldFrame(HWND field);

// Background colour every editable field is painted with. Callers that answer WM_CTLCOLOREDIT
// must return a brush of exactly this colour so the control's own erase matches the painted
// frame instead of leaving a differently coloured interior.
COLORREF FieldSurfaceColor();

// Palette published by the last SetTheme call. Scrolling overlays paint with exactly the tokens
// the field around them uses, so there is one palette rather than a per-module copy that can drift.
const ThemePalette& CurrentPalette();

// Height of one text line in the field's font, in pixels. Used by scrolling overlays to translate
// between pixel geometry and the editor's line-based scroll position.
int FieldLineHeight(HWND field);

// Re-applies the rounded window region and the formatting rect after something other than a
// resize changed the field's chrome (for example attaching a scroll bar overlay).
void UpdateFieldLayout(HWND field);

} // namespace GameExplorer::ThemeControls
