#pragma once

#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/render/ShaderPaint.h"

namespace motion {

struct EvaluatedGradientStop {
    Color color;
    float position = 0.f;
};

struct EvaluatedGradient {
    GradientType type = GradientType::Linear;
    Vec2 start;
    Vec2 end;
    float startAngle = 0.f;
    float endAngle = 360.f;
    std::vector<EvaluatedGradientStop> stops;
};

// Evaluated paint for DrawPath / StrokePath. Color / Gradient / Shader modes
// are selected by paintMode; inactive fields are unused by the adapter.
// `alpha` is a separate multiplier (like tgfx::Paint::setAlpha); do not bake it
// into `color.a` or shader uniform colors.
struct Paint {
    StylePaintMode paintMode = StylePaintMode::Color;
    Color color;
    float alpha = 1.f;
    FillRule fillRule = FillRule::NonZero;
    BlendMode blendMode = BlendMode::Normal;
    ShaderPaint shader;
    EvaluatedGradient gradient;

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
