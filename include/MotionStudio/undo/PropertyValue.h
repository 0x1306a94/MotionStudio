#pragma once

#include <string>
#include <variant>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/Vec3.h"
#include "MotionStudio/common/Vec4.h"
#include "MotionStudio/common/VectorNetwork.h"

namespace motion {

// Type-erased property value carried by commands. Dispatched to the correct
// Animatable<T> at execution time based on the active alternative.
using PropertyValue = std::variant<float, Vec2, Vec3, Color, VectorNetwork, std::string, Vec4>;

}  // namespace motion
