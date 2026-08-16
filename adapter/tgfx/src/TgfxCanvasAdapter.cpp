#include "TgfxCanvasAdapter.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/ColorFilter.h>
#include <tgfx/core/Font.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/ImageFilter.h>
#include <tgfx/core/MaskFilter.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/Shader.h>
#include <tgfx/core/Stroke.h>
#include <tgfx/core/Surface.h>
#include <tgfx/core/TextBlob.h>
#include <tgfx/core/Typeface.h>
#include <tgfx/gpu/Context.h>

#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/render/ImageScaleLayout.h"
#include "MotionStudio/render/Paint.h"
#include "MotionStudio/textlayout/TextLayout.h"
#include "RenderCache.h"
#include "TextPathLayout.h"
#include "TgfxGlyphMetrics.h"
#include "TgfxImageCache.h"
#include "TgfxIsolation.h"
#include "TgfxPathBuilder.h"
#include "TgfxPathCache.h"
#include "TgfxProfileTiming.h"
#include "TgfxTextTypeface.h"
#include "TgfxTypeConvert.h"
#include "effects/BrightnessContrastFilter.h"
#include "effects/ColorSourceEffect.h"
#include "effects/Uniform.h"

#include "MotionStudio/model/LayerEffect.h"

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

void NoteIsolationContentBounds(IsolationLayer &layer, tgfx::Canvas *canvas,
                                const tgfx::Rect &localBounds) {
    if (layer.masking || canvas == nullptr || canvas != layer.contentCanvas ||
        localBounds.isEmpty()) {
        return;
    }
    const tgfx::Rect mapped = canvas->getMatrix().mapRect(localBounds);
    if (mapped.isEmpty()) {
        return;
    }
    if (layer.contentBounds.isEmpty()) {
        layer.contentBounds = mapped;
    } else {
        layer.contentBounds.join(mapped);
    }
}

std::shared_ptr<tgfx::Image> PictureToImage(tgfx::Context *context,
                                            const std::shared_ptr<tgfx::Picture> &picture,
                                            const tgfx::Rect &contentBounds,
                                            const tgfx::Paint *paint) {
    if (context == nullptr || picture == nullptr || contentBounds.isEmpty()) {
        return nullptr;
    }
    tgfx::Rect bounds = contentBounds;
    bounds.roundOut();
    const int width = std::max(static_cast<int>(std::ceil(bounds.width())), 1);
    const int height = std::max(static_cast<int>(std::ceil(bounds.height())), 1);
    std::shared_ptr<tgfx::Surface> surface = tgfx::Surface::Make(context, width, height, true);
    if (surface == nullptr) {
        surface = tgfx::Surface::Make(context, width, height);
    }
    if (surface == nullptr) {
        return nullptr;
    }
    tgfx::Canvas *canvas = surface->getCanvas();
    canvas->clear();
    canvas->setMatrix(tgfx::Matrix::MakeTrans(-bounds.left, -bounds.top));
    canvas->drawPicture(picture, nullptr, paint);
    return surface->makeImageSnapshot();
}

std::shared_ptr<tgfx::Image> ApplyLayerEffect(const LayerEffect &effect,
                                              std::shared_ptr<tgfx::Image> input,
                                              RenderCache *cache, tgfx::Point *offset) {
    switch (effect.type()) {
        case LayerEffectType::BrightnessContrast: {
            const auto &brightnessContrast = static_cast<const BrightnessContrastEffect &>(effect);
            return BrightnessContrastFilter::Apply(input, cache,
                                                   brightnessContrast.brightness.evaluate(0),
                                                   brightnessContrast.contrast.evaluate(0), offset);
        }
        case LayerEffectType::GaussianBlur: {
            return input;
        }
    }
    return input;
}

IsolationLayer *TopIsolationLayer(const std::unique_ptr<TgfxIsolationStack> &stack) {
    if (stack == nullptr || stack->layers.empty()) {
        return nullptr;
    }
    return &stack->layers.back();
}

