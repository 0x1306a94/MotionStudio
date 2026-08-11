#pragma once

#include <cstdint>

namespace motion {

enum class GradientType : uint8_t {
    Linear = 0,
    Radial = 1,
    Conic = 2,
    Diamond = 3,
};

}  // namespace motion
