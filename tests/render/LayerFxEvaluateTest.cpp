#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::BuildCommands;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::DrawCommandType;
using motion::DropShadowStyle;
using motion::Expected;
using motion::FillStyle;
using motion::Layer;
using motion::LayerFxType;
using motion::LayerType;
using motion::PrecompContent;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::Vec2;

namespace {

struct RectScene {
    Document document;
    Composition *composition = nullptr;
    Layer *layer = nullptr;

    RectScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = 100;
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        auto rectElement = std::make_unique<ShapeRect>();
        rectElement->position.setStaticValue(Vec2{0, 0});
        rectElement->size.setStaticValue(Vec2{10, 10});
        content->geometry = std::move(rectElement);
        auto fill = std::make_unique<FillStyle>();
        fill->color.setStaticValue(Color{1, 0, 0, 1});
        layer->styles.push_back(std::move(fill));
    }

    Expected<SceneState, std::string> Evaluate() {
        return SceneEvaluator::Evaluate(document, composition->id, 0);
    }
};

}  // namespace

TEST(LayerFxEvaluateTest, DefaultDropShadowAppearsInSnapshot) {
    RectScene scene;
    scene.layer->layerStyles.push_back(std::make_unique<DropShadowStyle>());
    Expected<SceneState, std::string> result = scene.Evaluate();
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    ASSERT_EQ(result->layers[0].layerStyles.size(), 1u);
    EXPECT_EQ(result->layers[0].layerStyles[0]->type(), LayerFxType::DropShadow);
}

TEST(LayerFxEvaluateTest, DisabledStyleIsSkipped) {
    RectScene scene;
    auto style = std::make_unique<DropShadowStyle>();
    style->enabled = false;
    scene.layer->layerStyles.push_back(std::move(style));
    Expected<SceneState, std::string> result = scene.Evaluate();
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers[0].layerStyles.empty());
}

TEST(LayerFxEvaluateTest, PrecompStylesAreIgnored) {
    Document document;
    Composition *inner = document.addComposition(std::make_unique<Composition>());
    inner->duration = 100;
    Layer *rect = document.addLayer(inner->id, std::make_unique<Layer>(LayerType::Shape));
    rect->outPoint = 100;
    auto *content = static_cast<ShapeContent *>(rect->content.get());
    auto rectElement = std::make_unique<ShapeRect>();
    rectElement->position.setStaticValue(Vec2{0, 0});
    rectElement->size.setStaticValue(Vec2{10, 10});
    content->geometry = std::move(rectElement);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1, 0, 0, 1});
    rect->styles.push_back(std::move(fill));

    Composition *main = document.addComposition(std::make_unique<Composition>());
    main->duration = 100;
    Layer *precomp = document.addLayer(main->id, std::make_unique<Layer>(LayerType::Precomp));
    precomp->outPoint = 100;
    static_cast<PrecompContent *>(precomp->content.get())->compositionId = inner->id;
    precomp->layerStyles.push_back(std::make_unique<DropShadowStyle>());

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, main->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    EXPECT_TRUE(result->layers[0].layerStyles.empty());
    const auto commands = BuildCommands(*result);
    for (const auto &command : commands) {
        EXPECT_NE(command.type, DrawCommandType::BeginLayer);
        EXPECT_NE(command.type, DrawCommandType::EndLayer);
    }
}
