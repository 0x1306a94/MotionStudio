#include "TgfxPathCache.h"

#include <cmath>

#include <tgfx/core/PathEffect.h>
#include <tgfx/core/Stroke.h>

#include "TgfxPathBuilder.h"
#include "TgfxTypeConvert.h"

namespace motion {

namespace {

tgfx::Path ApplyTrimWindow(const tgfx::Path &path, const TrimWindow &window) {
    tgfx::Path result = path;
    if (window.start == window.end) {
        result.reset();
        return result;
    }
    auto effect = tgfx::PathEffect::MakeTrim(window.start, window.end);
    if (effect != nullptr) {
        effect->filterPath(&result);
    }
    return result;
}

tgfx::Path BuildPositionedStrokeOutline(const tgfx::Path &strokeGeometry,
                                        const tgfx::Path &fullPath,
                                        const StrokeOptions &options) {
    tgfx::Stroke stroke(options.width * 2, ToTgfxLineCap(options.cap), ToTgfxLineJoin(options.join));
    tgfx::Path outline = strokeGeometry;
    if (!stroke.applyToPath(&outline)) {
        outline.reset();
        return outline;
    }
    outline.addPath(fullPath, options.position == StrokePosition::Inside ? tgfx::PathOp::Intersect : tgfx::PathOp::Difference);
    return outline;
}

}  // namespace

bool NeedsTrim(const StrokeOptions &options) {
    return options.trimEnd - options.trimStart < 1.0f;
}

TrimWindow NormalizeTrimWindow(float trimStart, float trimEnd, float trimOffset) {
    const float shift = trimOffset / 360.0f;
    float start = trimStart + shift;
    float end = trimEnd + shift;
    const float base = std::floor(start);
    start -= base;
    end -= base;
    if (end < start) {
        end += 1.0f;
    }
    return TrimWindow{start, end};
}

size_t TgfxPathCache::PathCacheKeyHash::operator()(const PathCacheKey &key) const {
    uint64_t hash = static_cast<uint64_t>(key.kind);
    hash = MixHash(hash, static_cast<uint64_t>(key.fillRule));
    hash = MixHash(hash, key.contentHash);
    return static_cast<size_t>(hash);
}

bool TgfxPathCache::DerivedPathCacheKey::operator==(const DerivedPathCacheKey &other) const {
    return kind == other.kind && fillRule == other.fillRule && contentHash == other.contentHash &&
        derivedKind == other.derivedKind && hasTrim == other.hasTrim &&
        FloatBits(trimStart) == FloatBits(other.trimStart) &&
        FloatBits(trimEnd) == FloatBits(other.trimEnd) && position == other.position &&
        FloatBits(width) == FloatBits(other.width) && cap == other.cap && join == other.join;
}

size_t TgfxPathCache::DerivedPathCacheKeyHash::operator()(const DerivedPathCacheKey &key) const {
    uint64_t hash = static_cast<uint64_t>(key.kind);
    hash = MixHash(hash, static_cast<uint64_t>(key.fillRule));
    hash = MixHash(hash, key.contentHash);
    hash = MixHash(hash, static_cast<uint64_t>(key.derivedKind));
    hash = MixHash(hash, key.hasTrim ? 1ULL : 0ULL);
    hash = MixHash(hash, FloatBits(key.trimStart));
    hash = MixHash(hash, FloatBits(key.trimEnd));
    hash = MixHash(hash, static_cast<uint64_t>(key.position));
    hash = MixHash(hash, FloatBits(key.width));
    hash = MixHash(hash, static_cast<uint64_t>(key.cap));
    hash = MixHash(hash, static_cast<uint64_t>(key.join));
    return static_cast<size_t>(hash);
}

tgfx::Path TgfxPathCache::Resolve(const ShapeGeometry &geometry, FillRule fillRule) {
    PathCacheKey key;
    key.kind = geometry.kind;
    key.fillRule = fillRule;
    key.contentHash = HashGeometry(geometry, fillRule);
    const auto found = index_.find(key);
    if (found != index_.end()) {
        order_.splice(order_.begin(), order_, found->second);
        return found->second->second;
    }
    if (order_.size() >= Capacity) {
        index_.erase(order_.back().first);
        order_.pop_back();
    }
    order_.push_front({key, BuildTgfxPath(geometry, fillRule)});
    index_.emplace(key, order_.begin());
    return order_.front().second;
}

tgfx::Path TgfxPathCache::ResolveTrimmed(const ShapeGeometry &geometry, FillRule fillRule,
                                         const TrimWindow &window, const tgfx::Path &fullPath) {
    DerivedPathCacheKey key;
    key.kind = geometry.kind;
    key.fillRule = fillRule;
    key.contentHash = HashGeometry(geometry, fillRule);
    key.derivedKind = DerivedPathKind::Trimmed;
    key.hasTrim = true;
    key.trimStart = window.start;
    key.trimEnd = window.end;
    const auto found = derivedIndex_.find(key);
    if (found != derivedIndex_.end()) {
        derivedOrder_.splice(derivedOrder_.begin(), derivedOrder_, found->second);
        return found->second->second;
    }
    if (derivedOrder_.size() >= DerivedCapacity) {
        derivedIndex_.erase(derivedOrder_.back().first);
        derivedOrder_.pop_back();
    }
    derivedOrder_.push_front({key, ApplyTrimWindow(fullPath, window)});
    derivedIndex_.emplace(key, derivedOrder_.begin());
    return derivedOrder_.front().second;
}

tgfx::Path TgfxPathCache::ResolvePositionedOutline(const ShapeGeometry &geometry, FillRule fillRule,
                                                   bool hasTrim, const TrimWindow &window,
                                                   const StrokeOptions &options,
                                                   const tgfx::Path &fullPath,
                                                   const tgfx::Path &strokeGeometry) {
    DerivedPathCacheKey key;
    key.kind = geometry.kind;
    key.fillRule = fillRule;
    key.contentHash = HashGeometry(geometry, fillRule);
    key.derivedKind = DerivedPathKind::PositionedOutline;
    key.hasTrim = hasTrim;
    key.trimStart = hasTrim ? window.start : 0.0f;
    key.trimEnd = hasTrim ? window.end : 1.0f;
    key.position = options.position;
    key.width = options.width;
    key.cap = options.cap;
    key.join = options.join;
    const auto found = derivedIndex_.find(key);
    if (found != derivedIndex_.end()) {
        derivedOrder_.splice(derivedOrder_.begin(), derivedOrder_, found->second);
        return found->second->second;
    }
    if (derivedOrder_.size() >= DerivedCapacity) {
        derivedIndex_.erase(derivedOrder_.back().first);
        derivedOrder_.pop_back();
    }
    derivedOrder_.push_front(
        {key, BuildPositionedStrokeOutline(strokeGeometry, fullPath, options)});
    derivedIndex_.emplace(key, derivedOrder_.begin());
    return derivedOrder_.front().second;
}

}  // namespace motion
