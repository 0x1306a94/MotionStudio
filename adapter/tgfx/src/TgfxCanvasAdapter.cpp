#include "TgfxCanvasAdapter.h"

#include <algorithm>
#include <utility>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/ColorFilter.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/ImageFilter.h>
#include <tgfx/core/MaskFilter.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/Shader.h>
#include <tgfx/core/Stroke.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Context.h>

#include "MotionStudio/render/ImageScaleLayout.h"
#include "TgfxImageCache.h"
#include "TgfxIsolation.h"
#include "TgfxPathCache.h"
#include "TgfxProfileTiming.h"
#include "TgfxTypeConvert.h"

namespace motion {

namespace {

void DrawMaskPathContribution(tgfx::Canvas *target, const tgfx::Path &path, float opacity,
                              MaskMode mode) {
    tgfx::Paint paint;
    paint.setAntiAlias(true);
    paint.setStyle(tgfx::PaintStyle::Fill);
    paint.setColor(tgfx::Color::FromRGBA(255, 255, 255, ToByte(opacity)));
    paint.setBlendMode(ToMaskBlendMode(mode));
    target->drawPath(path, paint);
}

}  // namespace

TgfxCanvasAdapter::TgfxCanvasAdapter()
    : pathCache_(std::make_unique<TgfxPathCache>())
    , isolationStack_(std::make_unique<TgfxIsolationStack>())
    , imageCache_(std::make_unique<TgfxImageCache>()) {
}

TgfxCanvasAdapter::~TgfxCanvasAdapter() {
    frameRestore_.reset();
}

void TgfxCanvasAdapter::releaseGpuCaches(tgfx::Context *context) {
    frameRestore_.reset();
    compositionClipSaved_ = false;
    if (pathCache_ != nullptr) {
        pathCache_->Clear();
    }
    if (imageCache_ != nullptr) {
        imageCache_->Clear();
    }
    if (isolationStack_ != nullptr) {
        isolationStack_->layers.clear();
    }
    surface_.reset();
    if (context != nullptr) {
        context->purgeResourcesUntilMemoryTo(0);
    }
}

void TgfxCanvasAdapter::drawPreviewBackdrop() {
}

void TgfxCanvasAdapter::onFrameReady(int sceneWidth, int sceneHeight, Color backgroundColor,
                                     float cornerRadius) {
    if (!surface_ || sceneWidth <= 0 || sceneHeight <= 0) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    const tgfx::Rect compositionBounds =
        tgfx::Rect::MakeWH(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight));
    const float radius =
        std::clamp(cornerRadius, 0.0f,
                   std::min(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight)) * 0.5f);
    tgfx::Paint paint;
    paint.setStyle(tgfx::PaintStyle::Fill);
    paint.setColor(ToTgfxColor(backgroundColor));
    if (radius > 0.0f) {
        canvas->drawRoundRect(compositionBounds, radius, radius, paint);
        canvas->save();
        compositionClipSaved_ = true;
        tgfx::Path clipPath;
        clipPath.addRoundRect(compositionBounds, radius, radius);
        canvas->clipPath(clipPath);
    } else {
        canvas->drawRect(compositionBounds, paint);
        canvas->save();
        compositionClipSaved_ = true;
        canvas->clipRect(compositionBounds, false);
    }
}

void TgfxCanvasAdapter::beginFrame(int width, int height, Color backgroundColor,
                                   float cornerRadius) {
    // Release before acquireTarget: size changes may destroy the old surface/canvas.
    frameRestore_.reset();
    if (!acquireTarget(width, height) || !surface_) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    frameRestore_ = std::make_unique<tgfx::AutoCanvasRestore>(canvas);
    compositionClipSaved_ = false;
    drawPreviewBackdrop();
    onFrameReady(width, height, backgroundColor, cornerRadius);
    opacity_ = 1;
    blendMode_ = BlendMode::Normal;
    opacityStack_.clear();
    blendStack_.clear();
    if (isolationStack_ != nullptr) {
        isolationStack_->layers.clear();
    }
}

