#pragma once

#include <string>
#include <variant>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Type-erased property value carried by commands. Dispatched to the correct
// Animatable<T> at execution time based on the active alternative.
using PropertyValue = std::variant<float, Vec2, Color, BezierPath, std::string>;

}  // namespace motion
