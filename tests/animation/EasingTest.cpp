#include <gtest/gtest.h>

#include "MotionStudio/animation/Easing.h"

using motion::ApplyEasing;
using motion::Easing;
using motion::EasingType;
using motion::SolveBezierEasing;

TEST(EasingTest, Presets) {
    EXPECT_EQ(Easing::Linear().type, EasingType::Linear);
    EXPECT_EQ(Easing::Hold().type, EasingType::Hold);
    EXPECT_EQ(Easing::Ease().type, EasingType::Ease);
    EXPECT_EQ(Easing::EaseIn().type, EasingType::EaseIn);
    EXPECT_EQ(Easing::EaseOut().type, EasingType::EaseOut);
    EXPECT_EQ(Easing::EaseInOut().type, EasingType::EaseInOut);
    EXPECT_FLOAT_EQ(Easing::EaseIn().inX, 0.42f);
    EXPECT_FLOAT_EQ(Easing::EaseOut().outX, 0.58f);
}

TEST(EasingTest, BezierEqualityUsesTolerance) {
    EXPECT_EQ(Easing::Bezier(0.42000001f, 0, 1, 1),
              Easing::Bezier(0.42f, 0, 1, 1));
}

TEST(ApplyEasingTest, LinearIsIdentity) {
    EXPECT_FLOAT_EQ(ApplyEasing(Easing::Linear(), 0.3f), 0.3f);
}

TEST(ApplyEasingTest, HoldStaysAtStartValue) {
    EXPECT_FLOAT_EQ(ApplyEasing(Easing::Hold(), 0.9f), 0.0f);
}

TEST(SolveBezierEasingTest, ClampsAtEnds) {
    EXPECT_FLOAT_EQ(SolveBezierEasing(0.42f, 0, 1, 1, 0), 0);
    EXPECT_FLOAT_EQ(SolveBezierEasing(0.42f, 0, 1, 1, 1), 1);
}

TEST(SolveBezierEasingTest, LinearControlPointsGiveIdentity) {
    for (float x = 0.1f; x < 1.0f; x += 0.1f) {
        EXPECT_NEAR(SolveBezierEasing(0, 0, 1, 1, x), x, 1e-5f);
    }
}

TEST(SolveBezierEasingTest, SymmetricCurvePassesThroughCenter) {
    // cubic-bezier(0.5, 0, 0.5, 1) is symmetric about the center (0.5, 0.5).
    EXPECT_NEAR(SolveBezierEasing(0.5f, 0, 0.5f, 1, 0.5f), 0.5f, 1e-5f);
}

TEST(SolveBezierEasingTest, EaseInLagsBehindLinear) {
    EXPECT_LT(SolveBezierEasing(0.42f, 0, 1, 1, 0.5f), 0.5f);
}

TEST(SolveBezierEasingTest, YOvershootIsAllowed) {
    // Y control points outside [0,1] cause overshoot: mid-range y < 0.
    EXPECT_LT(SolveBezierEasing(0.5f, -0.5f, 0.5f, 1.5f, 0.25f), 0.0f);
}
