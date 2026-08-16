#include <gtest/gtest.h>
#include <memory>

#include <tgfx/core/ColorType.h>

#include "RenderCache.h"
#include "TgfxTestGPUEnvironment.h"

using motion::FilterResources;
using motion::RenderCache;

TEST(RenderCacheTest, StoresAndClearsFilterResources) {
    RenderCache cache;
    EXPECT_EQ(cache.findFilterResources(1), nullptr);

    auto resources = std::make_unique<FilterResources>();
    FilterResources *raw = resources.get();
    cache.addFilterResources(1, std::move(resources));
    EXPECT_EQ(cache.findFilterResources(1), raw);
    EXPECT_EQ(cache.findFilterResources(2), nullptr);

    cache.addFilterResources(1, nullptr);
    EXPECT_EQ(cache.findFilterResources(1), raw);

    cache.releaseAll();
    EXPECT_EQ(cache.findFilterResources(1), nullptr);
}

TEST(RenderCacheTest, PoolsTwoSurfacesPerSizeAndColorType) {
    auto env = tgfx_test::TgfxTestGPUEnvironment::Make(64, 64);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    tgfx::Context *context = env->lockContext();
    ASSERT_NE(context, nullptr);
    RenderCache *cache = env->renderCache();
    cache->attachToContext(context);
    cache->beginFrame();

    std::shared_ptr<tgfx::Surface> rgbaA =
        cache->acquireSurface(32, 48, tgfx::ColorType::RGBA_8888);
    std::shared_ptr<tgfx::Surface> rgbaB =
        cache->acquireSurface(32, 48, tgfx::ColorType::RGBA_8888);
    ASSERT_NE(rgbaA, nullptr);
    ASSERT_NE(rgbaB, nullptr);
    EXPECT_NE(rgbaA.get(), rgbaB.get());

    std::shared_ptr<tgfx::Surface> rgbaOverflow =
        cache->acquireSurface(32, 48, tgfx::ColorType::RGBA_8888);
    ASSERT_NE(rgbaOverflow, nullptr);
    EXPECT_NE(rgbaOverflow.get(), rgbaA.get());
    EXPECT_NE(rgbaOverflow.get(), rgbaB.get());

    cache->beginFrame();
    std::shared_ptr<tgfx::Surface> rgbaA2 =
        cache->acquireSurface(32, 48, tgfx::ColorType::RGBA_8888);
    std::shared_ptr<tgfx::Surface> rgbaB2 =
        cache->acquireSurface(32, 48, tgfx::ColorType::RGBA_8888);
    EXPECT_EQ(rgbaA2.get(), rgbaA.get());
    EXPECT_EQ(rgbaB2.get(), rgbaB.get());

    std::shared_ptr<tgfx::Surface> rgbaOther =
        cache->acquireSurface(16, 16, tgfx::ColorType::RGBA_8888);
    ASSERT_NE(rgbaOther, nullptr);
    EXPECT_NE(rgbaOther.get(), rgbaA.get());
    EXPECT_NE(rgbaOther.get(), rgbaB.get());

    std::shared_ptr<tgfx::Surface> alphaA =
        cache->acquireSurface(32, 48, tgfx::ColorType::ALPHA_8);
    std::shared_ptr<tgfx::Surface> alphaB =
        cache->acquireSurface(32, 48, tgfx::ColorType::ALPHA_8);
    ASSERT_NE(alphaA, nullptr);
    ASSERT_NE(alphaB, nullptr);
    EXPECT_NE(alphaA.get(), rgbaA.get());
    EXPECT_NE(alphaA.get(), alphaB.get());

    cache->releaseAll();
    std::shared_ptr<tgfx::Surface> rgbaAfterRelease =
        cache->acquireSurface(32, 48, tgfx::ColorType::RGBA_8888);
    ASSERT_NE(rgbaAfterRelease, nullptr);
    EXPECT_NE(rgbaAfterRelease.get(), rgbaA.get());
    EXPECT_NE(rgbaAfterRelease.get(), rgbaB.get());
    env->unlockContext();
}
