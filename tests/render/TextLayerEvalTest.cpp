#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/render/HitTest.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::BoundsOfLayerLocal;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::FillStyle;
using motion::HitTestLayer;
using motion::Layer;
using motion::LayerType;
using motion::SceneEvaluator;
using motion::StrokeStyle;
using motion::TextAlign;
using motion::TextContent;
using motion::Vec2;

TEST(TextLayerEvalTest, EvaluatesDefaultsAndStyles) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 800;
    composition->height = 600;
    composition->duration = 120;
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    layer->inPoint = 0;
    layer->outPoint = 120;
    auto *text = static_cast<TextContent *>(layer->content.get());
    text->text.setStaticValue("Hello");
    text->fontSize.setStaticValue(32.0f);
    text->size.setStaticValue(Vec2{200, 80});
    text->align = TextAlign::Center;
    text->fontFamily = "Helvetica";

    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1, 0, 0, 1});
    layer->styles.push_back(std::move(fill));
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 0, 1, 1});
    stroke->width.setStaticValue(2.0f);
    layer->styles.push_back(std::move(stroke));
    document.refreshEntityIndex();

    auto state = SceneEvaluator::Evaluate(document, composition->id, 0);
    ASSERT_TRUE(state.hasValue()) << state.error();
    ASSERT_EQ(state->layers.size(), 1u);
    ASSERT_TRUE(state->layers[0].textItem.has_value());
    const auto &item = *state->layers[0].textItem;
    EXPECT_EQ(item.text, "Hello");
    EXPECT_FLOAT_EQ(item.fontSize, 32.0f);
    EXPECT_FLOAT_EQ(item.containerSize.x, 200.0f);
    EXPECT_FLOAT_EQ(item.containerSize.y, 80.0f);
    EXPECT_EQ(item.hitSize, item.containerSize);
    EXPECT_EQ(item.align, TextAlign::Center);
    EXPECT_EQ(item.fontFamily, "Helvetica");
    ASSERT_EQ(item.styles.size(), 2u);
    EXPECT_FALSE(item.styles[0].isStroke);
    EXPECT_FLOAT_EQ(item.styles[0].color.r, 1.0f);
    EXPECT_TRUE(item.styles[1].isStroke);
    EXPECT_FLOAT_EQ(item.styles[1].color.b, 1.0f);
    EXPECT_FLOAT_EQ(item.styles[1].strokeWidth, 2.0f);

    Vec2 minPoint;
    Vec2 maxPoint;
    ASSERT_TRUE(BoundsOfLayerLocal(state->layers[0], minPoint, maxPoint));
    EXPECT_EQ(minPoint, (Vec2{0, 0}));
    EXPECT_EQ(maxPoint, (Vec2{200, 80}));

    EXPECT_TRUE(
        HitTestLayer(state->layers[0], state->layers[0].worldTransform.transformPoint({100, 40}), 0));
    EXPECT_FALSE(
        HitTestLayer(state->layers[0], state->layers[0].worldTransform.transformPoint({300, 40}), 0));
}

TEST(TextLayerEvalTest, DefaultFillIsBlackWithoutStyles) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->duration = 30;
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    layer->inPoint = 0;
    layer->outPoint = 30;
    document.refreshEntityIndex();

    auto state = SceneEvaluator::Evaluate(document, composition->id, 0);
    ASSERT_TRUE(state.hasValue()) << state.error();
    ASSERT_EQ(state->layers.size(), 1u);
    ASSERT_TRUE(state->layers[0].textItem.has_value());
    ASSERT_EQ(state->layers[0].textItem->styles.size(), 1u);
    EXPECT_FALSE(state->layers[0].textItem->styles[0].isStroke);
    EXPECT_FLOAT_EQ(state->layers[0].textItem->styles[0].color.r, 0.0f);
    EXPECT_FLOAT_EQ(state->layers[0].textItem->styles[0].color.a, 1.0f);
}
