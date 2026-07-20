#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Math.h"

namespace motion {

// 图层变换，5 个可动画属性。每个 Layer 必有。
// 世界变换：local = T(position) · R(rotation) · S(scale) · T(-anchorPoint)，
// 再左乘父级世界变换。
struct Transform {
    Animatable<Vec2> anchorPoint{Vec2{0, 0}};
    Animatable<Vec2> position{Vec2{0, 0}};
    Animatable<Vec2> scale{Vec2{1, 1}};
    Animatable<float> rotation{0.0f};   // 度
    Animatable<float> opacity{1.0f};    // 0.0 ~ 1.0
};

}  // namespace motion
