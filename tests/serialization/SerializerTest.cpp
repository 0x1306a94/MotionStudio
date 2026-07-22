#include <memory>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeGroup.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeStroke.h"
#include "MotionStudio/model/ShapeTrimPath.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/serialization/SchemaMigrator.h"
#include "MotionStudio/serialization/Serializer.h"

using motion::Asset;
using motion::AssetType;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::DocumentFingerprint;
using motion::Easing;
using motion::Expected;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::MaskMode;
using motion::PrecompContent;
using motion::SchemaMigrator;
using motion::Serializer;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapeFill;
using motion::ShapeGroup;
using motion::ShapePath;
using motion::ShapeRect;
using motion::ShapeStroke;
using motion::ShapeTrimPath;
using motion::Vec2;

namespace {

// Document covering all types and both static/animated properties.
std::unique_ptr<Document> BuildRichDocument() {
    auto document = std::make_unique<Document>();
    document->name = "rich";

    Asset asset;
    asset.type = AssetType::Image;
    asset.name = "sprite.png";
    asset.path = "assets/sprite.png";
    document->assets.push_back(asset);

    auto precompTarget = std::make_unique<Composition>();
    precompTarget->name = "nested";
    precompTarget->duration = 30;
    precompTarget->layers.push_back(std::make_unique<Layer>(LayerType::Null));

    auto composition = std::make_unique<Composition>();
    composition->name = "main";
    composition->duration = 90;
    composition->frameRate = {30000, 1001};
    composition->width = 1280;
    composition->height = 720;
    composition->backgroundColor = Color{0.1f, 0.2f, 0.3f, 1};
    composition->cornerRadius = 18.0f;

    // Shape layer: 7 shape element types + keyframes + spatial tangents + mask.
    auto shapeLayer = std::make_unique<Layer>(LayerType::Shape);
    shapeLayer->name = "shapes";
    shapeLayer->outPoint = 90;

    Keyframe<Vec2> positionFrom;
    positionFrom.time = 0;
    positionFrom.value = Vec2{0, 0};
    positionFrom.easing = Easing::EaseIn();
    positionFrom.spatialOutTangent = Vec2{30, 0};
    Keyframe<Vec2> positionTo;
    positionTo.time = 60;
    positionTo.value = Vec2{400, 0};
    positionTo.spatialInTangent = Vec2{-30, 0};
    shapeLayer->transform.position.addKeyframe(positionFrom);
    shapeLayer->transform.position.addKeyframe(positionTo);
    shapeLayer->transform.rotation.setStaticValue(45.0f);

    motion::Mask mask;
    mask.mode = MaskMode::Subtract;
    mask.path.vertices.push_back({{0, 0}, {}, {}});
    mask.path.closed = true;
    mask.inverted = true;
    shapeLayer->masks.push_back(mask);

    auto *shapeContent = static_cast<ShapeContent *>(shapeLayer->content.get());

    auto path = std::make_unique<ShapePath>();
    motion::BezierPath bezier;
    bezier.closed = true;
    bezier.vertices.push_back({{0, 0}, {-1, 0}, {1, 0}});
    bezier.vertices.push_back({{10, 0}, {-1, 0}, {1, 0}});
    path->path.setStaticValue(bezier);
    shapeContent->elements.push_back(std::move(path));

    auto fill = std::make_unique<ShapeFill>();
    Keyframe<Color> colorKeyframe;
    colorKeyframe.time = 10;
    colorKeyframe.value = Color{1, 0, 0, 1};
    colorKeyframe.easing = Easing::Bezier(0.3f, 0.1f, 0.7f, 0.9f);
    fill->color.addKeyframe(colorKeyframe);
    shapeContent->elements.push_back(std::move(fill));

    auto stroke = std::make_unique<ShapeStroke>();
    stroke->width.setStaticValue(3.5f);
    shapeContent->elements.push_back(std::move(stroke));

    auto group = std::make_unique<ShapeGroup>();
    group->transform.position.setStaticValue(Vec2{5, 5});
    group->elements.push_back(std::make_unique<ShapeRect>());
    group->elements.push_back(std::make_unique<ShapeEllipse>());
    shapeContent->elements.push_back(std::move(group));

    shapeContent->elements.push_back(std::make_unique<ShapeTrimPath>());

    composition->layers.push_back(std::move(shapeLayer));

    // Null parent layer + child layer referencing it.
    auto nullLayer = std::make_unique<Layer>(LayerType::Null);
    const motion::EntityId nullId = nullLayer->id;
    composition->layers.push_back(std::move(nullLayer));

    auto textLayer = std::make_unique<Layer>(LayerType::Text);
    textLayer->parentId = nullId;
    auto *textContent = static_cast<motion::TextContent *>(textLayer->content.get());
    textContent->text.setStaticValue(std::string{"hello"});
    textContent->fontFamily = "PingFang SC";
    composition->layers.push_back(std::move(textLayer));

    auto imageLayer = std::make_unique<Layer>(LayerType::Image);
    static_cast<motion::ImageContent *>(imageLayer->content.get())->assetId = asset.id;
    composition->layers.push_back(std::move(imageLayer));

    auto precompLayer = std::make_unique<Layer>(LayerType::Precomp);
    static_cast<PrecompContent *>(precompLayer->content.get())->compositionId =
        precompTarget->id;
    composition->layers.push_back(std::move(precompLayer));

    document->compositions.push_back(std::move(precompTarget));
    document->compositions.push_back(std::move(composition));
    document->refreshEntityIndex();
    return document;
}

}  // namespace

