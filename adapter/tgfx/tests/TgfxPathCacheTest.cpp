#include <gtest/gtest.h>

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
