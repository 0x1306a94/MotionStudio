#include "TgfxTypeConvert.h"

#include <algorithm>
#include <cmath>

namespace motion {

uint8_t ToByte(float value) {
    const float clamped = std::min(std::max(value, 0.0f), 1.0f);
    return uint8_t(std::lround(clamped * 255.0f));
}

tgfx::Color ToTgfxColor(const Color &color) {
    return tgfx::Color::FromRGBA(ToByte(color.r), ToByte(color.g), ToByte(color.b), ToByte(color.a));
}

tgfx::BlendMode ToTgfxBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: {
            return tgfx::BlendMode::SrcOver;
        }
        case BlendMode::Multiply: {
            return tgfx::BlendMode::Multiply;
        }
        case BlendMode::Screen: {
            return tgfx::BlendMode::Screen;
        }
        case BlendMode::Overlay: {
            return tgfx::BlendMode::Overlay;
        }
        case BlendMode::Darken: {
            return tgfx::BlendMode::Darken;
        }
        case BlendMode::Lighten: {
            return tgfx::BlendMode::Lighten;
        }
        case BlendMode::ColorDodge: {
            return tgfx::BlendMode::ColorDodge;
        }
        case BlendMode::ColorBurn: {
            return tgfx::BlendMode::ColorBurn;
        }
        case BlendMode::HardLight: {
            return tgfx::BlendMode::HardLight;
        }
        case BlendMode::SoftLight: {
            return tgfx::BlendMode::SoftLight;
        }
        case BlendMode::Difference: {
            return tgfx::BlendMode::Difference;
        }
        case BlendMode::Exclusion: {
            return tgfx::BlendMode::Exclusion;
        }
        case BlendMode::Hue: {
            return tgfx::BlendMode::Hue;
        }
        case BlendMode::Saturation: {
            return tgfx::BlendMode::Saturation;
        }
        case BlendMode::Color: {
            return tgfx::BlendMode::Color;
        }
        case BlendMode::Luminosity: {
            return tgfx::BlendMode::Luminosity;
        }
        case BlendMode::Add: {
            return tgfx::BlendMode::PlusLighter;
        }
    }
    return tgfx::BlendMode::SrcOver;
}

tgfx::LineCap ToTgfxLineCap(LineCap cap) {
    switch (cap) {
        case LineCap::Butt: {
            return tgfx::LineCap::Butt;
        }
        case LineCap::Round: {
            return tgfx::LineCap::Round;
        }
        case LineCap::Square: {
            return tgfx::LineCap::Square;
        }
    }
    return tgfx::LineCap::Butt;
}

tgfx::LineJoin ToTgfxLineJoin(LineJoin join) {
    switch (join) {
        case LineJoin::Miter: {
            return tgfx::LineJoin::Miter;
        }
        case LineJoin::Round: {
            return tgfx::LineJoin::Round;
        }
        case LineJoin::Bevel: {
            return tgfx::LineJoin::Bevel;
        }
    }
    return tgfx::LineJoin::Miter;
}

tgfx::PathFillType ToTgfxFillType(FillRule fillRule) {
    return fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd : tgfx::PathFillType::Winding;
}

tgfx::Matrix ToTgfxMatrix(const Mat3 &matrix) {
    tgfx::Matrix result;
    result.setAll(matrix.values[0], matrix.values[1], matrix.values[2], matrix.values[3],
                  matrix.values[4], matrix.values[5]);
    return result;
}

}  // namespace motion
