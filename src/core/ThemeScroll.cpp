#include "GameExplorerCore/ThemeScroll.h"

#include <algorithm>
#include <cmath>

namespace GameExplorer::ThemeScroll {

namespace {

int Clamp(int value, int lower, int upper) {
    if (upper < lower) return lower;
    return (std::max)(lower, (std::min)(value, upper));
}

} // namespace

int MaxScroll(int contentHeight, int pageHeight) {
    return (std::max)(0, contentHeight - pageHeight);
}

Metrics Compute(const RECT& area, int contentHeight, int pageHeight, int scrollPos) {
    Metrics metrics{};

    const int height = area.bottom - area.top;
    if (height <= 0 || pageHeight <= 0) return metrics;

    const int railTop = area.top + kEdgeInset;
    const int railBottom = area.bottom - kEdgeInset;
    const int railHeight = railBottom - railTop;
    // A short surface gets no bar at all rather than a two-pixel smear.
    if (railHeight < kMinThumbHeight) return metrics;

    metrics.needed = contentHeight > pageHeight;
    // No bar at all when everything fits: empty geometry keeps hit-testing and painting honest.
    if (!metrics.needed) return metrics;

    const int centerX = area.right - (kBarWidth / 2);
    metrics.rail = RECT{ centerX - (kRailWidth / 2), railTop,
                         centerX + (kRailWidth - (kRailWidth / 2)), railBottom };

    int thumbHeight = railHeight;
    if (contentHeight > 0) {
        thumbHeight = static_cast<int>((static_cast<long long>(railHeight) * pageHeight) / contentHeight);
        thumbHeight = Clamp(thumbHeight, kMinThumbHeight, railHeight);
    }

    int thumbTop = railTop;
    const int travel = railHeight - thumbHeight;
    const int maxScroll = MaxScroll(contentHeight, pageHeight);
    if (maxScroll > 0 && travel > 0) {
        const int position = Clamp(scrollPos, 0, maxScroll);
        thumbTop += static_cast<int>((static_cast<long long>(travel) * position) / maxScroll);
    }

    metrics.thumb = RECT{ centerX - (kThumbWidth / 2), thumbTop,
                          centerX + (kThumbWidth - (kThumbWidth / 2)), thumbTop + thumbHeight };
    return metrics;
}

int PositionForThumbDrag(const Metrics& metrics, int pointerY, int grabOffset,
                         int contentHeight, int pageHeight) {
    const int railHeight = metrics.rail.bottom - metrics.rail.top;
    const int thumbHeight = metrics.thumb.bottom - metrics.thumb.top;
    const int travel = railHeight - thumbHeight;
    const int maxScroll = MaxScroll(contentHeight, pageHeight);
    if (travel <= 0 || maxScroll <= 0) return 0;

    const int thumbTop = Clamp(pointerY - grabOffset, metrics.rail.top, metrics.rail.bottom - thumbHeight);
    const double ratio = static_cast<double>(thumbTop - metrics.rail.top) / static_cast<double>(travel);
    return Clamp(static_cast<int>(std::lround(ratio * static_cast<double>(maxScroll))), 0, maxScroll);
}

void Paint(HDC dc, const Metrics& metrics, const ThemePalette& palette, bool hovered, bool pressed) {
    if (dc == nullptr || !metrics.needed) return;

    HGDIOBJ oldBrush = nullptr;
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));

    HBRUSH railBrush = CreateSolidBrush(palette.scrollTrack);
    oldBrush = SelectObject(dc, railBrush);
    RoundRect(dc, metrics.rail.left, metrics.rail.top, metrics.rail.right, metrics.rail.bottom,
              kRailWidth, kRailWidth);
    SelectObject(dc, oldBrush);
    DeleteObject(railBrush);

    // Fluent behaviour: the thumb thickens towards the edges of the gutter while it is hovered or
    // dragged, so the resting state stays unobtrusive.
    RECT thumb = metrics.thumb;
    const int extra = pressed ? 3 : (hovered ? 2 : 0);
    thumb.left -= extra;
    thumb.right += extra;

    const COLORREF thumbColor = pressed ? palette.scrollThumbPressed
                                        : (hovered ? palette.scrollThumbHover : palette.scrollThumb);
    HBRUSH thumbBrush = CreateSolidBrush(thumbColor);
    oldBrush = SelectObject(dc, thumbBrush);
    const int thumbWidth = thumb.right - thumb.left;
    RoundRect(dc, thumb.left, thumb.top, thumb.right, thumb.bottom, thumbWidth, thumbWidth);
    SelectObject(dc, oldBrush);
    DeleteObject(thumbBrush);

    SelectObject(dc, oldPen);
}

} // namespace GameExplorer::ThemeScroll
