#include "TestHarness.h"

#include "GameExplorerCore/ThemeScroll.h"

using namespace GameExplorer::ThemeScroll;

namespace {

RECT Area(int width, int height) {
    return RECT{ 0, 0, width, height };
}

} // namespace

TEST_CASE(ThemeScroll_HiddenWhenContentFits) {
    const RECT area = Area(400, 300);
    const Metrics metrics = Compute(area, 280, 300, 0);
    ASSERT_FALSE(metrics.needed);
    ASSERT_EQ(metrics.rail.left, metrics.rail.right);   // no rail painted
    ASSERT_EQ(metrics.thumb.top, metrics.thumb.bottom); // no thumb painted
}

TEST_CASE(ThemeScroll_ThumbTracksContentRatio) {
    const RECT area = Area(400, 200);
    // Half of the content visible: the thumb should cover roughly half the rail.
    const Metrics metrics = Compute(area, 400, 200, 0);
    ASSERT_TRUE(metrics.needed);

    const int railHeight = metrics.rail.bottom - metrics.rail.top;
    const int thumbHeight = metrics.thumb.bottom - metrics.thumb.top;
    ASSERT_EQ(railHeight, 200 - (2 * kEdgeInset));
    ASSERT_TRUE(thumbHeight >= railHeight / 2 - 2);
    ASSERT_TRUE(thumbHeight <= railHeight / 2 + 2);

    // Thumb sits at the top when the offset is zero and at the bottom when fully scrolled.
    ASSERT_EQ(metrics.thumb.top, metrics.rail.top);

    const Metrics bottom = Compute(area, 400, 200, MaxScroll(400, 200));
    ASSERT_EQ(bottom.thumb.bottom, bottom.rail.bottom);
}

TEST_CASE(ThemeScroll_ThumbNeverCollapses) {
    const RECT area = Area(400, 600);
    // Very long content: proportional height would be one pixel, so the minimum applies.
    const Metrics metrics = Compute(area, 600000, 600, 0);
    ASSERT_TRUE(metrics.needed);
    ASSERT_EQ(metrics.thumb.bottom - metrics.thumb.top, kMinThumbHeight);
}

TEST_CASE(ThemeScroll_ScrollPositionClamped) {
    const RECT area = Area(400, 200);
    const Metrics top = Compute(area, 1000, 200, -500);
    ASSERT_EQ(top.thumb.top, top.rail.top);

    const Metrics over = Compute(area, 1000, 200, 5000);
    ASSERT_EQ(over.thumb.bottom, over.rail.bottom);
}

TEST_CASE(ThemeScroll_DragMapsPointerToOffset) {
    const RECT area = Area(400, 200);
    constexpr int contentHeight = 1000;
    constexpr int pageHeight = 200;
    const Metrics metrics = Compute(area, contentHeight, pageHeight, 0);
    ASSERT_TRUE(metrics.needed);

    const int thumbHeight = metrics.thumb.bottom - metrics.thumb.top;
    const int maxScroll = MaxScroll(contentHeight, pageHeight);
    ASSERT_EQ(maxScroll, 800);

    // Dragging the thumb to the bottom of the rail yields the maximum offset.
    ASSERT_EQ(PositionForThumbDrag(metrics, metrics.rail.bottom, 0, contentHeight, pageHeight), maxScroll);
    // Dragging above the rail clamps to the top.
    ASSERT_EQ(PositionForThumbDrag(metrics, metrics.rail.top - 50, 0, contentHeight, pageHeight), 0);

    // A grab offset inside the thumb is preserved instead of snapping the thumb top to the pointer.
    const int expected = PositionForThumbDrag(metrics, metrics.rail.top + (thumbHeight / 2), thumbHeight / 2,
                                              contentHeight, pageHeight);
    ASSERT_TRUE(expected >= 0);
    ASSERT_TRUE(expected < maxScroll / 4);
}

TEST_CASE(ThemeScroll_DragWithoutOverflowIsNoOp) {
    const RECT area = Area(400, 300);
    const Metrics metrics = Compute(area, 100, 300, 0);
    ASSERT_FALSE(metrics.needed);
    ASSERT_EQ(PositionForThumbDrag(metrics, 150, 0, 100, 300), 0);
}
