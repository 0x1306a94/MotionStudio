#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Layer transform with five animatable properties. Every Layer owns one.
// Local matrix: T(position) * R(rotation) * S(scale) * T(-anchorPoint),
// then left-multiplied by the parent's world transform.
struct Transform {
    Animatable<Vec2> anchorPoint{Vec2{0, 0}};
    Animatable<Vec2> position{Vec2{0, 0}};
    Animatable<Vec2> scale{Vec2{1, 1}};
    Animatable<float> rotation{0.0f};  // degrees
    Animatable<float> opacity{1.0f};   // 0.0 ~ 1.0
};

}  // namespace motion