void TgfxCanvasAdapter::endFrame() {
    endFrameProfile_ = {};
    const auto restoreStart = TgfxProfileClock::now();
    frameRestore_.reset();
    compositionClipSaved_ = false;
    const auto restoreEnd = TgfxProfileClock::now();
    endFrameProfile_.canvasRestoreMs = Milliseconds(restoreStart, restoreEnd);

    const auto presentStart = TgfxProfileClock::now();
    presentTarget();
    const auto presentEnd = TgfxProfileClock::now();
    endFrameProfile_.presentTargetMs = Milliseconds(presentStart, presentEnd);
}

const EndFrameProfile &TgfxCanvasAdapter::endFrameProfile() const {
    return endFrameProfile_;
}

void TgfxCanvasAdapter::restoreCompositionClip() {
    if (!surface_ || !compositionClipSaved_) {
        return;
    }
    surface_->getCanvas()->restore();
    compositionClipSaved_ = false;
}

tgfx::Canvas *TgfxCanvasAdapter::drawingCanvas() {
    if (isolationStack_ != nullptr && !isolationStack_->layers.empty()) {
        IsolationLayer &top = isolationStack_->layers.back();
        if (top.masking && top.maskCanvas != nullptr) {
            return top.maskCanvas;
        }
        if (top.contentCanvas != nullptr) {
            return top.contentCanvas;
        }
    }
    return surface_ != nullptr ? surface_->getCanvas() : nullptr;
}

void TgfxCanvasAdapter::save() {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr) {
        return;
    }
    canvas->save();
    opacityStack_.push_back(opacity_);
    blendStack_.push_back(blendMode_);
}

void TgfxCanvasAdapter::restore() {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || opacityStack_.empty()) {
        return;
    }
    canvas->restore();
    opacity_ = opacityStack_.back();
    opacityStack_.pop_back();
    blendMode_ = blendStack_.back();
    blendStack_.pop_back();
}

void TgfxCanvasAdapter::concatTransform(const Mat3 &matrix) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr) {
        return;
    }
    canvas->concat(ToTgfxMatrix(matrix));
}

void TgfxCanvasAdapter::setOpacity(float opacity) {
    opacity_ = opacity;
}

void TgfxCanvasAdapter::setBlendMode(BlendMode mode) {
    blendMode_ = mode;
}

void TgfxCanvasAdapter::drawPath(const ShapeGeometry &geometry, const Paint &paint) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || !pathCache_) {
        return;
    }
    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    Color color = paint.color;
    color.a *= opacity_;
    tgfxPaint.setColor(ToTgfxColor(color));
    tgfxPaint.setBlendMode(ToTgfxBlendMode(blendMode_));
    canvas->drawPath(pathCache_->Resolve(geometry, paint.fillRule), tgfxPaint);
}

void TgfxCanvasAdapter::strokePath(const ShapeGeometry &geometry, const Paint &paint,
                                   const StrokeOptions &options) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || !pathCache_) {
        return;
    }
    const tgfx::Path fullPath = pathCache_->Resolve(geometry, paint.fillRule);
    const bool hasTrim = NeedsTrim(options);
    TrimWindow trimWindow{};
    tgfx::Path strokeGeometry = fullPath;
    if (hasTrim) {
        trimWindow = NormalizeTrimWindow(options.trimStart, options.trimEnd, options.trimOffset);
        if (trimWindow.start == trimWindow.end) {
            return;
        }
        strokeGeometry = pathCache_->ResolveTrimmed(geometry, paint.fillRule, trimWindow, fullPath);
    }
    if (strokeGeometry.isEmpty()) {
        return;
    }

    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    Color color = paint.color;
    color.a *= opacity_;
    tgfxPaint.setColor(ToTgfxColor(color));
    tgfxPaint.setBlendMode(ToTgfxBlendMode(blendMode_));

    if (options.position == StrokePosition::Center) {
        tgfxPaint.setStyle(tgfx::PaintStyle::Stroke);
        tgfxPaint.setStrokeWidth(options.width);
        tgfxPaint.setLineCap(ToTgfxLineCap(options.cap));
        tgfxPaint.setLineJoin(ToTgfxLineJoin(options.join));
        canvas->drawPath(strokeGeometry, tgfxPaint);
        return;
    }

    // Inside/outside: cache the boolean outline so PathRef identity stays
    // stable and tgfx GPU shape proxies can hit across frames.
    const tgfx::Path outline =
        pathCache_->ResolvePositionedOutline(geometry, paint.fillRule, hasTrim, trimWindow, options,
                                             fullPath, strokeGeometry);
    if (outline.isEmpty()) {
        return;
    }
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    canvas->drawPath(outline, tgfxPaint);
}

