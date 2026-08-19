#pragma once

#include <memory>
#include <vector>

#include <tgfx/core/BlendMode.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Picture.h>
#include <tgfx/core/PictureRecorder.h>
#include <tgfx/core/Rect.h>

#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/render/MaskApplyMode.h"

namespace tgfx {
class Context;
class Canvas;
class Surface;
}  // namespace tgfx

namespace motion {

class RenderCache;

struct CoverageImage {
    std::shared_ptr<tgfx::Image> image;
    tgfx::Matrix localMatrix = tgfx::Matrix::I();
    bool invertAlpha = false;
};

struct IsolationLayer {
    IsolationLayer() = default;
    IsolationLayer(const IsolationLayer &) = delete;
    IsolationLayer &operator=(const IsolationLayer &) = delete;
    IsolationLayer(IsolationLayer &&) = delete;
    IsolationLayer &operator=(IsolationLayer &&) = delete;

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
    float compositeOpacity = 1.0f;
    BlendMode compositeBlend = BlendMode::Normal;
    // Layer-local AABB of content drawn into contentRecorder; sizes maskSurface.
    tgfx::Rect contentBounds = tgfx::Rect::MakeEmpty();
    std::vector<CoverageImage> coverages;
};

struct TgfxIsolationStack {
    // PictureRecorder owns a raw Canvas* and is not safely movable; keep
    // layers in unique_ptr so vector growth never relocates a recorder.
    std::vector<std::unique_ptr<IsolationLayer>> layers;
};

tgfx::BlendMode ToMaskBlendMode(MaskMode mode);
CoverageImage IntersectCoverageImages(tgfx::Context *context, RenderCache *cache,
                                      const CoverageImage &base, const CoverageImage &next);

}  // namespace motion
