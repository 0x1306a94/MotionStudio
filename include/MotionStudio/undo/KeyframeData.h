#pragma once

#include <string>
#include <variant>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/VectorNetwork.h"

namespace motion {

// Type-erased keyframe carried by commands. Dispatched to the correct
// Animatable<T> at execution time based on the active alternative.
using KeyframeData =
    std::variant<Keyframe<float>, Keyframe<Vec2>, Keyframe<Color>,
                 Keyframe<VectorNetwork>, Keyframe<std::string>>;

}  // namespace motion
