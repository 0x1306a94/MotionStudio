#include <gtest/gtest.h>

#include "MotionStudio/model/StrokeDash.h"

TEST(StrokeDashTest, OddLengthDuplicates) {
    const std::vector<float> out = motion::NormalizeDashArray({5.0f});
    ASSERT_EQ(out.size(), 2u);
    EXPECT_FLOAT_EQ(out[0], 5.0f);
    EXPECT_FLOAT_EQ(out[1], 5.0f);
}

TEST(StrokeDashTest, ClampsNegativesAndRejectsZeroSum) {
    EXPECT_TRUE(motion::NormalizeDashArray({-1.0f, 0.0f}).empty());
}

TEST(StrokeDashTest, NeedsDashRequiresDashedAndValidPattern) {
    EXPECT_FALSE(motion::NeedsDash(motion::StrokeMode::Solid, {8.0f, 8.0f}));
    EXPECT_FALSE(motion::NeedsDash(motion::StrokeMode::Dashed, {}));
    EXPECT_TRUE(motion::NeedsDash(motion::StrokeMode::Dashed, {8.0f, 8.0f}));
}
