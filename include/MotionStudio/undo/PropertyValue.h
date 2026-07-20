#pragma once

#include <string>
#include <variant>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// 命令携带的属性值（类型擦除，执行时按类型分发到 Animatable<T>）。
using PropertyValue = std::variant<float, Vec2, Color, BezierPath, std::string>;

}  // namespace motion
