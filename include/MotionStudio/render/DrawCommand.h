#pragma once

#include <vector>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/render/Paint.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "MotionStudio/render/StrokeOptions.h"

namespace motion {

enum class DrawCommandType {
    Save,
    Restore,
    ConcatTransform,
    SetOpacity,
    SetBlendMode,
    DrawPath,
    StrokePath,
    ClipPath,
};

// Flat tagged draw command. Only the fields belonging to the command's type
// are meaningful; the rest keep their defaults.
struct DrawCommand {
    DrawCommandType type = DrawCommandType::Save;
    Mat3 transform;                           // ConcatTransform
    float opacity = 1;                        // SetOpacity
    BlendMode blendMode = BlendMode::Normal;  // SetBlendMode
    ShapeGeometry geometry;                   // DrawPath / StrokePath / ClipPath
    Paint paint;                              // DrawPath / StrokePath
    StrokeOptions stroke;                     // StrokePath
    FillRule fillRule = FillRule::NonZero;    // ClipPath
};

using DrawCommandList = std::vector<DrawCommand>;

}  // namespace motion