void TgfxCanvasAdapter::clipPath(const ShapeGeometry &geometry, FillRule rule) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || !pathCache_) {
        return;
    }
    canvas->clipPath(pathCache_->Resolve(geometry, rule));
}

void TgfxCanvasAdapter::beginLayer() {
    if (isolationStack_ == nullptr) {
        return;
    }
    // Emplace first: moving PictureRecorder invalidates any canvas* from
    // beginRecording().
    isolationStack_->layers.emplace_back();
    IsolationLayer &layer = isolationStack_->layers.back();
    layer.contentCanvas = layer.contentRecorder.beginRecording();
}

void TgfxCanvasAdapter::endLayer() {
    if (isolationStack_ == nullptr || isolationStack_->layers.empty() || surface_ == nullptr) {
        return;
    }
    // PictureRecorder is not safely movable; finish and pop in place.
    IsolationLayer &layer = isolationStack_->layers.back();
    layer.contentCanvas = nullptr;
    std::shared_ptr<tgfx::Picture> content = layer.contentRecorder.finishRecordingAsPicture();

    tgfx::Paint paint;
    paint.setAntiAlias(true);
    if (!layer.coverages.empty()) {
        tgfx::Context *context = surface_->getContext();
        CoverageImage combined = layer.coverages.front();
        for (size_t i = 1; i < layer.coverages.size(); ++i) {
            CoverageImage intersected =
                IntersectCoverageImages(context, combined, layer.coverages[i]);
            if (intersected.image == nullptr) {
                continue;
            }
            combined = std::move(intersected);
        }
        if (combined.image != nullptr) {
            auto shader = tgfx::Shader::MakeImageShader(combined.image, tgfx::TileMode::Decal,
                                                        tgfx::TileMode::Decal);
            if (shader != nullptr) {
                shader = shader->makeWithMatrix(combined.localMatrix);
                paint.setMaskFilter(tgfx::MaskFilter::MakeShader(shader, combined.invertAlpha));
            }
        }
    }

    isolationStack_->layers.pop_back();
    tgfx::Canvas *parent = drawingCanvas();
    if (parent == nullptr || content == nullptr) {
        return;
    }
    parent->drawPicture(content, nullptr, &paint);
}

void TgfxCanvasAdapter::beginMask(MaskApplyMode mode) {
    if (isolationStack_ == nullptr || isolationStack_->layers.empty()) {
        return;
    }
    IsolationLayer &layer = isolationStack_->layers.back();
    layer.maskApplyMode = mode;
    layer.masking = true;
    layer.savedOpacity = opacity_;
    opacity_ = 1.0f;
    // maskRecorder is already owned by the stack entry; begin after settle.
    layer.maskCanvas = layer.maskRecorder.beginRecording();
}

