#pragma once

#include <cstdint>

namespace motion {

// Whether a path stroke is drawn solid or with a dash pattern.
enum class StrokeMode : uint8_t {
    Solid = 0,
    Dashed = 1
};

}  // namespace motion
