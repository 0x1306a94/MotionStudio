#pragma once

#include <cstdint>

namespace motion {

// How image pixels map into the layer container. Values align with PAGScaleMode.
enum class ImageScaleMode : uint8_t {
    None = 0,
    Stretch = 1,
    LetterBox = 2,
    Zoom = 3,
};

}  // namespace motion
