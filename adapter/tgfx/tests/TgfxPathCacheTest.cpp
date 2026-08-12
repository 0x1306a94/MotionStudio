#include <gtest/gtest.h>

#include <tgfx/core/Rect.h>

#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/render/ShapeGeometry.h"

#include "TgfxPathCache.h"

using motion::FillRule;
using motion::MakeRectGeometry;
using motion::TgfxPathCache;
using motion::Vec2;

TEST(TgfxPathCacheTest, MaskExpandedReusesPathRefAcrossCalls) {
    TgfxPathCache cache;
    const auto geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{100, 80}, 0.0f);
    const tgfx::Path source = cache.Resolve(geometry, FillRule::NonZero);

    const tgfx::Path first = cache.ResolveMaskExpanded(geometry, FillRule::NonZero, 8.0f, source);
    const tgfx::Path second = cache.ResolveMaskExpanded(geometry, FillRule::NonZero, 8.0f, source);

    EXPECT_FALSE(first.isEmpty());
    EXPECT_TRUE(first.isSame(second));
}

TEST(TgfxPathCacheTest, MaskExpandedZeroReturnsSourceWithoutDerivedEntry) {
    TgfxPathCache cache;
    const auto geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{100, 80}, 0.0f);
    const tgfx::Path source = cache.Resolve(geometry, FillRule::NonZero);

    const tgfx::Path expanded = cache.ResolveMaskExpanded(geometry, FillRule::NonZero, 0.0f, source);
    EXPECT_TRUE(expanded.isSame(source));
}

TEST(TgfxPathCacheTest, MaskExpandedDifferentExpansionMisses) {
    TgfxPathCache cache;
    const auto geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{100, 80}, 0.0f);
    const tgfx::Path source = cache.Resolve(geometry, FillRule::NonZero);

    const tgfx::Path grow = cache.ResolveMaskExpanded(geometry, FillRule::NonZero, 8.0f, source);
    const tgfx::Path shrink = cache.ResolveMaskExpanded(geometry, FillRule::NonZero, -8.0f, source);

    EXPECT_FALSE(grow.isSame(shrink));
    EXPECT_NE(grow, shrink);
}

// AE / PAG: negative expansion shrinks to a smaller solid (center stays covered).
TEST(TgfxPathCacheTest, MaskExpandedNegativeShrinksSolidNotRing) {
    TgfxPathCache cache;
    const auto geometry = MakeRectGeometry(Vec2{0, 0}, Vec2{200, 200}, 0.0f);
    const tgfx::Path source = cache.Resolve(geometry, FillRule::NonZero);
    const tgfx::Path shrink = cache.ResolveMaskExpanded(geometry, FillRule::NonZero, -40.0f, source);

    const tgfx::Rect sourceBounds = source.getBounds();
    const tgfx::Rect shrinkBounds = shrink.getBounds();
    EXPECT_FALSE(shrink.isEmpty());
    EXPECT_LT(shrinkBounds.width(), sourceBounds.width());
    EXPECT_LT(shrinkBounds.height(), sourceBounds.height());
    EXPECT_TRUE(shrink.contains(0.0f, 0.0f));
    EXPECT_FALSE(shrink.contains(95.0f, 0.0f));
}

TEST(TgfxPathCacheTest, ClearDropsCachedPathRefs) {
    TgfxPathCache cache;
    const auto geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{100, 80}, 0.0f);
    const tgfx::Path before = cache.Resolve(geometry, FillRule::NonZero);
    const tgfx::Path expanded = cache.ResolveMaskExpanded(geometry, FillRule::NonZero, 8.0f, before);

    cache.Clear();

    const tgfx::Path after = cache.Resolve(geometry, FillRule::NonZero);
    const tgfx::Path expandedAfter =
        cache.ResolveMaskExpanded(geometry, FillRule::NonZero, 8.0f, after);
    EXPECT_FALSE(before.isSame(after));
    EXPECT_FALSE(expanded.isSame(expandedAfter));
}
