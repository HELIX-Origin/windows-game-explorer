#pragma once

#include <windows.h>

#include "GameExplorerCore/DarkMode.h"

namespace GameExplorer::ThemeScroll {

// Metrics for the thin, rounded scrollbar that every scrolling surface in the app paints itself
// instead of relying on the classic non-client scrollbar. Keeping it in one place means the
// artwork gallery, the owned-games catalogue and any future list share exactly one look.
constexpr int kBarWidth = 12;      // gutter reserved at the right edge of the scrolling area
constexpr int kRailWidth = 4;      // painted track thickness
constexpr int kThumbWidth = 6;     // painted scroll box thickness
constexpr int kEdgeInset = 4;      // distance from the top/bottom edge of the area
constexpr int kMinThumbHeight = 32;

struct Metrics {
    RECT rail;    // painted track
    RECT thumb;   // painted scroll box
    bool needed;  // content is taller than the viewport, so the bar is visible
};

// Computes the bar geometry for a scrolling surface.
//   area          - client rectangle of the scrolling surface (bar hugs its right edge)
//   contentHeight - total height of all rows
//   pageHeight    - visible height of the viewport
//   scrollPos     - current vertical offset, clamped to the valid range
Metrics Compute(const RECT& area, int contentHeight, int pageHeight, int scrollPos);

// Largest valid scroll offset for the given content/viewport height.
int MaxScroll(int contentHeight, int pageHeight);

// Maps a pointer position to a scroll offset while the thumb is being dragged.
//   pointerY    - current pointer position in the surface's client coordinates
//   grabOffset  - pointer distance from the thumb's top edge when the drag started
int PositionForThumbDrag(const Metrics& metrics, int pointerY, int grabOffset,
                         int contentHeight, int pageHeight);

// Paints the track and thumb with the shared palette tokens.
void Paint(HDC dc, const Metrics& metrics, const ThemePalette& palette, bool hovered, bool pressed);

} // namespace GameExplorer::ThemeScroll
