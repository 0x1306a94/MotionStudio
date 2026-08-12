#pragma once

#include <memory>
#include <vector>

#include <tgfx/core/BlendMode.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Picture.h>
#include <tgfx/core/PictureRecorder.h>
#include <tgfx/core/Rect.h>

#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/render/MaskApplyMode.h"

namespace tgfx {
class Context;
class Canvas;
class Surface;
}  // namespace tgfx

namespace motion {

struct CoverageImage {
    std::shared_ptr<tgfx::Image> image;
    tgfx::Matrix localMatrix = tgfx::Matrix::I();
    bool invertAlpha = false;
};

struct IsolationLayer {
    tgfx::PictureRecorder contentRecorder;
    tgfx::Canvas *contentCanvas = nullptr;
    tgfx::PictureRecorder maskRecorder;
    tgfx::Canvas *maskCanvas = nullptr;
    // Finite mask target so inverse fill has pixels outside the path (AE Inv).
    std::shared_ptr<tgfx::Surface> maskSurface;
    tgfx::Rect maskSurfaceBounds = tgfx::Rect::MakeEmpty();
    bool masking = false;
    MaskApplyMode maskApplyMode = MaskApplyMode::PathCoverage;
    float savedOpacity = 1.0f;
    // Layer-local AABB of content drawn into contentRecorder; sizes maskSurface.
    tgfx::Rect contentBounds = tgfx::Rect::MakeEmpty();
    std::vector<CoverageImage> coverages;
};

struct TgfxIsolationStack {
    std::vector<IsolationLayer> layers;
};

tgfx::BlendMode ToMaskBlendMode(MaskMode mode);
CoverageImage IntersectCoverageImages(tgfx::Context *context, const CoverageImage &base,
                                      const CoverageImage &next);

}  // namespace motion
