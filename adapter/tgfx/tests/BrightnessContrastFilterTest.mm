#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "RenderCache.h"
#include "TgfxTestGPUEnvironment.h"
#include "effects/BrightnessContrastFilter.h"

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/Surface.h>

using motion::BrightnessContrastFilter;
using motion::RenderCache;
using tgfx_test::ChannelDelta;
using tgfx_test::MakeSolidImage;
using tgfx_test::Pixel;
using tgfx_test::ReadCenter;
using tgfx_test::TgfxTestGPUEnvironment;

TEST(BrightnessContrastFilterTest, IdentityLeavesOpaqueColorUnchanged) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{128.f / 255.f, 64.f / 255.f, 32.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto filtered = BrightnessContrastFilter::Apply(input, &cache, 0.f, 0.f, &offset);
    ASSERT_NE(filtered, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(filtered);
    Pixel center = {};
    ASSERT_TRUE(ReadCenter(env->surface(), kSize, &center));
    env->unlockContext();

    EXPECT_LE(ChannelDelta(center.r, 128), 2);
    EXPECT_LE(ChannelDelta(center.g, 64), 2);
    EXPECT_LE(ChannelDelta(center.b, 32), 2);
    EXPECT_GE(center.a, 250);
}

TEST(BrightnessContrastFilterTest, PositiveBrightnessRaisesValue) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{128.f / 255.f, 64.f / 255.f, 32.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto filtered = BrightnessContrastFilter::Apply(input, &cache, 100.f, 0.f, &offset);
    ASSERT_NE(filtered, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(filtered);
    Pixel center = {};
    ASSERT_TRUE(ReadCenter(env->surface(), kSize, &center));
    env->unlockContext();

    const int luma = static_cast<int>(center.r) + static_cast<int>(center.g) + static_cast<int>(center.b);
    EXPECT_GT(luma, 128 + 64 + 32 + 20);
}

TEST(BrightnessContrastFilterTest, PositiveContrastPushesAwayFromMidGrey) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{64.f / 255.f, 64.f / 255.f, 64.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto filtered = BrightnessContrastFilter::Apply(input, &cache, 0.f, 100.f, &offset);
    ASSERT_NE(filtered, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(filtered);
    Pixel center = {};
    ASSERT_TRUE(ReadCenter(env->surface(), kSize, &center));
    env->unlockContext();

    EXPECT_LT(static_cast<int>(center.r), 64 - 8);
    EXPECT_LT(static_cast<int>(center.g), 64 - 8);
    EXPECT_LT(static_cast<int>(center.b), 64 - 8);
}

TEST(BrightnessContrastFilterTest, SharesPipelineAcrossInstances) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{128.f / 255.f, 64.f / 255.f, 32.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto first = BrightnessContrastFilter::Apply(input, &cache, 0.f, 0.f, &offset);
    auto second = BrightnessContrastFilter::Apply(input, &cache, 100.f, 0.f, &offset);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(first);
    canvas->drawImage(second);
    Pixel center = {};
    ASSERT_TRUE(ReadCenter(env->surface(), kSize, &center));

    auto probe = std::make_shared<BrightnessContrastFilter>(&cache, 0.f, 0.f);
    auto *resources = cache.findFilterResources(probe->typeId());
    ASSERT_NE(resources, nullptr);
    ASSERT_NE(resources->pipeline, nullptr);
    env->unlockContext();
}

TEST(BrightnessContrastFilterTest, ApplyNullInputReturnsNull) {
    RenderCache cache;
    tgfx::Point offset = {};
    EXPECT_EQ(BrightnessContrastFilter::Apply(nullptr, &cache, 0.f, 0.f, &offset), nullptr);
}