void TgfxCanvasAdapter::endMask() {
    if (isolationStack_ == nullptr || isolationStack_->layers.empty()) {
        return;
    }
    IsolationLayer &layer = isolationStack_->layers.back();
    if (!layer.masking) {
        return;
    }
    layer.masking = false;
    layer.maskCanvas = nullptr;
    opacity_ = layer.savedOpacity;
    std::shared_ptr<tgfx::Picture> maskPicture = layer.maskRecorder.finishRecordingAsPicture();
    if (maskPicture == nullptr || surface_ == nullptr) {
        return;
    }

    tgfx::Rect bounds = maskPicture->getBounds();
    if (bounds.isEmpty()) {
        return;
    }
    bounds.roundOut();
    const int width = std::max(static_cast<int>(std::ceil(bounds.width())), 1);
    const int height = std::max(static_cast<int>(std::ceil(bounds.height())), 1);
    tgfx::Matrix pictureMatrix = tgfx::Matrix::MakeTrans(-bounds.left, -bounds.top);
    std::shared_ptr<tgfx::Image> image =
        tgfx::Image::MakeFrom(maskPicture, width, height, &pictureMatrix);
    if (image == nullptr) {
        return;
    }

    bool invertAlpha = false;
    switch (layer.maskApplyMode) {
        case MaskApplyMode::PathCoverage:
        case MaskApplyMode::AlphaMatte: {
            break;
        }
        case MaskApplyMode::AlphaMatteInverted: {
            invertAlpha = true;
            break;
        }
        case MaskApplyMode::LumaMatte:
        case MaskApplyMode::LumaMatteInverted: {
            image = image->makeWithFilter(tgfx::ImageFilter::ColorFilter(tgfx::ColorFilter::Luma()));
            invertAlpha = layer.maskApplyMode == MaskApplyMode::LumaMatteInverted;
            break;
        }
    }
    if (image == nullptr) {
        return;
    }

    CoverageImage coverage;
    coverage.image = std::move(image);
    coverage.localMatrix = tgfx::Matrix::MakeTrans(bounds.left, bounds.top);
    coverage.invertAlpha = invertAlpha;
    layer.coverages.push_back(std::move(coverage));
}

void TgfxCanvasAdapter::drawMaskPath(const ShapeGeometry &geometry, MaskMode mode, float opacity,
                                     bool inverted, float feather, float expansion) {
    if (!pathCache_ || isolationStack_ == nullptr || isolationStack_->layers.empty()) {
        return;
    }
    IsolationLayer &layer = isolationStack_->layers.back();
    if (!layer.masking || layer.maskCanvas == nullptr) {
        return;
    }
    tgfx::Canvas *canvas = layer.maskCanvas;
    tgfx::Path path = pathCache_->Resolve(geometry, FillRule::NonZero);
    path = pathCache_->ResolveMaskExpanded(geometry, FillRule::NonZero, expansion, path);
    if (inverted) {
        path.toggleInverseFillType();
    }

    if (feather > 0.0f) {
        tgfx::Paint layerPaint;
        layerPaint.setImageFilter(tgfx::ImageFilter::Blur(feather, feather));
        canvas->saveLayer(&layerPaint);
        DrawMaskPathContribution(canvas, path, opacity, mode);
        canvas->restore();
        return;
    }
    DrawMaskPathContribution(canvas, path, opacity, mode);
}

void TgfxCanvasAdapter::drawImage(const std::string &path, Vec2 containerSize, Vec2 intrinsicSize,
                                  ImageScaleMode mode) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || imageCache_ == nullptr) {
        return;
    }
    if (containerSize.x <= 0.0f || containerSize.y <= 0.0f) {
        return;
    }
    std::shared_ptr<tgfx::Image> image = imageCache_->GetOrLoad(path);
    if (image == nullptr) {
        return;
    }
    Vec2 resolvedIntrinsic = intrinsicSize;
    if (resolvedIntrinsic.x <= 0.0f || resolvedIntrinsic.y <= 0.0f) {
        resolvedIntrinsic = {static_cast<float>(image->width()),
                             static_cast<float>(image->height())};
    }
    const ImageRect destination =
        ComputeImageDestinationRect(containerSize, resolvedIntrinsic, mode);
    if (destination.isEmpty()) {
        return;
    }

    tgfx::Paint paint;
    paint.setAntiAlias(true);
    paint.setAlpha(opacity_);
    paint.setBlendMode(ToTgfxBlendMode(blendMode_));

    canvas->save();
    canvas->clipRect(tgfx::Rect::MakeWH(containerSize.x, containerSize.y));
    const tgfx::Rect src = tgfx::Rect::MakeWH(static_cast<float>(image->width()),
                                              static_cast<float>(image->height()));
    const tgfx::Rect dst =
        tgfx::Rect::MakeXYWH(destination.x, destination.y, destination.width, destination.height);
    canvas->drawImageRect(image, src, dst, tgfx::SamplingOptions(), &paint);
    canvas->restore();
}

}  // namespace motion
