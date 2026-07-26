#include "TgfxCanvasAdapter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/PathEffect.h>
#include <tgfx/core/PathTypes.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/Stroke.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Device.h>

namespace motion {

namespace {

using ProfileClock = std::chrono::steady_clock;

double Milliseconds(ProfileClock::time_point start, ProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

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
        case BlendMode::Overlay: {
            return tgfx::BlendMode::Overlay;
        }
        case BlendMode::Darken: {
            return tgfx::BlendMode::Darken;
        }
        case BlendMode::Lighten: {
            return tgfx::BlendMode::Lighten;
        }
        case BlendMode::ColorDodge: {
            return tgfx::BlendMode::ColorDodge;
        }
        case BlendMode::ColorBurn: {
            return tgfx::BlendMode::ColorBurn;
        }
        case BlendMode::HardLight: {
            return tgfx::BlendMode::HardLight;
        }
        case BlendMode::SoftLight: {
            return tgfx::BlendMode::SoftLight;
        }
        case BlendMode::Difference: {
            return tgfx::BlendMode::Difference;
        }
        case BlendMode::Exclusion: {
            return tgfx::BlendMode::Exclusion;
        }
        case BlendMode::Hue: {
            return tgfx::BlendMode::Hue;
        }
        case BlendMode::Saturation: {
            return tgfx::BlendMode::Saturation;
        }
        case BlendMode::Color: {
            return tgfx::BlendMode::Color;
        }
        case BlendMode::Luminosity: {
            return tgfx::BlendMode::Luminosity;
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

// Zero relative tangents mean a straight segment; emit lineTo so tgfx can
// recognize axis-aligned rectangles via Path::isRect and take the drawRect
// fast path instead of generic cubic triangulation.
bool IsStraightSegment(const BezierPath::Vertex &from, const BezierPath::Vertex &to) {
    return from.outTangent.x == 0.0f && from.outTangent.y == 0.0f && to.inTangent.x == 0.0f &&
        to.inTangent.y == 0.0f;
}

void AppendBezierSegment(tgfx::Path &result, const BezierPath::Vertex &from,
                         const BezierPath::Vertex &to) {
    if (IsStraightSegment(from, to)) {
        result.lineTo(to.point.x, to.point.y);
        return;
    }
    result.cubicTo(from.point.x + from.outTangent.x, from.point.y + from.outTangent.y,
                   to.point.x + to.inTangent.x, to.point.y + to.inTangent.y, to.point.x,
                   to.point.y);
}

// Converts a BezierPath (relative tangents) into a tgfx path with absolute
// control points.
tgfx::Path BezierToTgfxPath(const BezierPath &path, FillRule fillRule) {
    tgfx::Path result;
    if (path.vertices.empty()) {
        result.setFillType(fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd
                                                         : tgfx::PathFillType::Winding);
        return result;
    }
    const BezierPath::Vertex &first = path.vertices.front();
    result.moveTo(first.point.x, first.point.y);
    for (size_t i = 1; i < path.vertices.size(); ++i) {
        AppendBezierSegment(result, path.vertices[i - 1], path.vertices[i]);
    }
    if (path.closed && path.vertices.size() > 1) {
        AppendBezierSegment(result, path.vertices.back(), first);
        result.close();
    }
    result.setFillType(fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd
                                                     : tgfx::PathFillType::Winding);
    return result;
}

tgfx::PathFillType ToTgfxFillType(FillRule fillRule) {
    return fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd
                                         : tgfx::PathFillType::Winding;
}

tgfx::Rect CenteredBounds(Vec2 center, Vec2 size) {
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    return tgfx::Rect::MakeXYWH(center.x - halfWidth, center.y - halfHeight, halfWidth * 2.0f,
                                halfHeight * 2.0f);
}

tgfx::Path BuildTgfxPath(const ShapeGeometry &geometry, FillRule fillRule) {
    tgfx::Path result;
    switch (geometry.kind) {
        case ShapeGeometryKind::Path: {
            return BezierToTgfxPath(geometry.path, fillRule);
        }
        case ShapeGeometryKind::Rect: {
            const tgfx::Rect bounds = CenteredBounds(geometry.center, geometry.size);
            const float maxRadius = std::min(bounds.width(), bounds.height()) * 0.5f;
            const float radius = std::clamp(geometry.cornerRadius, 0.0f, maxRadius);
            if (radius > 0.0f) {
                result.addRoundRect(bounds, radius, radius);
            } else {
                result.addRect(bounds);
            }
            break;
        }
        case ShapeGeometryKind::Ellipse: {
            result.addOval(CenteredBounds(geometry.center, geometry.size));
            break;
        }
    }
    result.setFillType(ToTgfxFillType(fillRule));
    return result;
}

uint64_t MixHash(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

uint64_t FloatBits(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float size mismatch");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint64_t HashGeometry(const ShapeGeometry &geometry, FillRule fillRule) {
    uint64_t hash = static_cast<uint64_t>(geometry.kind);
    hash = MixHash(hash, static_cast<uint64_t>(fillRule));
    switch (geometry.kind) {
        case ShapeGeometryKind::Path: {
            hash = MixHash(hash, geometry.path.closed ? 1ULL : 0ULL);
            hash = MixHash(hash, geometry.path.vertices.size());
            for (const BezierPath::Vertex &vertex : geometry.path.vertices) {
                hash = MixHash(hash, FloatBits(vertex.point.x));
                hash = MixHash(hash, FloatBits(vertex.point.y));
                hash = MixHash(hash, FloatBits(vertex.inTangent.x));
                hash = MixHash(hash, FloatBits(vertex.inTangent.y));
                hash = MixHash(hash, FloatBits(vertex.outTangent.x));
                hash = MixHash(hash, FloatBits(vertex.outTangent.y));
            }
            break;
        }
        case ShapeGeometryKind::Rect: {
            hash = MixHash(hash, FloatBits(geometry.center.x));
            hash = MixHash(hash, FloatBits(geometry.center.y));
            hash = MixHash(hash, FloatBits(geometry.size.x));
            hash = MixHash(hash, FloatBits(geometry.size.y));
            hash = MixHash(hash, FloatBits(geometry.cornerRadius));
            break;
        }
        case ShapeGeometryKind::Ellipse: {
            hash = MixHash(hash, FloatBits(geometry.center.x));
            hash = MixHash(hash, FloatBits(geometry.center.y));
            hash = MixHash(hash, FloatBits(geometry.size.x));
            hash = MixHash(hash, FloatBits(geometry.size.y));
            break;
        }
    }
    return hash;
}

tgfx::Matrix ToTgfxMatrix(const Mat3 &matrix) {
    tgfx::Matrix result;
    result.setAll(matrix.values[0], matrix.values[1], matrix.values[2], matrix.values[3],
                  matrix.values[4], matrix.values[5]);
    return result;
}

bool NeedsTrim(const StrokeOptions &options) {
    return options.trimEnd - options.trimStart < 1.0f;
}

// Cuts the [trimStart, trimEnd] window (rotated by trimOffset degrees) out of
// the path, matching AE trim paths. MakeTrim accepts values outside [0, 1]
// and wraps them cyclically over the total length of all contours, so the
// offset is simply folded into start/end. When start > end the window wraps
// through the path start ([start, 1] + [0, end]), expressed as [start, end+1].
// An empty window (start == end) makes the effect reset the path to empty.
tgfx::Path TrimmedPath(const tgfx::Path &path, float trimStart, float trimEnd,
                       float trimOffset) {
    const float shift = trimOffset / 360.0f;
    float start = trimStart + shift;
    float end = trimEnd + shift;
    const float base = std::floor(start);
    start -= base;
    end -= base;
    if (end < start) {
        end += 1.0f;
    }
    tgfx::Path result = path;
    auto effect = tgfx::PathEffect::MakeTrim(start, end);
    if (effect != nullptr) {
        effect->filterPath(&result);
    }
    return result;
}

struct PathCacheKey {
    ShapeGeometryKind kind = ShapeGeometryKind::Path;
    FillRule fillRule = FillRule::NonZero;
    uint64_t contentHash = 0;

    bool operator==(const PathCacheKey &other) const {
        return kind == other.kind && fillRule == other.fillRule && contentHash == other.contentHash;
    }
};

struct PathCacheKeyHash {
    size_t operator()(const PathCacheKey &key) const {
        uint64_t hash = static_cast<uint64_t>(key.kind);
        hash = MixHash(hash, static_cast<uint64_t>(key.fillRule));
        hash = MixHash(hash, key.contentHash);
        return static_cast<size_t>(hash);
    }
};

}  // namespace

struct TgfxPathCache {
    // Front = most recently used. Capacity covers a few animated size variants
    // without unbounded growth across long playback sessions.
    static constexpr size_t kCapacity = 512;

    using EntryList = std::list<std::pair<PathCacheKey, tgfx::Path>>;
    EntryList order;
    std::unordered_map<PathCacheKey, EntryList::iterator, PathCacheKeyHash> index;

    tgfx::Path Resolve(const ShapeGeometry &geometry, FillRule fillRule) {
        PathCacheKey key;
        key.kind = geometry.kind;
        key.fillRule = fillRule;
        key.contentHash = HashGeometry(geometry, fillRule);
        const auto found = index.find(key);
        if (found != index.end()) {
            order.splice(order.begin(), order, found->second);
            return found->second->second;
        }
        if (order.size() >= kCapacity) {
            index.erase(order.back().first);
            order.pop_back();
        }
        order.push_front({key, BuildTgfxPath(geometry, fillRule)});
        index.emplace(key, order.begin());
        return order.front().second;
    }
};

TgfxCanvasAdapter::TgfxCanvasAdapter()
    : pathCache_(std::make_unique<TgfxPathCache>()) {
}

TgfxCanvasAdapter::~TgfxCanvasAdapter() {
    frameRestore_.reset();
}

void TgfxCanvasAdapter::drawPreviewBackdrop() {
}

void TgfxCanvasAdapter::onFrameReady(int sceneWidth, int sceneHeight, Color backgroundColor,
                                     float cornerRadius) {
    if (!surface_ || sceneWidth <= 0 || sceneHeight <= 0) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    const tgfx::Rect compositionBounds = tgfx::Rect::MakeWH(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight));
    const float radius = std::clamp(cornerRadius, 0.0f,
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
}

void TgfxCanvasAdapter::endFrame() {
    endFrameProfile_ = {};
    const auto restoreStart = ProfileClock::now();
    frameRestore_.reset();
    compositionClipSaved_ = false;
    const auto restoreEnd = ProfileClock::now();
    endFrameProfile_.canvasRestoreMs = Milliseconds(restoreStart, restoreEnd);

    const auto presentStart = ProfileClock::now();
    presentTarget();
    const auto presentEnd = ProfileClock::now();
    endFrameProfile_.presentTargetMs = Milliseconds(presentStart, presentEnd);
}

const TgfxEndFrameProfile &TgfxCanvasAdapter::endFrameProfile() const {
    return endFrameProfile_;
}

void TgfxCanvasAdapter::restoreCompositionClip() {
    if (!surface_ || !compositionClipSaved_) {
        return;
    }
    surface_->getCanvas()->restore();
    compositionClipSaved_ = false;
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

void TgfxCanvasAdapter::drawPath(const ShapeGeometry &geometry, const Paint &paint) {
    if (!surface_ || !pathCache_) {
        return;
    }
    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    Color color = paint.color;
    color.a *= opacity_;
    tgfxPaint.setColor(ToTgfxColor(color));
    tgfxPaint.setBlendMode(ToTgfxBlendMode(blendMode_));
    surface_->getCanvas()->drawPath(pathCache_->Resolve(geometry, paint.fillRule), tgfxPaint);
}

void TgfxCanvasAdapter::strokePath(const ShapeGeometry &geometry, const Paint &paint,
                                   const StrokeOptions &options) {
    if (!surface_ || !pathCache_) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    const tgfx::Path fullPath = pathCache_->Resolve(geometry, paint.fillRule);
    tgfx::Path strokeGeometry = fullPath;
    if (NeedsTrim(options)) {
        strokeGeometry = TrimmedPath(fullPath, options.trimStart, options.trimEnd,
                                     options.trimOffset);
    }
    // An empty trim window (start == end) draws nothing; tgfx asserts on
    // empty-geometry draws downstream.
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
    // Inside/outside strokes draw at double width with the unwanted half cut
    // off by a boolean op against the source path, mirroring tgfx ShapeLayer.
    tgfx::Stroke stroke(options.width * 2, ToTgfxLineCap(options.cap),
                        ToTgfxLineJoin(options.join));
    tgfx::Path outline = strokeGeometry;
    if (!stroke.applyToPath(&outline)) {
        return;
    }
    outline.addPath(fullPath, options.position == StrokePosition::Inside ? tgfx::PathOp::Intersect : tgfx::PathOp::Difference);
    if (outline.isEmpty()) {
        return;
    }
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    canvas->drawPath(outline, tgfxPaint);
}

void TgfxCanvasAdapter::clipPath(const ShapeGeometry &geometry, FillRule rule) {
    if (!surface_ || !pathCache_) {
        return;
    }
    surface_->getCanvas()->clipPath(pathCache_->Resolve(geometry, rule));
}

}  // namespace motion
