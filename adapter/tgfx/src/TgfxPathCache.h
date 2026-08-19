#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>

#include <tgfx/core/Path.h>

#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/StrokeMode.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "MotionStudio/render/StrokeOptions.h"

namespace motion {

// Fold trimOffset into [start, end] the same way AE/MakeTrim wraps windows.
// Cache keys must use these normalized ends, not the raw offset degrees.
struct TrimWindow {
    float start = 0.0f;
    float end = 1.0f;
};

bool NeedsTrim(const StrokeOptions &options);
TrimWindow NormalizeTrimWindow(float trimStart, float trimEnd, float trimOffset);

// LRU of source geometry paths plus derived trim / inside-outside outlines.
// Stabilizes PathRef identity so tgfx GPU shape proxies can hit across frames.
struct TgfxPathCache {
    static constexpr size_t Capacity = 1024;
    static constexpr size_t DerivedCapacity = 1024;

    tgfx::Path Resolve(const ShapeGeometry &geometry, FillRule fillRule);
    tgfx::Path ResolveTrimmed(const ShapeGeometry &geometry, FillRule fillRule,
                              const TrimWindow &window, const tgfx::Path &fullPath);
    tgfx::Path ResolvePositionedOutline(const ShapeGeometry &geometry, FillRule fillRule,
                                        bool hasTrim, const TrimWindow &window,
                                        const StrokeOptions &options, const tgfx::Path &fullPath,
                                        const tgfx::Path &strokeGeometry);
    // expansion==0 returns sourcePath without touching the derived LRU.
    // inverted is applied by the caller (toggleInverseFillType) so it stays
    // out of the cache key.
    tgfx::Path ResolveMaskExpanded(const ShapeGeometry &geometry, FillRule fillRule,
                                   float expansion, const tgfx::Path &sourcePath);

    // Drops all cached PathRefs so GPU shape proxies can become purgeable.
    void Clear();

  private:
    enum class DerivedPathKind : uint8_t {
        Trimmed = 0,
        PositionedOutline = 1,
        MaskExpanded = 2,
    };

    struct PathCacheKey {
        ShapeGeometryKind kind = ShapeGeometryKind::Path;
        FillRule fillRule = FillRule::NonZero;
        uint64_t contentHash = 0;

        bool operator==(const PathCacheKey &other) const {
            return kind == other.kind && fillRule == other.fillRule &&
                contentHash == other.contentHash;
        }
    };

    struct PathCacheKeyHash {
        size_t operator()(const PathCacheKey &key) const;
    };

    struct DerivedPathCacheKey {
        ShapeGeometryKind kind = ShapeGeometryKind::Path;
        FillRule fillRule = FillRule::NonZero;
        uint64_t contentHash = 0;
        DerivedPathKind derivedKind = DerivedPathKind::Trimmed;
        bool hasTrim = false;
        float trimStart = 0.0f;
        float trimEnd = 1.0f;
        StrokePosition position = StrokePosition::Center;
        float width = 0.0f;
        LineCap cap = LineCap::Butt;
        LineJoin join = LineJoin::Miter;
        float miterLimit = 4.0f;
        StrokeMode strokeMode = StrokeMode::Solid;
        float dashOffset = 0.0f;
        uint64_t dashHash = 0;
        // Meaningful when derivedKind == MaskExpanded.
        float expansion = 0.0f;

        bool operator==(const DerivedPathCacheKey &other) const;
    };

    struct DerivedPathCacheKeyHash {
        size_t operator()(const DerivedPathCacheKey &key) const;
    };

    using EntryList = std::list<std::pair<PathCacheKey, tgfx::Path>>;
    using DerivedEntryList = std::list<std::pair<DerivedPathCacheKey, tgfx::Path>>;

    EntryList order_ = {};
    std::unordered_map<PathCacheKey, EntryList::iterator, PathCacheKeyHash> index_ = {};
    DerivedEntryList derivedOrder_ = {};
    std::unordered_map<DerivedPathCacheKey, DerivedEntryList::iterator, DerivedPathCacheKeyHash>
        derivedIndex_ = {};
};

}  // namespace motion
