#pragma once

#include <cstdint>

namespace motion {

// Horizontal alignment of text lines within the layer box.
enum class TextAlign : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2,
};

}  // namespace motion