uint64_t HashTextPathDrawParams(const TextDrawParams &params) {
    uint64_t hash = 0;
    hash = MixHash(hash, params.text.size());
    for (unsigned char byte : params.text) {
        hash = MixHash(hash, byte);
    }
    hash = MixHash(hash, params.fontFamily.size());
    for (unsigned char byte : params.fontFamily) {
        hash = MixHash(hash, byte);
    }
    hash = MixHash(hash, params.fontStyle.size());
    for (unsigned char byte : params.fontStyle) {
        hash = MixHash(hash, byte);
    }
    hash = MixHash(hash, FloatBits(params.fontSize));
    hash = MixHash(hash, static_cast<uint64_t>(params.align));
    hash = MixHash(hash, params.textPathReversed ? 1ULL : 0ULL);
    hash = MixHash(hash, params.textPathPerpendicular ? 1ULL : 0ULL);
    hash = MixHash(hash, params.textPathForceAlignment ? 1ULL : 0ULL);
    hash = MixHash(hash, FloatBits(params.textPathFirstMargin));
    hash = MixHash(hash, FloatBits(params.textPathLastMargin));
    hash = MixHash(hash, params.textPath.contours.size());
    for (const BezierPath::Contour &contour : params.textPath.contours) {
        hash = MixHash(hash, contour.closed ? 1ULL : 0ULL);
        hash = MixHash(hash, contour.vertices.size());
        for (const BezierPath::Vertex &vertex : contour.vertices) {
            hash = MixHash(hash, FloatBits(vertex.point.x));
            hash = MixHash(hash, FloatBits(vertex.point.y));
            hash = MixHash(hash, FloatBits(vertex.inTangent.x));
            hash = MixHash(hash, FloatBits(vertex.inTangent.y));
            hash = MixHash(hash, FloatBits(vertex.outTangent.x));
            hash = MixHash(hash, FloatBits(vertex.outTangent.y));
        }
    }
    return hash;
}

void DrawTextOnPath(tgfx::Canvas *canvas, const TextDrawParams &params, float opacity,
                    const TextPathLayoutResult &layout,
                    const std::shared_ptr<tgfx::Typeface> &typeface) {
    const tgfx::Font font(typeface, params.fontSize > 0.0f ? params.fontSize : 1.0f);
    for (const TextDrawStyle &style : params.styles) {
        for (const TextPathGlyph &glyph : layout.glyphs) {
            if (glyph.utf8.empty()) {
                continue;
            }
            std::shared_ptr<tgfx::TextBlob> blob = tgfx::TextBlob::MakeFrom(glyph.utf8, font);
            if (blob == nullptr) {
                continue;
            }
            Color color = style.color;
            color.a *= opacity;
            tgfx::Paint paint;
            paint.setAntiAlias(true);
            paint.setColor(ToTgfxColor(color));
            paint.setBlendMode(ToTgfxBlendMode(style.blendMode));
            if (style.isStroke) {
                if (style.strokeWidth <= 0.0f) {
                    continue;
                }
                paint.setStyle(tgfx::PaintStyle::Stroke);
                paint.setStrokeWidth(style.strokeWidth);
            } else {
                paint.setStyle(tgfx::PaintStyle::Fill);
            }
            canvas->save();
            canvas->concat(ToTgfxMatrix(glyph.matrix));
            canvas->drawTextBlob(blob, 0.0f, 0.0f, paint);
            canvas->restore();
        }
    }
}

std::string BuildColorSourceSourceKey(const ShaderPaint &shader) {
    std::ostringstream out;
    out << shader.mainImage << '\n';
    for (const ShaderUniformDecl &decl : shader.uniforms) {
        out << decl.name << '\0' << static_cast<int>(decl.format) << '\0' << decl.count << '\n';
    }
    return out.str();
}

