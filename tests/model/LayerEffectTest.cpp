#include <gtest/gtest.h>

#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerEffect.h"

using motion::BrightnessContrastEffect;
using motion::GaussianBlurEffect;
using motion::Layer;
using motion::LayerEffectType;
using motion::LayerType;

TEST(LayerEffectTest, DefaultBrightnessContrastIsIdentitySnapshot) {
    BrightnessContrastEffect effect;
    EXPECT_EQ(effect.type(), LayerEffectType::BrightnessContrast);
    EXPECT_TRUE(effect.enabled);
    EXPECT_EQ(effect.snapshot(0), nullptr);
}

TEST(LayerEffectTest, DisabledEffectSnapshotsToNull) {
    BrightnessContrastEffect effect;
    effect.enabled = false;
    effect.brightness.setStaticValue(40.0f);
    EXPECT_EQ(effect.snapshot(0), nullptr);
}

TEST(LayerEffectTest, BakesStaticValuesAndKeepsId) {
    BrightnessContrastEffect effect;
    effect.brightness.setStaticValue(40.0f);
    effect.contrast.setStaticValue(-10.0f);
    const auto snap = effect.snapshot(0);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->id, effect.id);
    EXPECT_EQ(snap->type(), LayerEffectType::BrightnessContrast);
    const auto &baked = static_cast<const BrightnessContrastEffect &>(*snap);
    EXPECT_FLOAT_EQ(baked.brightness.evaluate(0), 40.0f);
    EXPECT_FLOAT_EQ(baked.contrast.evaluate(0), -10.0f);
    EXPECT_FALSE(baked.brightness.isAnimated());
}

TEST(LayerEffectTest, GaussianBlurZeroBlurrinessIsIdentity) {
    GaussianBlurEffect effect;
    EXPECT_EQ(effect.snapshot(0), nullptr);
    effect.blurriness.setStaticValue(8.0f);
    const auto snap = effect.snapshot(0);
    ASSERT_NE(snap, nullptr);
    const auto &baked = static_cast<const GaussianBlurEffect &>(*snap);
    EXPECT_FLOAT_EQ(baked.blurriness.evaluate(0), 8.0f);
    EXPECT_FALSE(baked.repeatEdgePixels);
}

TEST(LayerEffectTest, LayerHoldsEffectsVector) {
    Layer layer(LayerType::Shape);
    layer.effects.push_back(std::make_unique<GaussianBlurEffect>());
    ASSERT_EQ(layer.effects.size(), 1u);
    EXPECT_EQ(layer.effects[0]->type(), LayerEffectType::GaussianBlur);
}
