#include <gtest/gtest.h>

#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerFx.h"

using motion::BlendMode;
using motion::DropShadowStyle;
using motion::Layer;
using motion::LayerFxDrawPosition;
using motion::LayerFxType;
using motion::LayerStrokeStyle;
using motion::LayerType;
using motion::OuterGlowStyle;
using motion::StrokePosition;

TEST(LayerFxTest, DefaultDropShadowIsVisibleSnapshot) {
    DropShadowStyle style;
    EXPECT_EQ(style.type(), LayerFxType::DropShadow);
    EXPECT_EQ(style.drawPosition(), LayerFxDrawPosition::Behind);
    EXPECT_EQ(style.blendMode, BlendMode::Multiply);
    const auto snap = style.snapshot(0);
    ASSERT_NE(snap, nullptr);
    const auto &baked = static_cast<const DropShadowStyle &>(*snap);
    EXPECT_FLOAT_EQ(baked.distance.evaluate(0), 5.0f);
    EXPECT_FLOAT_EQ(baked.size.evaluate(0), 5.0f);
    EXPECT_FLOAT_EQ(baked.opacity.evaluate(0), 0.75f);
}

TEST(LayerFxTest, DropShadowIdentityIsNull) {
    DropShadowStyle style;
    style.distance.setStaticValue(0.0f);
    style.size.setStaticValue(0.0f);
    style.spread.setStaticValue(0.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
    style.enabled = false;
    style.distance.setStaticValue(5.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
}

TEST(LayerFxTest, OuterGlowClampsRangeAndSkipsZeroSize) {
    OuterGlowStyle style;
    EXPECT_EQ(style.blendMode, BlendMode::Screen);
    style.range.setStaticValue(0.0f);
    const auto snap = style.snapshot(0);
    ASSERT_NE(snap, nullptr);
    const auto &baked = static_cast<const OuterGlowStyle &>(*snap);
    EXPECT_FLOAT_EQ(baked.range.evaluate(0), 0.01f);
    style.size.setStaticValue(0.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
}

TEST(LayerFxTest, StrokeDrawsAboveAndSkipsZeroSize) {
    LayerStrokeStyle style;
    EXPECT_EQ(style.type(), LayerFxType::Stroke);
    EXPECT_EQ(style.drawPosition(), LayerFxDrawPosition::Above);
    EXPECT_EQ(style.position, StrokePosition::Outside);
    EXPECT_NE(style.snapshot(0), nullptr);
    style.size.setStaticValue(0.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
}

TEST(LayerFxTest, LayerHoldsLayerStylesVector) {
    Layer layer(LayerType::Shape);
    layer.layerStyles.push_back(std::make_unique<DropShadowStyle>());
    ASSERT_EQ(layer.layerStyles.size(), 1u);
    EXPECT_EQ(layer.layerStyles[0]->type(), LayerFxType::DropShadow);
}
