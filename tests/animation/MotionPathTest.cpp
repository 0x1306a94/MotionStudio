#include <gtest/gtest.h>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/MotionPath.h"

using motion::Animatable;
using motion::BezierPath;
using motion::BuildMotionPath;
using motion::Keyframe;
using motion::Vec2;

namespace {

Keyframe<Vec2> MakeVec2Keyframe(motion::FrameTime time, Vec2 value) {
    Keyframe<Vec2> keyframe;
    keyframe.time = time;
    keyframe.value = value;
    return keyframe;
}

}  // namespace

TEST(MotionPathTest, FewerThanTwoKeyframesYieldsEmptyPath) {
    Animatable<Vec2> position;
    EXPECT_TRUE(BuildMotionPath(position).vertices.empty());

    position.addKeyframe(MakeVec2Keyframe(0, {0, 0}));
    EXPECT_TRUE(BuildMotionPath(position).vertices.empty());
}

TEST(MotionPathTest, StraightSegmentUsesZeroTangents) {
    Animatable<Vec2> position;
    position.addKeyframe(MakeVec2Keyframe(0, {0, 0}));
    position.addKeyframe(MakeVec2Keyframe(10, {100, 0}));

    BezierPath path = BuildMotionPath(position);
    ASSERT_EQ(path.vertices.size(), 2u);
    EXPECT_FALSE(path.closed);
    EXPECT_EQ(path.vertices[0].point, (Vec2{0, 0}));
    EXPECT_EQ(path.vertices[1].point, (Vec2{100, 0}));
    EXPECT_EQ(path.vertices[0].outTangent, (Vec2{0, 0}));
    EXPECT_EQ(path.vertices[1].inTangent, (Vec2{0, 0}));
}

TEST(MotionPathTest, SpatialSegmentPreservesTangents) {
    Animatable<Vec2> position;
    Keyframe<Vec2> from = MakeVec2Keyframe(0, {0, 0});
    from.spatialOutTangent = Vec2{10, 10};
    Keyframe<Vec2> to = MakeVec2Keyframe(10, {100, 0});
    to.spatialInTangent = Vec2{-10, 10};
    position.addKeyframe(from);
    position.addKeyframe(to);

    BezierPath path = BuildMotionPath(position);
    ASSERT_EQ(path.vertices.size(), 2u);
    EXPECT_EQ(path.vertices[0].outTangent, (Vec2{10, 10}));
    EXPECT_EQ(path.vertices[1].inTangent, (Vec2{-10, 10}));
}
