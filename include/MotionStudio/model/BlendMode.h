#pragma once

namespace motion {

// Compositing blend mode for a layer. Matches the Lottie "bm" value set.
enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
    Add
};

}  // namespace motion
