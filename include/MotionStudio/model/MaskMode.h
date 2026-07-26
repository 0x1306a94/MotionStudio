#pragma once

namespace motion {

// How a path mask contributes to the layer's coverage (AE Masks subset).
enum class MaskMode {
    Add,
    Subtract,
    Intersect
};

}  // namespace motion