void WriteShaderUniformValues(UniformData *uniformData, const ShaderPaint &shader) {
    if (uniformData == nullptr) {
        return;
    }
    for (const EvaluatedShaderUniform &value : shader.values) {
        switch (value.kind) {
            case ShaderUniformValueKind::AnimFloat:
                uniformData->setData(value.name, value.floatValue);
                break;
            case ShaderUniformValueKind::AnimFloat2: {
                const float xy[2] = {value.float2Value.x, value.float2Value.y};
                uniformData->setData(value.name, xy, sizeof(xy));
                break;
            }
            case ShaderUniformValueKind::AnimFloat3: {
                const float xyz[3] = {value.float3Value.x, value.float3Value.y, value.float3Value.z};
                uniformData->setData(value.name, xyz, sizeof(xyz));
                break;
            }
            case ShaderUniformValueKind::AnimFloat4: {
                const float v[4] = {value.float4Value.x, value.float4Value.y, value.float4Value.z,
                                    value.float4Value.w};
                uniformData->setData(value.name, v, sizeof(v));
                break;
            }
            case ShaderUniformValueKind::AnimColor:
                uniformData->setData(value.name, ToTgfxColor(value.colorValue));
                break;
            default:
                break;
        }
    }
}

std::shared_ptr<tgfx::Shader> MakeGradientTgfxShader(const EvaluatedGradient &gradient) {
    if (gradient.stops.size() < 2u) {
        return {};
    }
    std::vector<tgfx::Color> colors;
    std::vector<float> positions;
    colors.reserve(gradient.stops.size());
    positions.reserve(gradient.stops.size());
    for (const EvaluatedGradientStop &stop : gradient.stops) {
        colors.push_back(ToTgfxColor(stop.color));
        positions.push_back(stop.position);
    }
    const tgfx::Point start = tgfx::Point::Make(gradient.start.x, gradient.start.y);
    const tgfx::Point end = tgfx::Point::Make(gradient.end.x, gradient.end.y);
    switch (gradient.type) {
        case GradientType::Linear:
            return tgfx::Shader::MakeLinearGradient(start, end, colors, positions);
        case GradientType::Radial: {
            const float radius = tgfx::Point::Distance(start, end);
            if (radius <= 0.f) {
                return {};
            }
            return tgfx::Shader::MakeRadialGradient(start, radius, colors, positions);
        }
        case GradientType::Conic:
            return tgfx::Shader::MakeConicGradient(start, gradient.startAngle, gradient.endAngle,
                                                   colors, positions);
        case GradientType::Diamond: {
            const float radius = tgfx::Point::Distance(start, end);
            if (radius <= 0.f) {
                return {};
            }
            return tgfx::Shader::MakeDiamondGradient(start, radius, colors, positions);
        }
    }
    return {};
}

// Builds a shader for Shader/Gradient paints. Returns:
// - nullopt: Color mode (caller uses solid color)
// - empty shared_ptr: Shader/Gradient unavailable (caller skips draw)
// - non-null: ready shader
std::optional<std::shared_ptr<tgfx::Shader>> MakePaintImageShader(
    const Paint &paint, const tgfx::Rect &sourceBounds, RenderCache *renderCache,
    const ColorSourceFrameContext &frameContext) {
    if (paint.paintMode == StylePaintMode::Color) {
        return std::nullopt;
    }
    if (paint.paintMode == StylePaintMode::Gradient) {
        std::shared_ptr<tgfx::Shader> shader = MakeGradientTgfxShader(paint.gradient);
        return shader;
    }
    if (paint.paintMode != StylePaintMode::Shader) {
        return std::shared_ptr<tgfx::Shader>{};
    }
    if (renderCache == nullptr || !paint.shader.shaderId.isValid() || paint.shader.mainImage.empty()) {
        return std::shared_ptr<tgfx::Shader>{};
    }
    renderCache->invalidateColorSourcePipelineIfSourceChanged(paint.shader.shaderId,
                                                              BuildColorSourceSourceKey(paint.shader));
    std::vector<Uniform> decls;
    decls.reserve(paint.shader.uniforms.size());
    for (const ShaderUniformDecl &decl : paint.shader.uniforms) {
        decls.emplace_back(decl.name, decl.format, decl.count);
    }
    auto effect = ColorSourceEffect::Make(paint.shader.shaderId, paint.shader.mainImage,
                                          std::move(decls), sourceBounds, renderCache);
    if (effect == nullptr) {
        return std::shared_ptr<tgfx::Shader>{};
    }
    effect->setFrameContext(frameContext);
    WriteShaderUniformValues(effect->getUniformData(), paint.shader);
    if (!effect->preparePipeline()) {
        return std::shared_ptr<tgfx::Shader>{};
    }
    return effect->makeImageShader();
}

}  // namespace

