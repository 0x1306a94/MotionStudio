#include "TgfxIsolation.h"

#include <algorithm>
#include <cmath>

#include <tgfx/core/BlendMode.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Context.h>

namespace motion {

tgfx::BlendMode ToMaskBlendMode(MaskMode mode) {
    switch (mode) {
        case MaskMode::Add: {
            return tgfx::BlendMode::SrcOver;
        }
        case MaskMode::Subtract: {
            return tgfx::BlendMode::DstOut;
        }
        case MaskMode::Intersect: {
            return tgfx::BlendMode::DstIn;
        }
    }
    return tgfx::BlendMode::SrcOver;
}

CoverageImage IntersectCoverageImages(tgfx::Context *context, const CoverageImage &base,
                                      const CoverageImage &next) {
    CoverageImage result;
    if (context == nullptr || base.image == nullptr || next.image == nullptr) {
        return result;
    }
    const tgfx::Rect baseBounds = base.localMatrix.mapRect(
        tgfx::Rect::MakeWH(static_cast<float>(base.image->width()),
                           static_cast<float>(base.image->height())));
    const tgfx::Rect nextBounds = next.localMatrix.mapRect(
        tgfx::Rect::MakeWH(static_cast<float>(next.image->width()),
                           static_cast<float>(next.image->height())));
    tgfx::Rect bounds = baseBounds;
    bounds.join(nextBounds);
    bounds.roundOut();
    const int width = std::max(static_cast<int>(std::ceil(bounds.width())), 1);
    const int height = std::max(static_cast<int>(std::ceil(bounds.height())), 1);
    auto surface = tgfx::Surface::Make(context, width, height);
    if (surface == nullptr) {
        return result;
    }
    tgfx::Canvas *canvas = surface->getCanvas();
    canvas->clear();
    {
        tgfx::Paint paint;
        paint.setAntiAlias(true);
        tgfx::Matrix matrix = base.localMatrix;
        matrix.postTranslate(-bounds.left, -bounds.top);
        canvas->setMatrix(matrix);
        canvas->drawImage(base.image, &paint);
    }
    {
        tgfx::Paint paint;
        paint.setAntiAlias(true);
        paint.setBlendMode(tgfx::BlendMode::DstIn);
        tgfx::Matrix matrix = next.localMatrix;
        matrix.postTranslate(-bounds.left, -bounds.top);
        canvas->setMatrix(matrix);
        canvas->drawImage(next.image, &paint);
    }
    result.image = surface->makeImageSnapshot();
    result.localMatrix = tgfx::Matrix::MakeTrans(bounds.left, bounds.top);
    return result;
}

}  // namespace motion
