#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/serialization/SchemaMigrator.h"
#include "MotionStudio/serialization/Serializer.h"

using motion::Asset;
using motion::AssetType;
using motion::Color;
using motion::Composition;
using motion::documentFingerprint;
using motion::Document;
using motion::Easing;
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

// 覆盖全部类型与双态属性的文档。
std::unique_ptr<Document> buildRichDocument() {
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

    // Shape 图层：7 种形状元素 + 关键帧 + 空间手柄 + mask。
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

    auto* shapeContent = static_cast<ShapeContent*>(shapeLayer->content.get());

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

    // Null 父图层 + 引用它的子图层。
    auto nullLayer = std::make_unique<Layer>(LayerType::Null);
    const motion::EntityId nullId = nullLayer->id;
    composition->layers.push_back(std::move(nullLayer));

    auto textLayer = std::make_unique<Layer>(LayerType::Text);
    textLayer->setParent(nullId, *document);  // document 索引此时为空 → 拒绝；序列化仍保留目标 ID
    textLayer->parentId = nullId;            // 直接建立父子关系
    auto* textContent = static_cast<motion::TextContent*>(textLayer->content.get());
    textContent->text.setStaticValue(std::string{"hello"});
    textContent->fontFamily = "PingFang SC";
    composition->layers.push_back(std::move(textLayer));

    auto imageLayer = std::make_unique<Layer>(LayerType::Image);
    static_cast<motion::ImageContent*>(imageLayer->content.get())->assetId = asset.id;
    composition->layers.push_back(std::move(imageLayer));

    auto precompLayer = std::make_unique<Layer>(LayerType::Precomp);
    static_cast<PrecompContent*>(precompLayer->content.get())->compositionId =
        precompTarget->id;
    composition->layers.push_back(std::move(precompLayer));

    document->compositions.push_back(std::move(precompTarget));
    document->compositions.push_back(std::move(composition));
    document->refreshEntityIndex();
    return document;
}

}  // namespace

TEST(SerializerTest, RoundTripIsJsonStable) {
    auto document = buildRichDocument();
    const std::string first = Serializer::serialize(*document);

    auto restored = Serializer::deserialize(first);
    const std::string second = Serializer::serialize(*restored);

    EXPECT_EQ(first, second);
}

TEST(SerializerTest, RestoredModelEvaluatesAndIndexes) {
    auto document = buildRichDocument();
    auto restored = Serializer::deserialize(Serializer::serialize(*document));

    ASSERT_EQ(restored->compositions.size(), 2u);
    const Composition& main = *restored->compositions[1];
    ASSERT_EQ(main.layers.size(), 5u);
    EXPECT_EQ(main.frameRate, (motion::FrameRate{30000, 1001}));

    const Layer& shapes = *main.layers[0];
    // 关键帧 + 缓动 + 空间手柄恢复：逐帧求值与原文档一致。
    const Layer& originalShapes = *document->compositions[1]->layers[0];
    for (motion::FrameTime time : {0, 15, 30, 45, 60, 90}) {
        EXPECT_TRUE(motion::approxEqual(shapes.transform.position.evaluate(time),
                                        originalShapes.transform.position.evaluate(time)))
            << "time=" << time;
    }
    EXPECT_TRUE(shapes.transform.position.isAnimated());
    EXPECT_EQ(shapes.transform.position.keyframes()[0].easing, Easing::EaseIn());
    EXPECT_EQ(shapes.transform.rotation.staticValue(), 45.0f);

    // EntityIndex 已重建：按原 ID 可解析。
    EXPECT_NE(restored->entityIndex().findLayer(shapes.id), nullptr);
    const auto* shapeContent = static_cast<const ShapeContent*>(shapes.content.get());
    EXPECT_NE(restored->entityIndex().findShape(shapeContent->elements[0]->id), nullptr);

    // 父子关系保留。
    const Layer& textLayer = *main.layers[2];
    EXPECT_EQ(textLayer.parentId, main.layers[1]->id);
}

TEST(SerializerTest, AnimatableJsonShape) {
    Document document;
    Composition* composition = document.addComposition(std::make_unique<Composition>());
    Layer* layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Null));
    layer->transform.rotation.setStaticValue(30.0f);
    Keyframe<Vec2> keyframe;
    keyframe.time = 5;
    keyframe.value = Vec2{1, 2};
    layer->transform.position.addKeyframe(keyframe);

    const auto json = nlohmann::json::parse(Serializer::serialize(document));
    const auto& transform =
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
    EXPECT_THROW(Serializer::deserialize("not json"), std::invalid_argument);
    EXPECT_THROW(Serializer::deserialize("{}"), std::invalid_argument);
    EXPECT_THROW(Serializer::deserialize(R"({"schemaVersion": 99})"),
                 std::invalid_argument);

    auto document = buildRichDocument();
    std::string text = Serializer::serialize(*document);
    // 破坏 layer 类型字段。
    const auto broken = text.replace(text.find("\"type\": \"shape\""), 15,
                                     "\"type\": \"bogus\"");
    EXPECT_THROW(Serializer::deserialize(broken), std::invalid_argument);
}

TEST(SchemaMigratorTest, CurrentVersionPassesThrough) {
    const std::string text = R"({"schemaVersion": 1, "name": "x"})";
    EXPECT_EQ(nlohmann::json::parse(SchemaMigrator::migrate(text)),
              nlohmann::json::parse(text));
}

TEST(SchemaMigratorTest, RejectsUnknownVersions) {
    EXPECT_THROW(SchemaMigrator::migrate("{}"), std::invalid_argument);
    EXPECT_THROW(SchemaMigrator::migrate(R"({"schemaVersion": 0})"),
                 std::invalid_argument);
    EXPECT_THROW(SchemaMigrator::migrate(R"({"schemaVersion": 2})"),
                 std::invalid_argument);
}

TEST(FingerprintTest, StableAndSensitiveToChange) {
    auto document = buildRichDocument();
    const uint64_t before = documentFingerprint(*document);
    EXPECT_EQ(documentFingerprint(*document), before);

    document->compositions[1]->layers[0]->transform.rotation.setStaticValue(90.0f);
    EXPECT_NE(documentFingerprint(*document), before);
}