TgfxCanvasAdapter::TgfxCanvasAdapter()
    : pathCache_(std::make_unique<TgfxPathCache>())
    , isolationStack_(std::make_unique<TgfxIsolationStack>())
    , imageCache_(std::make_unique<TgfxImageCache>())
    , renderCache_(std::make_unique<RenderCache>()) {
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
    if (renderCache_ != nullptr) {
        renderCache_->releaseAll();
        renderCache_->detachFromContext();
    }
    surface_.reset();
    if (context != nullptr) {
        context->purgeResourcesUntilMemoryTo(0);
    }
}

void TgfxCanvasAdapter::drawPreviewBackdrop() {
}

void TgfxCanvasAdapter::onFrameReady(int sceneWidth, int sceneHeight, Color backgroundColor, float cornerRadius) {
    if (!surface_ || sceneWidth <= 0 || sceneHeight <= 0) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    const tgfx::Rect compositionBounds = tgfx::Rect::MakeWH(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight));
    const float radius = std::clamp(cornerRadius, 0.0f, std::min(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight)) * 0.5f);
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

void TgfxCanvasAdapter::beginFrame(int width, int height, Color backgroundColor, float cornerRadius) {
    // Release before acquireTarget: size changes may destroy the old surface/canvas.
    frameRestore_.reset();
    if (!acquireTarget(width, height) || !surface_) {
        return;
    }
    if (renderCache_ != nullptr) {
        renderCache_->attachToContext(surface_->getContext());
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

void TgfxCanvasAdapter::setColorSourceFrameContext(float timeSeconds, int64_t frameIndex,
                                                   float frameRate) {
    colorSourceTimeSeconds_ = timeSeconds;
    colorSourceFrameIndex_ = frameIndex;
    colorSourceFrameRate_ = frameRate;
}

void TgfxCanvasAdapter::drawPath(const ShapeGeometry &geometry, const Paint &paint) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || !pathCache_ || geometry.isZero()) {
        return;
    }
    // Fill uses faces only; ignore strokePath for cache identity.
    ShapeGeometry fillGeometry = geometry;
    fillGeometry.strokePath = {};
    if (fillGeometry.kind == motion::ShapeGeometryKind::Path && fillGeometry.path.contours.empty()) {
        return;
    }
    const tgfx::Path path = pathCache_->Resolve(fillGeometry, paint.fillRule);
    if (path.isEmpty()) {
        return;
    }
    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    tgfxPaint.setBlendMode(ToTgfxBlendMode(blendMode_));
    const ColorSourceFrameContext frameContext{colorSourceTimeSeconds_, colorSourceFrameIndex_,
                                               colorSourceFrameRate_};
    const std::optional<std::shared_ptr<tgfx::Shader>> shader =
        MakePaintImageShader(paint, path.getBounds(), renderCache_.get(), frameContext);
    if (shader.has_value()) {
        if (*shader == nullptr) {
            return;
        }
        tgfxPaint.setShader(*shader);
        // tgfx setAlpha replaces brush color alpha (shader path has no solid color).
        tgfxPaint.setAlpha(paint.alpha * opacity_);
    } else {
        // setColor writes RGB+A; setAlpha after it applies paint/layer multipliers.
        tgfxPaint.setColor(ToTgfxColor(paint.color));
        tgfxPaint.setAlpha(paint.color.a * paint.alpha * opacity_);
    }
    canvas->drawPath(path, tgfxPaint);
    if (IsolationLayer *layer = TopIsolationLayer(isolationStack_)) {
        NoteIsolationContentBounds(*layer, canvas, path.getBounds());
    }
}

