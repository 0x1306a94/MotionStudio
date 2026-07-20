#pragma once

#include <string>
#include <variant>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Type-erased keyframe carried by commands. Dispatched to the correct
// Animatable<T> at execution time based on the active alternative.
using KeyframeData =
    std::variant<Keyframe<float>, Keyframe<Vec2>, Keyframe<Color>,
                 Keyframe<BezierPath>, Keyframe<std::string>>;

}  // namespace motion
