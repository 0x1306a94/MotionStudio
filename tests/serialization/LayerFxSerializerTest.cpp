#include <memory>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/serialization/Serializer.h"

using motion::BlendMode;
using motion::Composition;
using motion::Document;
using motion::DropShadowStyle;
using motion::Expected;
using motion::Layer;
using motion::LayerFxType;
using motion::LayerStrokeStyle;
using motion::LayerType;
using motion::OuterGlowStyle;
using motion::Serializer;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::StrokePosition;

namespace {

struct LayerFxDocument {
    Document document;
    Layer *layer = nullptr;

    LayerFxDocument() {
        Composition *composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;
        composition->width = 100;
        composition->height = 100;
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = 100;
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        content->geometry = std::make_unique<ShapeRect>();
        layer->styles.push_back(std::make_unique<motion::FillStyle>());
        layer->layerStyles.push_back(std::make_unique<DropShadowStyle>());
        layer->layerStyles.push_back(std::make_unique<OuterGlowStyle>());
        layer->layerStyles.push_back(std::make_unique<LayerStrokeStyle>());
    }
};

}  // namespace

TEST(LayerFxSerializerTest, RoundTripKeepsAllStyleTypes) {
    LayerFxDocument scene;
    const std::string first = Serializer::serialize(scene.document);
    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(first);
    ASSERT_TRUE(restored.hasValue());
    const auto &styles = (*restored)->compositions[0]->layers[0]->layerStyles;
    ASSERT_EQ(styles.size(), 3u);
    EXPECT_EQ(styles[0]->type(), LayerFxType::DropShadow);
    EXPECT_EQ(styles[1]->type(), LayerFxType::OuterGlow);
    EXPECT_EQ(styles[2]->type(), LayerFxType::Stroke);
    const auto *shadow = static_cast<const DropShadowStyle *>(styles[0].get());
    const auto *glow = static_cast<const OuterGlowStyle *>(styles[1].get());
    const auto *stroke = static_cast<const LayerStrokeStyle *>(styles[2].get());
    EXPECT_EQ(shadow->blendMode, BlendMode::Multiply);
    EXPECT_FLOAT_EQ(shadow->distance.evaluate(0), 5.0f);
    EXPECT_EQ(glow->blendMode, BlendMode::Screen);
    EXPECT_FLOAT_EQ(glow->range.evaluate(0), 1.0f);
    EXPECT_EQ(stroke->blendMode, BlendMode::Normal);
    EXPECT_EQ(stroke->position, StrokePosition::Outside);
    EXPECT_FLOAT_EQ(stroke->size.evaluate(0), 3.0f);
    EXPECT_EQ(Serializer::serialize(**restored), first);
}

TEST(LayerFxSerializerTest, MissingLayerStylesArrayLoadsEmpty) {
    LayerFxDocument scene;
    auto json = nlohmann::json::parse(Serializer::serialize(scene.document));
    json["compositions"][0]["layers"][0].erase("layerStyles");
    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    EXPECT_TRUE((*restored)->compositions[0]->layers[0]->layerStyles.empty());
}

TEST(LayerFxSerializerTest, UnknownTypeIsSkipped) {
    LayerFxDocument scene;
    auto json = nlohmann::json::parse(Serializer::serialize(scene.document));
    json["compositions"][0]["layers"][0]["layerStyles"] = nlohmann::json::array({
        {{"id", "aaaaaaaaaaaaaaaa"}, {"type", "nope"}},
        json["compositions"][0]["layers"][0]["layerStyles"][1],
        json["compositions"][0]["layers"][0]["layerStyles"][2],
    });
    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    const auto &styles = (*restored)->compositions[0]->layers[0]->layerStyles;
    ASSERT_EQ(styles.size(), 2u);
    EXPECT_EQ(styles[0]->type(), LayerFxType::OuterGlow);
    EXPECT_EQ(styles[1]->type(), LayerFxType::Stroke);
}
