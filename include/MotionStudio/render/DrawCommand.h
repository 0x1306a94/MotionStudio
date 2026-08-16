#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/render/MaskApplyMode.h"
#include "MotionStudio/render/Paint.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "MotionStudio/render/StrokeOptions.h"
#include "MotionStudio/render/TextDrawParams.h"

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
    BeginLayer,
    EndLayer,
    BeginMask,
    EndMask,
    DrawMaskPath,
    DrawImage,
    DrawText,
};

// Flat tagged draw command. Only the fields belonging to the command's type
// are meaningful; the rest keep their defaults.
struct DrawCommand {
    DrawCommandType type = DrawCommandType::Save;
    Mat3 transform;                                             // ConcatTransform
    float opacity = 1;                                          // SetOpacity
    BlendMode blendMode = BlendMode::Normal;                    // SetBlendMode
    ShapeGeometry geometry;                                     // DrawPath / StrokePath / ClipPath / DrawMaskPath
    Paint paint;                                                // DrawPath / StrokePath
    StrokeOptions stroke;                                       // StrokePath
    FillRule fillRule = FillRule::NonZero;                      // ClipPath
    MaskApplyMode maskApplyMode = MaskApplyMode::PathCoverage;  // BeginMask
    MaskMode maskMode = MaskMode::Add;                          // DrawMaskPath
    float maskOpacity = 1.0f;                                   // DrawMaskPath
    bool maskInverted = false;                                  // DrawMaskPath
    float maskFeather = 0.0f;                                   // DrawMaskPath
    float maskExpansion = 0.0f;                                 // DrawMaskPath
    std::string imagePath;                                      // DrawImage (absolute)
    Vec2 imageContainerSize;                                    // DrawImage
    Vec2 imageIntrinsicSize;                                    // DrawImage
    ImageScaleMode imageScaleMode = ImageScaleMode::LetterBox;  // DrawImage
    TextDrawParams textParams;                                  // DrawText
    std::vector<std::shared_ptr<const LayerEffect>> effects;    // EndLayer
    std::vector<std::shared_ptr<const LayerFx>> layerStyles;    // EndLayer
};

using DrawCommandList = std::vector<DrawCommand>;

}  // namespace motion
