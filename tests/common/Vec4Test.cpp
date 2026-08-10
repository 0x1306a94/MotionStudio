#include <gtest/gtest.h>

#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/common/Vec4.h"

using motion::ApproxEqual;
using motion::Interpolator;
using motion::Vec4;

TEST(Vec4Test, ArithmeticAndApproxEqual) {
    Vec4 a{1, 2, 3, 4};
    Vec4 b{4, 5, 6, 7};
    EXPECT_EQ((a + b), (Vec4{5, 7, 9, 11}));
    EXPECT_EQ((b - a), (Vec4{3, 3, 3, 3}));
    EXPECT_EQ((a * 2.f), (Vec4{2, 4, 6, 8}));
    EXPECT_TRUE(ApproxEqual(a, Vec4{1, 2, 3, 4}));
}

TEST(Vec4Test, InterpolatorLerpMidpoint) {
    Vec4 mid = Interpolator<Vec4>::Lerp(Vec4{0, 0, 0, 0}, Vec4{2, 4, 6, 8}, 0.5f);
    EXPECT_TRUE(ApproxEqual(mid, Vec4{1, 2, 3, 4}));
}
