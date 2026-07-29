#pragma once

#include <memory>
#include <vector>

#include <tgfx/core/BlendMode.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Picture.h>
#include <tgfx/core/PictureRecorder.h>

#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/render/MaskApplyMode.h"

namespace tgfx {
class Context;
class Canvas;
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
    bool masking = false;
    MaskApplyMode maskApplyMode = MaskApplyMode::PathCoverage;
    float savedOpacity = 1.0f;
    std::vector<CoverageImage> coverages;
};

struct TgfxIsolationStack {
    std::vector<IsolationLayer> layers;
};

tgfx::BlendMode ToMaskBlendMode(MaskMode mode);
CoverageImage IntersectCoverageImages(tgfx::Context *context, const CoverageImage &base,
                                      const CoverageImage &next);

}  // namespace motion
