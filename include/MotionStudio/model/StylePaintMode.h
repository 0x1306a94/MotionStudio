#pragma once

#include <cstdint>

namespace motion {

// Active paint kind for FillStyle / StrokeStyle. Selects which field is
// evaluated (color / gradient / shader); the other fields coexist and are not
// cleared when switching kind.
enum class StylePaintMode : uint8_t {
    Color = 0,
    Shader = 1,
    Gradient = 2,
};

}  // namespace motion
