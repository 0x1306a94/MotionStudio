#pragma once

#include <cstdint>

namespace motion {

// XOR paint source for FillStyle / StrokeStyle: solid color or document shader.
enum class StylePaintMode : uint8_t {
    Color = 0,
    Shader = 1,
};

}  // namespace motion