TEST(SerializerTest, RoundTripIsJsonStable) {
    auto document = BuildRichDocument();
    const std::string first = Serializer::serialize(*document);

    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(first);
    ASSERT_TRUE(restored.hasValue());
    const std::string second = Serializer::serialize(**restored);

    EXPECT_EQ(first, second);
}

TEST(SerializerTest, RestoredModelEvaluatesAndIndexes) {
    auto document = BuildRichDocument();
    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(Serializer::serialize(*document));
    ASSERT_TRUE(restored.hasValue());

    ASSERT_EQ((*restored)->compositions.size(), 2u);
    const Composition &main = *(*restored)->compositions[1];
    ASSERT_EQ(main.layers.size(), 5u);
    EXPECT_EQ(main.frameRate, (motion::FrameRate{30000, 1001}));

    const Layer &shapes = *main.layers[0];
    // Keyframes + easing + spatial tangents restored: per-frame evaluation matches original.
    const Layer &originalShapes = *document->compositions[1]->layers[0];
    for (motion::FrameTime time : {0, 15, 30, 45, 60, 90}) {
        EXPECT_TRUE(motion::ApproxEqual(shapes.transform.position.evaluate(time),
                                        originalShapes.transform.position.evaluate(time)))
            << "time=" << time;
    }
    EXPECT_TRUE(shapes.transform.position.isAnimated());
    EXPECT_EQ(shapes.transform.position.keyframes()[0].easing, Easing::EaseIn());
    EXPECT_EQ(shapes.transform.rotation.staticValue(), 45.0f);

    // EntityIndex rebuilt: original IDs resolve correctly.
    EXPECT_NE((*restored)->entityIndex().findLayer(shapes.id), nullptr);
    const auto *shapeContent = static_cast<const ShapeContent *>(shapes.content.get());
    EXPECT_NE((*restored)->entityIndex().findShape(shapeContent->elements[0]->id),
              nullptr);

    // Parent-child relationship preserved.
    const Layer &textLayer = *main.layers[2];
    EXPECT_EQ(textLayer.parentId, main.layers[1]->id);
}

TEST(SerializerTest, AnimatableJsonShape) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Null));
    layer->transform.rotation.setStaticValue(30.0f);
    Keyframe<Vec2> keyframe;
    keyframe.time = 5;
    keyframe.value = Vec2{1, 2};
    layer->transform.position.addKeyframe(keyframe);

    const auto json = nlohmann::json::parse(Serializer::serialize(document));
    const auto &transform =
        json["compositions"][0]["layers"][0]["transform"];
    EXPECT_EQ(transform["rotation"]["static"], 30.0f);
    EXPECT_EQ(transform["position"]["keyframes"][0]["time"], 5);
    EXPECT_FALSE(transform["position"].contains("static"));
}

TEST(SerializerTest, EntityIdSerializesAs16HexChars) {
    Document document;
    const auto json = nlohmann::json::parse(Serializer::serialize(document));
    const std::string id = json["id"].get<std::string>();
    EXPECT_EQ(id.size(), 16u);
    EXPECT_EQ(id.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST(SerializerTest, RejectsInvalidInput) {
    EXPECT_FALSE(Serializer::deserialize("not json").hasValue());
    EXPECT_FALSE(Serializer::deserialize("{}").hasValue());

    Expected<std::unique_ptr<Document>, std::string> badVersion =
        Serializer::deserialize(R"({"schemaVersion": 99})");
    ASSERT_FALSE(badVersion.hasValue());
    EXPECT_NE(badVersion.error().find("schemaVersion"), std::string::npos);

    auto document = BuildRichDocument();
    std::string text = Serializer::serialize(*document);
    const std::string broken = text.replace(text.find("\"type\": \"shape\""), 15,
                                            "\"type\": \"bogus\"");
    EXPECT_FALSE(Serializer::deserialize(broken).hasValue());
}

TEST(SchemaMigratorTest, CurrentVersionPassesThrough) {
    const std::string text = R"({"schemaVersion": 1, "name": "x"})";
    Expected<std::string, std::string> migrated = SchemaMigrator::migrate(text);
    ASSERT_TRUE(migrated.hasValue());
    EXPECT_EQ(nlohmann::json::parse(*migrated), nlohmann::json::parse(text));
}

TEST(SchemaMigratorTest, RejectsUnknownVersions) {
    EXPECT_FALSE(SchemaMigrator::migrate("{}").hasValue());
    EXPECT_FALSE(SchemaMigrator::migrate(R"({"schemaVersion": 0})").hasValue());
    EXPECT_FALSE(SchemaMigrator::migrate(R"({"schemaVersion": 2})").hasValue());
}

TEST(FingerprintTest, StableAndSensitiveToChange) {
    auto document = BuildRichDocument();
    const uint64_t before = DocumentFingerprint(*document);
    EXPECT_EQ(DocumentFingerprint(*document), before);

    document->compositions[1]->layers[0]->transform.rotation.setStaticValue(90.0f);
    EXPECT_NE(DocumentFingerprint(*document), before);
}
