#pragma once

namespace motion {

// RGBA color with each component in linear space [0, 1].
struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;

    bool operator==(const Color &other) const;
    bool operator!=(const Color &other) const;
};

}  // namespace motion
