#include <gtest/gtest.h>

#include "MotionStudio/common/Math.h"

using motion::approxEqual;
using motion::Mat3;
using motion::Vec2;

TEST(Vec2Test, ArithmeticOperators) {
    Vec2 a{1, 2};
    Vec2 b{3, 4};

    Vec2 sum = a + b;
    EXPECT_EQ(sum, (Vec2{4, 6}));

    Vec2 difference = b - a;
    EXPECT_EQ(difference, (Vec2{2, 2}));

    Vec2 scaled = a * 2.0f;
    EXPECT_EQ(scaled, (Vec2{2, 4}));

    Vec2 negated = -a;
    EXPECT_EQ(negated, (Vec2{-1, -2}));
}

TEST(Mat3Test, TranslateMovesPoint) {
    Vec2 moved = Mat3::translate({10, 20}).transformPoint({1, 2});
    EXPECT_EQ(moved, (Vec2{11, 22}));
}

TEST(Mat3Test, Rotate90Degrees) {
    Vec2 rotated = Mat3::rotate(90.0f).transformPoint({1, 0});
    EXPECT_TRUE(approxEqual(rotated, Vec2{0, 1}));
}

TEST(Mat3Test, ScaleScalesPoint) {
    Vec2 scaled = Mat3::scale({2, 3}).transformPoint({4, 5});
    EXPECT_EQ(scaled, (Vec2{8, 15}));
}

TEST(Mat3Test, CompositionMatchesTransformChain) {
    // local = T(position) · S(scale) · T(-anchor)
    Mat3 local = Mat3::translate({10, 20}) * Mat3::scale({2, 2}) * Mat3::translate({-1, -1});
    Vec2 transformed = local.transformPoint({2, 2});
    EXPECT_EQ(transformed, (Vec2{12, 22}));
}

TEST(Mat3Test, WorldIsParentTimesLocal) {
    Mat3 parent = Mat3::translate({100, 0});
    Mat3 local = Mat3::scale({2, 2});
    Vec2 transformed = (parent * local).transformPoint({1, 1});
    EXPECT_EQ(transformed, (Vec2{102, 2}));
}
