#include <gtest/gtest.h>

#include "MotionStudio/common/ScaleAnchor.h"
#include "MotionStudio/common/Vec2.h"

using motion::ScaleAnchorForSizeChange;
using motion::Vec2;

TEST(ScaleAnchorTest, ScalesProportionally) {
    EXPECT_EQ(ScaleAnchorForSizeChange({400, 120}, {800, 240}, {200, 60}), (Vec2{400, 120}));
}

TEST(ScaleAnchorTest, ZeroOldWidthKeepsX) {
    EXPECT_EQ(ScaleAnchorForSizeChange({0, 100}, {50, 200}, {10, 50}), (Vec2{10, 100}));
}