void TgfxCanvasAdapter::strokePath(const ShapeGeometry &geometry, const Paint &paint,
                                   const StrokeOptions &options) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || !pathCache_ || geometry.isZero()) {
        return;
    }
    // Fill silhouette (for Inside/Outside boolean).
    ShapeGeometry fillGeometry = geometry;
    fillGeometry.strokePath = {};
    const tgfx::Path fullPath = pathCache_->Resolve(fillGeometry, paint.fillRule);

    // Edges / fallback path actually stroked.
    ShapeGeometry strokeSource = geometry;
    if (!geometry.strokePath.contours.empty()) {
        strokeSource.path = geometry.strokePath;
    }
    strokeSource.strokePath = {};
    const bool hasTrim = NeedsTrim(options);
    TrimWindow trimWindow{};
    tgfx::Path strokeGeometry = pathCache_->Resolve(strokeSource, paint.fillRule);
    if (hasTrim) {
        trimWindow = NormalizeTrimWindow(options.trimStart, options.trimEnd, options.trimOffset);
        if (trimWindow.start == trimWindow.end) {
            return;
        }
        strokeGeometry =
            pathCache_->ResolveTrimmed(strokeSource, paint.fillRule, trimWindow, strokeGeometry);
    }
    if (strokeGeometry.isEmpty()) {
        return;
    }
    // Trim can collapse a non-zero source to a point; hairlines must still draw.
    const tgfx::Rect bounds = strokeGeometry.getBounds();
    if (bounds.width() <= 0.0f && bounds.height() <= 0.0f) {
        return;
    }

    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setBlendMode(ToTgfxBlendMode(blendMode_));
    const ColorSourceFrameContext frameContext{colorSourceTimeSeconds_, colorSourceFrameIndex_,
                                               colorSourceFrameRate_};
    const tgfx::Rect shaderBounds =
        options.position == StrokePosition::Center ? bounds : fullPath.getBounds();
    const std::optional<std::shared_ptr<tgfx::Shader>> shader =
        MakePaintImageShader(paint, shaderBounds, renderCache_.get(), frameContext);
    if (shader.has_value()) {
        if (*shader == nullptr) {
            return;
        }
        tgfxPaint.setShader(*shader);
        tgfxPaint.setAlpha(paint.alpha * opacity_);
    } else {
        tgfxPaint.setColor(ToTgfxColor(paint.color));
        tgfxPaint.setAlpha(paint.color.a * paint.alpha * opacity_);
    }

    if (options.position == StrokePosition::Center) {
        tgfxPaint.setStyle(tgfx::PaintStyle::Stroke);
        tgfxPaint.setStrokeWidth(options.width);
        tgfxPaint.setLineCap(ToTgfxLineCap(options.cap));
        tgfxPaint.setLineJoin(ToTgfxLineJoin(options.join));
        canvas->drawPath(strokeGeometry, tgfxPaint);
        if (IsolationLayer *layer = TopIsolationLayer(isolationStack_)) {
            tgfx::Rect strokeBounds = bounds;
            strokeBounds.outset(options.width * 0.5f, options.width * 0.5f);
            NoteIsolationContentBounds(*layer, canvas, strokeBounds);
        }
        return;
    }

    // Inside/outside: cache the boolean outline so PathRef identity stays
    // stable and tgfx GPU shape proxies can hit across frames.
    // fullPath is the fill silhouette; strokeGeometry is the edge set.
    const tgfx::Path outline = pathCache_->ResolvePositionedOutline(
        strokeSource, paint.fillRule, hasTrim, trimWindow, options, fullPath, strokeGeometry);
    if (outline.isEmpty()) {
        return;
    }
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    canvas->drawPath(outline, tgfxPaint);
    if (IsolationLayer *layer = TopIsolationLayer(isolationStack_)) {
        NoteIsolationContentBounds(*layer, canvas, outline.getBounds());
    }
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
    layer.compositeOpacity = opacity_;
    layer.compositeBlend = blendMode_;
    opacity_ = 1.0f;
    blendMode_ = BlendMode::Normal;
    layer.contentCanvas = layer.contentRecorder.beginRecording();
}

