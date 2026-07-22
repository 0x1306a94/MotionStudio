#include "TgfxCanvasAdapter.h"

#include <algorithm>
#include <cmath>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/PathTypes.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Device.h>

namespace motion {

namespace {

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

// Converts a BezierPath (relative tangents) into a tgfx path with absolute
// control points.
tgfx::Path ToTgfxPath(const BezierPath &path, FillRule fillRule) {
    tgfx::Path result;
    if (path.vertices.empty()) {
        result.setFillType(fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd
                                                         : tgfx::PathFillType::Winding);
        return result;
    }
    const BezierPath::Vertex &first = path.vertices.front();
    result.moveTo(first.point.x, first.point.y);
    for (size_t i = 1; i < path.vertices.size(); ++i) {
        const BezierPath::Vertex &previous = path.vertices[i - 1];
        const BezierPath::Vertex &current = path.vertices[i];
        result.cubicTo(previous.point.x + previous.outTangent.x,
                       previous.point.y + previous.outTangent.y,
                       current.point.x + current.inTangent.x,
                       current.point.y + current.inTangent.y, current.point.x,
                       current.point.y);
    }
    if (path.closed && path.vertices.size() > 1) {
        const BezierPath::Vertex &last = path.vertices.back();
        result.cubicTo(last.point.x + last.outTangent.x, last.point.y + last.outTangent.y,
                       first.point.x + first.inTangent.x, first.point.y + first.inTangent.y,
                       first.point.x, first.point.y);
        result.close();
    }
    result.setFillType(fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd
                                                     : tgfx::PathFillType::Winding);
    return result;
}

tgfx::Matrix ToTgfxMatrix(const Mat3 &matrix) {
    tgfx::Matrix result;
    result.setAll(matrix.values[0], matrix.values[1], matrix.values[2], matrix.values[3],
                  matrix.values[4], matrix.values[5]);
    return result;
}

}  // namespace

TgfxCanvasAdapter::TgfxCanvasAdapter() = default;

TgfxCanvasAdapter::~TgfxCanvasAdapter() {
    frameRestore_.reset();
}

void TgfxCanvasAdapter::drawPreviewBackdrop() {
}

void TgfxCanvasAdapter::onFrameReady(int sceneWidth, int sceneHeight, Color backgroundColor) {
    if (!surface_ || sceneWidth <= 0 || sceneHeight <= 0) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    const tgfx::Rect compositionBounds = tgfx::Rect::MakeWH(float(sceneWidth), float(sceneHeight));
    tgfx::Paint paint;
    paint.setStyle(tgfx::PaintStyle::Fill);
    paint.setColor(ToTgfxColor(backgroundColor));
    // Black chrome: Src replaces coverage cleanly. Transparency grid: SrcOver
    // so AA edges blend into the checkerboard instead of writing translucent
    // pixels that composite against the MTKView's black clear.
    paint.setBlendMode(compositionBackgroundSrcOver() ? tgfx::BlendMode::SrcOver : tgfx::BlendMode::Src);
    canvas->drawRect(compositionBounds, paint);
    // AE-style hard clip to the composition rect for subsequent layer draws.
    canvas->clipRect(compositionBounds, false);
}

void TgfxCanvasAdapter::beginFrame(int width, int height, Color clearColor) {
    // Release before acquireTarget: size changes may destroy the old surface/canvas.
    frameRestore_.reset();
    if (!acquireTarget(width, height) || !surface_) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    frameRestore_ = std::make_unique<tgfx::AutoCanvasRestore>(canvas);
    drawPreviewBackdrop();
    onFrameReady(width, height, clearColor);
    opacity_ = 1;
    blendMode_ = BlendMode::Normal;
    opacityStack_.clear();
    blendStack_.clear();
}

void TgfxCanvasAdapter::endFrame() {
    frameRestore_.reset();
    presentTarget();
}

void TgfxCanvasAdapter::save() {
    if (!surface_) {
        return;
    }
    surface_->getCanvas()->save();
    opacityStack_.push_back(opacity_);
    blendStack_.push_back(blendMode_);
}

void TgfxCanvasAdapter::restore() {
    if (!surface_ || opacityStack_.empty()) {
        return;
    }
    surface_->getCanvas()->restore();
    opacity_ = opacityStack_.back();
    opacityStack_.pop_back();
    blendMode_ = blendStack_.back();
    blendStack_.pop_back();
}

void TgfxCanvasAdapter::concatTransform(const Mat3 &matrix) {
    if (!surface_) {
        return;
    }
    surface_->getCanvas()->concat(ToTgfxMatrix(matrix));
}

void TgfxCanvasAdapter::setOpacity(float opacity) {
    opacity_ = opacity;
}

void TgfxCanvasAdapter::setBlendMode(BlendMode mode) {
    blendMode_ = mode;
}

void TgfxCanvasAdapter::drawPath(const BezierPath &path, const Paint &paint) {
    if (!surface_) {
        return;
    }
    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    Color color = paint.color;
    color.a *= opacity_;
    tgfxPaint.setColor(ToTgfxColor(color));
    tgfxPaint.setBlendMode(ToTgfxBlendMode(blendMode_));
    surface_->getCanvas()->drawPath(ToTgfxPath(path, paint.fillRule), tgfxPaint);
}

void TgfxCanvasAdapter::strokePath(const BezierPath &path, const Paint &paint, float width, LineCap cap, LineJoin join) {
    if (!surface_) {
        return;
    }
    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setStyle(tgfx::PaintStyle::Stroke);
    tgfxPaint.setStrokeWidth(width);
    tgfxPaint.setLineCap(ToTgfxLineCap(cap));
    tgfxPaint.setLineJoin(ToTgfxLineJoin(join));
    Color color = paint.color;
    color.a *= opacity_;
    tgfxPaint.setColor(ToTgfxColor(color));
    tgfxPaint.setBlendMode(ToTgfxBlendMode(blendMode_));
    surface_->getCanvas()->drawPath(ToTgfxPath(path, paint.fillRule), tgfxPaint);
}

void TgfxCanvasAdapter::clipPath(const BezierPath &path, FillRule rule) {
    if (!surface_) {
        return;
    }
    surface_->getCanvas()->clipPath(ToTgfxPath(path, rule));
}

}  // namespace motion
