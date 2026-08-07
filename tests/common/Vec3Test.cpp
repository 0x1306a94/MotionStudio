#include <gtest/gtest.h>

#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/common/Vec3.h"

using motion::ApproxEqual;
using motion::Interpolator;
using motion::Vec3;

TEST(Vec3Test, ArithmeticAndApproxEqual) {
    Vec3 a{1, 2, 3};
    Vec3 b{4, 5, 6};
    EXPECT_EQ((a + b), (Vec3{5, 7, 9}));
    EXPECT_EQ((b - a), (Vec3{3, 3, 3}));
    EXPECT_EQ((a * 2.f), (Vec3{2, 4, 6}));
    EXPECT_TRUE(ApproxEqual(a, Vec3{1, 2, 3}));
}

TEST(Vec3Test, InterpolatorLerpMidpoint) {
    Vec3 mid = Interpolator<Vec3>::Lerp(Vec3{0, 0, 0}, Vec3{2, 4, 6}, 0.5f);
    EXPECT_TRUE(ApproxEqual(mid, Vec3{1, 2, 3}));
}