void TgfxCanvasAdapter::endLayer(
    const std::vector<std::shared_ptr<const LayerEffect>> &effects) {
    if (isolationStack_ == nullptr || isolationStack_->layers.empty() || surface_ == nullptr) {
        return;
    }
    // PictureRecorder is not safely movable; finish and pop in place.
    IsolationLayer &layer = isolationStack_->layers.back();
    layer.contentCanvas = nullptr;
    std::shared_ptr<tgfx::Picture> content = layer.contentRecorder.finishRecordingAsPicture();
    const float compositeOpacity = layer.compositeOpacity;
    const BlendMode compositeBlend = layer.compositeBlend;
    const tgfx::Rect contentBounds = layer.contentBounds;
    std::vector<CoverageImage> coverages = std::move(layer.coverages);

    tgfx::Paint maskPaint;
    maskPaint.setAntiAlias(true);
    if (!coverages.empty()) {
        tgfx::Context *context = surface_->getContext();
        CoverageImage combined = coverages.front();
        for (size_t i = 1; i < coverages.size(); ++i) {
            CoverageImage intersected = IntersectCoverageImages(context, combined, coverages[i]);
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
                maskPaint.setMaskFilter(tgfx::MaskFilter::MakeShader(shader, combined.invertAlpha));
            }
        }
    }

    isolationStack_->layers.pop_back();
    tgfx::Canvas *parent = drawingCanvas();
    if (parent == nullptr || content == nullptr) {
        return;
    }

    tgfx::Paint compositePaint = maskPaint;
    compositePaint.setAlpha(compositeOpacity);
    compositePaint.setBlendMode(ToTgfxBlendMode(compositeBlend));

    const bool hasEffects = !effects.empty();
    std::shared_ptr<tgfx::Image> filtered;
    tgfx::Point offset = {};
    tgfx::Rect bounds = contentBounds;
    bounds.roundOut();
    if (hasEffects) {
        tgfx::Context *context = surface_->getContext();
        const tgfx::Paint *rasterPaint =
            maskPaint.getMaskFilter() != nullptr ? &maskPaint : nullptr;
        filtered = PictureToImage(context, content, contentBounds, rasterPaint);
        if (filtered != nullptr) {
            for (const auto &effect : effects) {
                if (effect == nullptr) {
                    continue;
                }
                std::shared_ptr<tgfx::Image> next =
                    ApplyLayerEffect(*effect, filtered, renderCache_.get(), &offset);
                if (next == nullptr) {
                    break;
                }
                filtered = std::move(next);
            }
        }
    }

    if (filtered != nullptr) {
        parent->save();
        parent->concat(tgfx::Matrix::MakeTrans(bounds.left + offset.x, bounds.top + offset.y));
        tgfx::Paint imagePaint;
        imagePaint.setAntiAlias(true);
        imagePaint.setAlpha(compositeOpacity);
        imagePaint.setBlendMode(ToTgfxBlendMode(compositeBlend));
        parent->drawImage(filtered, &imagePaint);
        parent->restore();
        return;
    }
    parent->drawPicture(content, nullptr, &compositePaint);
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
    layer.maskSurface = nullptr;
    layer.maskSurfaceBounds = tgfx::Rect::MakeEmpty();
    layer.maskCanvas = nullptr;

    // Prefer a finite surface sized to content: inverse fill needs a clip larger
    // than the path's geometric bounds (Picture+MakeFrom leaves Inv empty).
    if (!layer.contentBounds.isEmpty() && surface_ != nullptr) {
        tgfx::Rect bounds = layer.contentBounds;
        bounds.roundOut();
        const int width = std::max(static_cast<int>(std::ceil(bounds.width())), 1);
        const int height = std::max(static_cast<int>(std::ceil(bounds.height())), 1);
        tgfx::Context *context = surface_->getContext();
        std::shared_ptr<tgfx::Surface> maskSurface =
            tgfx::Surface::Make(context, width, height, true);
        if (maskSurface == nullptr) {
            maskSurface = tgfx::Surface::Make(context, width, height);
        }
        if (maskSurface != nullptr) {
            layer.maskSurface = maskSurface;
            layer.maskSurfaceBounds = bounds;
            layer.maskCanvas = maskSurface->getCanvas();
            layer.maskCanvas->clear();
            layer.maskCanvas->setMatrix(tgfx::Matrix::MakeTrans(-bounds.left, -bounds.top));
            return;
        }
    }
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
    if (surface_ == nullptr) {
        layer.maskSurface = nullptr;
        return;
    }

    std::shared_ptr<tgfx::Image> image;
    tgfx::Rect bounds = tgfx::Rect::MakeEmpty();
    if (layer.maskSurface != nullptr) {
        bounds = layer.maskSurfaceBounds;
        image = layer.maskSurface->makeImageSnapshot();
        layer.maskSurface = nullptr;
        layer.maskSurfaceBounds = tgfx::Rect::MakeEmpty();
    } else {
        std::shared_ptr<tgfx::Picture> maskPicture = layer.maskRecorder.finishRecordingAsPicture();
        if (maskPicture == nullptr) {
            return;
        }
        bounds = maskPicture->getBounds();
        if (!layer.contentBounds.isEmpty()) {
            if (bounds.isEmpty()) {
                bounds = layer.contentBounds;
            } else {
                bounds.join(layer.contentBounds);
            }
        }
        if (bounds.isEmpty()) {
            return;
        }
        bounds.roundOut();
        const int width = std::max(static_cast<int>(std::ceil(bounds.width())), 1);
        const int height = std::max(static_cast<int>(std::ceil(bounds.height())), 1);
        tgfx::Matrix pictureMatrix = tgfx::Matrix::MakeTrans(-bounds.left, -bounds.top);
        image = tgfx::Image::MakeFrom(maskPicture, width, height, &pictureMatrix);
    }
    if (image == nullptr || bounds.isEmpty()) {
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
    // Inverse fill type keeps geometric bounds; on a finite mask surface build
    // exterior explicitly (contentBounds − path) so AE Inv has coverage pixels.
    if (inverted) {
        tgfx::Rect exteriorBounds = layer.maskSurfaceBounds;
        if (exteriorBounds.isEmpty()) {
            exteriorBounds = layer.contentBounds;
        }
        if (!exteriorBounds.isEmpty()) {
            tgfx::Path exterior;
            exterior.addRect(exteriorBounds);
            exterior.addPath(path, tgfx::PathOp::Difference);
            path = std::move(exterior);
        } else {
            path.toggleInverseFillType();
        }
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
    const ImageRect destination = ComputeImageDestinationRect(containerSize, resolvedIntrinsic, mode);
    if (destination.isEmpty()) {
        return;
    }

    tgfx::Paint paint;
    paint.setAntiAlias(true);
    paint.setAlpha(opacity_);
    paint.setBlendMode(ToTgfxBlendMode(blendMode_));

    tgfx::AutoCanvasRestore restore(canvas);
    canvas->clipRect(tgfx::Rect::MakeWH(containerSize.x, containerSize.y));
    const tgfx::Rect src = tgfx::Rect::MakeWH(static_cast<float>(image->width()),
                                              static_cast<float>(image->height()));
    const tgfx::Rect dst = tgfx::Rect::MakeXYWH(destination.x, destination.y, destination.width, destination.height);
    canvas->drawImageRect(image, src, dst, tgfx::SamplingOptions(), &paint);
    if (IsolationLayer *layer = TopIsolationLayer(isolationStack_)) {
        NoteIsolationContentBounds(*layer, canvas,
                                   tgfx::Rect::MakeWH(containerSize.x, containerSize.y));
    }
}

void TgfxCanvasAdapter::drawText(const TextDrawParams &params) {
    tgfx::Canvas *canvas = drawingCanvas();
    if (canvas == nullptr || params.styles.empty()) {
        return;
    }
    std::shared_ptr<tgfx::Typeface> typeface =
        ResolveTextTypeface(params.fontFamily, params.fontStyle);
    if (typeface == nullptr) {
        return;
    }

    if (params.textPathEnabled && !params.textPath.contours.empty()) {
        const uint64_t key = HashTextPathDrawParams(params);
        if (textPathCacheValid_ && key == textPathCacheKey_) {
            ++textPathCacheHits_;
        } else {
            TextPathLayoutInput layoutInput;
            layoutInput.text = params.text;
            layoutInput.fontSize = params.fontSize;
            layoutInput.align = params.align;
            layoutInput.fontFamily = params.fontFamily;
            layoutInput.fontStyle = params.fontStyle;
            layoutInput.path = params.textPath;
            layoutInput.reversed = params.textPathReversed;
            layoutInput.perpendicular = params.textPathPerpendicular;
            layoutInput.forceAlignment = params.textPathForceAlignment;
            layoutInput.firstMargin = params.textPathFirstMargin;
            layoutInput.lastMargin = params.textPathLastMargin;
            textPathCacheResult_ = LayoutTextOnPath(layoutInput);
            textPathCacheKey_ = key;
            textPathCacheValid_ = true;
        }
        DrawTextOnPath(canvas, params, opacity_, textPathCacheResult_, typeface);
        if (IsolationLayer *layer = TopIsolationLayer(isolationStack_)) {
            const Vec2 &min = textPathCacheResult_.boundsMin;
            const Vec2 &max = textPathCacheResult_.boundsMax;
            NoteIsolationContentBounds(
                *layer, canvas, tgfx::Rect::MakeLTRB(min.x, min.y, max.x, max.y));
        }
        return;
    }

    const bool boxTextMode = params.boxTextMode;
    if (boxTextMode && (params.containerSize.x <= 0.0f || params.containerSize.y <= 0.0f)) {
        return;
    }

    TgfxGlyphMetrics glyphMetrics(typeface);
    textlayout::TextLayoutInput input;
    input.text = params.text;
    input.softWrap = boxTextMode;
    input.shrinkToFit = boxTextMode;
    if (boxTextMode) {
        input.boxWidth = params.containerSize.x;
        input.boxHeight = params.containerSize.y;
    } else {
        // Unused when softWrap is false; keep positive for layout guards.
        input.boxWidth = 1.0f;
        input.boxHeight = 1.0f;
    }
    input.fontSize = params.fontSize > 0.0f ? params.fontSize : 1.0f;
    switch (params.align) {
        case TextAlign::Left: {
            input.align = textlayout::Align::Left;
            break;
        }
        case TextAlign::Center: {
            input.align = textlayout::Align::Center;
            break;
        }
        case TextAlign::Right: {
            input.align = textlayout::Align::Right;
            break;
        }
    }
    input.metrics = &glyphMetrics;
    const textlayout::TextLayoutResult layout = textlayout::LayoutText(input);

    const tgfx::Font font(typeface, layout.appliedFontSize);
    for (const TextDrawStyle &style : params.styles) {
        for (const textlayout::TextLine &line : layout.lines) {
            if (line.text.empty()) {
                continue;
            }
            std::shared_ptr<tgfx::TextBlob> blob = tgfx::TextBlob::MakeFrom(line.text, font);
            if (blob == nullptr) {
                continue;
            }
            Color color = style.color;
            color.a *= opacity_;
            tgfx::Paint paint;
            paint.setAntiAlias(true);
            paint.setColor(ToTgfxColor(color));
            paint.setBlendMode(ToTgfxBlendMode(style.blendMode));
            if (style.isStroke) {
                if (style.strokeWidth <= 0.0f) {
                    continue;
                }
                paint.setStyle(tgfx::PaintStyle::Stroke);
                paint.setStrokeWidth(style.strokeWidth);
            } else {
                paint.setStyle(tgfx::PaintStyle::Fill);
            }
            canvas->drawTextBlob(blob, line.x, line.y, paint);
        }
    }
    if (IsolationLayer *layer = TopIsolationLayer(isolationStack_)) {
        if (boxTextMode) {
            NoteIsolationContentBounds(
                *layer, canvas,
                tgfx::Rect::MakeWH(params.containerSize.x, params.containerSize.y));
        } else {
            tgfx::Rect textBounds = tgfx::Rect::MakeEmpty();
            for (const textlayout::TextLine &line : layout.lines) {
                if (line.text.empty()) {
                    continue;
                }
                const float width = line.width > 0.0f ? line.width : layout.appliedFontSize;
                const float height = layout.appliedFontSize;
                const tgfx::Rect lineBounds =
                    tgfx::Rect::MakeXYWH(line.x, line.y - height, width, height);
                if (textBounds.isEmpty()) {
                    textBounds = lineBounds;
                } else {
                    textBounds.join(lineBounds);
                }
            }
            NoteIsolationContentBounds(*layer, canvas, textBounds);
        }
    }
}

}  // namespace motion
