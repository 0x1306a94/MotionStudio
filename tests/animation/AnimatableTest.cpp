#include <gtest/gtest.h>

#include "MotionStudio/animation/Animatable.h"

using motion::Animatable;
using motion::Easing;
using motion::FrameTime;
using motion::Keyframe;
using motion::Vec2;

namespace {
Keyframe<float> FloatKeyframe(FrameTime time, float value,
                              Easing easing = Easing::Linear()) {
    Keyframe<float> keyframe;
    keyframe.time = time;
    keyframe.value = value;
    keyframe.easing = easing;
    return keyframe;
}

Keyframe<Vec2> Vec2Keyframe(FrameTime time, Vec2 value,
                            Easing easing = Easing::Linear()) {
    Keyframe<Vec2> keyframe;
    keyframe.time = time;
    keyframe.value = value;
    keyframe.easing = easing;
    return keyframe;
}
}  // namespace

TEST(AnimatableTest, StaticValueWhenNoKeyframes) {
    Animatable<float> animatable{5.0f};
    EXPECT_FALSE(animatable.isAnimated());
    EXPECT_FLOAT_EQ(animatable.evaluate(100), 5.0f);

    animatable.setStaticValue(7.0f);
    EXPECT_FLOAT_EQ(animatable.staticValue(), 7.0f);
}

TEST(AnimatableTest, AddKeyframeKeepsAscendingOrder) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(20, 2));
    animatable.addKeyframe(FloatKeyframe(0, 0));
    animatable.addKeyframe(FloatKeyframe(10, 1));

    ASSERT_EQ(animatable.keyframes().size(), 3u);
    EXPECT_EQ(animatable.keyframes()[0].time, FrameTime(0));
    EXPECT_EQ(animatable.keyframes()[1].time, FrameTime(10));
    EXPECT_EQ(animatable.keyframes()[2].time, FrameTime(20));
    EXPECT_TRUE(animatable.isAnimated());
}

TEST(AnimatableTest, AddKeyframeReplacesSameTime) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(10, 1));
    animatable.addKeyframe(FloatKeyframe(10, 99));

    ASSERT_EQ(animatable.keyframes().size(), 1u);
    EXPECT_FLOAT_EQ(animatable.keyframes()[0].value, 99.0f);
}

TEST(AnimatableTest, EvaluateClampsOutsideRange) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(10, 1));
    animatable.addKeyframe(FloatKeyframe(20, 2));

    EXPECT_FLOAT_EQ(animatable.evaluate(0), 1.0f);
    EXPECT_FLOAT_EQ(animatable.evaluate(10), 1.0f);
    EXPECT_FLOAT_EQ(animatable.evaluate(30), 2.0f);
}

TEST(AnimatableTest, EvaluateLinearInterpolation) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(0, 0));
    animatable.addKeyframe(FloatKeyframe(10, 100));

    EXPECT_FLOAT_EQ(animatable.evaluate(5), 50.0f);
}

TEST(AnimatableTest, EvaluateHoldKeepsPreviousValue) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(0, 0, Easing::Hold()));
    animatable.addKeyframe(FloatKeyframe(10, 100));

    EXPECT_FLOAT_EQ(animatable.evaluate(9), 0.0f);
}

TEST(AnimatableTest, EvaluateSymmetricBezierAtMidpoint) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(0, 0, Easing::Bezier(0.5f, 0, 0.5f, 1)));
    animatable.addKeyframe(FloatKeyframe(10, 100));

    EXPECT_NEAR(animatable.evaluate(5), 50.0f, 1e-3f);
}

TEST(AnimatableTest, EvaluateEaseInLagsBelowLinear) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(0, 0, Easing::EaseIn()));
    animatable.addKeyframe(FloatKeyframe(10, 100));

    EXPECT_LT(animatable.evaluate(5), 50.0f);
}

TEST(AnimatableTest, EvaluateUsesFirstKeyframeEasingPerSegment) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(0, 0, Easing::Hold()));
    animatable.addKeyframe(FloatKeyframe(10, 10, Easing::Linear()));
    animatable.addKeyframe(FloatKeyframe(20, 20));

    EXPECT_FLOAT_EQ(animatable.evaluate(5), 0.0f);   // 第一段 Hold
    EXPECT_FLOAT_EQ(animatable.evaluate(15), 15.0f); // 第二段 Linear
}

TEST(AnimatableTest, EvaluateSpatialArcDiffersFromStraightLine) {
    Animatable<Vec2> animatable;
    Keyframe<Vec2> from = Vec2Keyframe(0, {0, 0});
    from.spatialOutTangent = Vec2{10, 10};
    Keyframe<Vec2> to = Vec2Keyframe(10, {100, 0});
    to.spatialInTangent = Vec2{-10, 10};
    animatable.addKeyframe(from);
    animatable.addKeyframe(to);

    // 三次贝塞尔中点：P0=(0,0) P1=(10,10) P2=(90,10) P3=(100,0) → (50, 7.5)
    Vec2 mid = animatable.evaluate(5);
    EXPECT_TRUE(motion::ApproxEqual(mid, Vec2{50, 7.5f}));
}

TEST(AnimatableTest, EvaluateFallsBackToLineWithoutTangents) {
    Animatable<Vec2> animatable;
    animatable.addKeyframe(Vec2Keyframe(0, {0, 0}));
    animatable.addKeyframe(Vec2Keyframe(10, {100, 0}));

    Vec2 mid = animatable.evaluate(5);
    EXPECT_EQ(mid, (Vec2{50, 0}));
}

TEST(AnimatableTest, TakeAndRemoveKeyframe) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(10, 1));

    EXPECT_FALSE(animatable.takeKeyframe(20).has_value());

    auto taken = animatable.takeKeyframe(10);
    ASSERT_TRUE(taken.has_value());
    EXPECT_FLOAT_EQ(taken->value, 1.0f);
    EXPECT_FALSE(animatable.isAnimated());

    animatable.addKeyframe(FloatKeyframe(10, 1));
    animatable.removeKeyframe(10);
    EXPECT_FALSE(animatable.isAnimated());
}

TEST(AnimatableTest, UpdateKeyframeOnlyWhenExists) {
    Animatable<float> animatable;
    animatable.addKeyframe(FloatKeyframe(10, 1));

    EXPECT_FALSE(animatable.updateKeyframe(20, FloatKeyframe(20, 2)));
    EXPECT_TRUE(animatable.updateKeyframe(10, FloatKeyframe(10, 99)));
    EXPECT_FLOAT_EQ(animatable.keyframes()[0].value, 99.0f);
}

TEST(AnimatableTest, ClearKeyframesFallsBackToStatic) {
    Animatable<float> animatable{3.0f};
    animatable.addKeyframe(FloatKeyframe(0, 0));
    animatable.clearKeyframes();

    EXPECT_FALSE(animatable.isAnimated());
    EXPECT_FLOAT_EQ(animatable.evaluate(5), 3.0f);
}
