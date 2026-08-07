#pragma once

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/render/ShaderPaint.h"

namespace motion {

// Evaluated paint for DrawPath / StrokePath. Color mode uses `color`; Shader
// mode carries a self-contained `shader` snapshot (adapter does not query Document).
// `alpha` is a separate multiplier (like tgfx::Paint::setAlpha); do not bake it
// into `color.a` or shader uniform colors.
struct Paint {
    StylePaintMode paintMode = StylePaintMode::Color;
    Color color;
    float alpha = 1.f;
    FillRule fillRule = FillRule::NonZero;
    BlendMode blendMode = BlendMode::Normal;
    ShaderPaint shader;

    Paint() = default;

    // Solid-color paint used by overlays and tests (Color mode).
    Paint(Color colorValue, FillRule fillRuleValue = FillRule::NonZero,
          BlendMode blendModeValue = BlendMode::Normal)
        : paintMode(StylePaintMode::Color)
        , color(colorValue)
        , fillRule(fillRuleValue)
        , blendMode(blendModeValue) {
    }
};

}  // namespace motion
