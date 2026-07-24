#include <gtest/gtest.h>

#include "MotionStudio/common/Time.h"

using motion::FrameRate;
using motion::FrameTime;
using motion::TimeRange;

TEST(TimeRangeTest, EndIsExclusive) {
    TimeRange range{10, 20};
    EXPECT_TRUE(range.contains(10));
    EXPECT_TRUE(range.contains(19));
    EXPECT_FALSE(range.contains(20));
    EXPECT_FALSE(range.contains(9));
}

TEST(FrameRateTest, ToAndFromSeconds) {
    FrameRate fps30{30, 1};
    EXPECT_DOUBLE_EQ(fps30.toSeconds(90), 3.0);
    EXPECT_EQ(fps30.fromSeconds(3.0), static_cast<FrameTime>(90));
}

TEST(FrameRateTest, NonIntegerRateRoundTripsWithinOneFrame) {
    FrameRate ntsc{30000, 1001};
    const FrameTime frame = ntsc.fromSeconds(1.5);
    EXPECT_NEAR(ntsc.toSeconds(frame), 1.5, 1.0 / 30.0);
}
