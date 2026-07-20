#pragma once

#include <string>
#include <variant>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// 命令携带的关键帧（类型擦除，执行时按类型分发到 Animatable<T>）。
using KeyframeData =
    std::variant<Keyframe<float>, Keyframe<Vec2>, Keyframe<Color>,
                 Keyframe<BezierPath>, Keyframe<std::string>>;

}  // namespace motion
