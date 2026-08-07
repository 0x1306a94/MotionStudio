#include <memory>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeTrimPath.h"
#include "MotionStudio/model/ShapeType.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/serialization/Dto.h"
#include "MotionStudio/serialization/SchemaMigrator.h"
#include "MotionStudio/serialization/Serializer.h"

using motion::Asset;
using motion::AssetType;
using motion::BezierPath;
using motion::BezierPathToVectorNetwork;
using motion::BindShaderPaint;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::DocumentFingerprint;
using motion::Easing;
using motion::Expected;
using motion::FillStyle;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::MakeSingleContour;
using motion::MaskMode;
using motion::PrecompContent;
using motion::SchemaMigrator;
using motion::Serializer;
using motion::ShaderDefinition;
using motion::ShaderUniformDecl;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapePath;
using motion::ShapeRect;
using motion::ShapeTrimPath;
using motion::ShapeType;
using motion::StylePaintMode;
using motion::UniformFormat;
using motion::ValidateShaderReferences;
using motion::Vec2;
using motion::VectorNetwork;
using motion::VertexMirrorMode;

namespace {

// Document covering all types and both static/animated properties.
std::unique_ptr<Document> BuildRichDocument() {
    auto document = std::make_unique<Document>();
    document->name = "rich";

    Asset asset;
    asset.type = AssetType::Image;
    asset.name = "sprite.png";
    asset.path = "assets/sprite.png";
    asset.width = 640;
    asset.height = 480;
    document->assets.push_back(asset);

    auto precompTarget = std::make_unique<Composition>();
    precompTarget->name = "nested";
    precompTarget->duration = 30;
    precompTarget->layers.push_back(std::make_unique<Layer>(LayerType::Group));

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
    motion::BezierPath maskPath = MakeSingleContour({{{0, 0}, {}, {}}}, false);
    maskPath.contours[0].closed = true;
    mask.path.setStaticValue(BezierPathToVectorNetwork(maskPath));
    mask.inverted = true;
    mask.feather.setStaticValue(2.0f);
    mask.expansion.setStaticValue(-1.0f);
    shapeLayer->masks.push_back(mask);
    shapeLayer->trackMatteType = motion::TrackMatteType::Alpha;

    auto fillStyle = std::make_unique<FillStyle>();
    Keyframe<Color> styleColorKeyframe;
    styleColorKeyframe.time = 10;
    styleColorKeyframe.value = Color{0.2f, 0.4f, 0.6f, 1};
    fillStyle->color.addKeyframe(styleColorKeyframe);
    fillStyle->blendMode = motion::BlendMode::Overlay;
    shapeLayer->styles.push_back(std::move(fillStyle));

    auto strokeStyle = std::make_unique<motion::StrokeStyle>();
    strokeStyle->width.setStaticValue(4.0f);
    strokeStyle->blendMode = motion::BlendMode::Multiply;
    strokeStyle->position = motion::StrokePosition::Inside;
    strokeStyle->trimStart.setStaticValue(0.25f);
    strokeStyle->trimEnd.setStaticValue(0.75f);
    Keyframe<float> trimOffsetKeyframe;
    trimOffsetKeyframe.time = 20;
    trimOffsetKeyframe.value = 90.0f;
    strokeStyle->trimOffset.addKeyframe(trimOffsetKeyframe);
    shapeLayer->styles.push_back(std::move(strokeStyle));

    auto *shapeContent = static_cast<ShapeContent *>(shapeLayer->content.get());

    auto path = std::make_unique<ShapePath>();
    motion::BezierPath bezier = MakeSingleContour({{{0, 0}, {-1, 0}, {1, 0}}, {{10, 0}, {-1, 0}, {1, 0}}}, true);
    path->path.setStaticValue(BezierPathToVectorNetwork(bezier));
    shapeContent->geometry = std::move(path);

    composition->layers.push_back(std::move(shapeLayer));

    // Additional shape layers cover the other geometry variants (one geometry per layer).
    auto rectLayer = std::make_unique<Layer>(LayerType::Shape);
    static_cast<ShapeContent *>(rectLayer->content.get())->geometry =
        std::make_unique<ShapeRect>();
    composition->layers.push_back(std::move(rectLayer));

    auto ellipseLayer = std::make_unique<Layer>(LayerType::Shape);
    static_cast<ShapeContent *>(ellipseLayer->content.get())->geometry =
        std::make_unique<ShapeEllipse>();
    composition->layers.push_back(std::move(ellipseLayer));

    auto trimLayer = std::make_unique<Layer>(LayerType::Shape);
    static_cast<ShapeContent *>(trimLayer->content.get())->geometry =
        std::make_unique<ShapeTrimPath>();
    composition->layers.push_back(std::move(trimLayer));

    // Group parent layer + child layer referencing it.
    auto nullLayer = std::make_unique<Layer>(LayerType::Group);
    const motion::EntityId nullId = nullLayer->id;
    composition->layers.push_back(std::move(nullLayer));

    auto textLayer = std::make_unique<Layer>(LayerType::Text);
    textLayer->parentId = nullId;
    auto *textContent = static_cast<motion::TextContent *>(textLayer->content.get());
    textContent->text.setStaticValue(std::string{"hello"});
    textContent->fontFamily = "PingFang SC";
    textContent->fontStyle = "Regular";
    textContent->fontSize = 36.0f;
    textContent->size = Vec2{320, 96};
    textContent->boxTextMode = true;
    textContent->align = motion::TextAlign::Center;
    composition->layers.push_back(std::move(textLayer));

    auto imageLayer = std::make_unique<Layer>(LayerType::Image);
    auto *imageContent = static_cast<motion::ImageContent *>(imageLayer->content.get());
    imageContent->assetId = asset.id;
    imageContent->size.setStaticValue(Vec2{320, 240});
    imageContent->scaleMode = motion::ImageScaleMode::Zoom;
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

TEST(SerializerTest, FillWithoutBlendModeDefaultsToNormal) {
    auto document = BuildRichDocument();
    auto json = nlohmann::json::parse(Serializer::serialize(*document));
    for (auto &layer : json["compositions"][1]["layers"]) {
        for (auto &style : layer["styles"]) {
            style.erase("blendMode");
            style.erase("position");
            style.erase("trimStart");
            style.erase("trimEnd");
            style.erase("trimOffset");
        }
    }

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    const Layer &shapes = *(*restored)->compositions[1]->layers[0];
    ASSERT_EQ(shapes.styles.size(), 2u);
    const auto *fillStyle = static_cast<const FillStyle *>(shapes.styles[0].get());
    EXPECT_EQ(fillStyle->blendMode, motion::BlendMode::Normal);
    const auto *strokeStyle =
        static_cast<const motion::StrokeStyle *>(shapes.styles[1].get());
    EXPECT_EQ(strokeStyle->blendMode, motion::BlendMode::Normal);
    EXPECT_EQ(strokeStyle->position, motion::StrokePosition::Center);
    EXPECT_EQ(strokeStyle->trimStart.staticValue(), 0.0f);
    EXPECT_EQ(strokeStyle->trimEnd.staticValue(), 1.0f);
    EXPECT_EQ(strokeStyle->trimOffset.staticValue(), 0.0f);
}

TEST(SerializerTest, LayerWithoutTrackMatteDefaultsToNone) {
    auto document = BuildRichDocument();
    auto json = nlohmann::json::parse(Serializer::serialize(*document));
    for (auto &layer : json["compositions"][1]["layers"]) {
        layer.erase("trackMatteType");
        layer.erase("trackMatteLayerId");
        for (auto &mask : layer["masks"]) {
            // Legacy mask path was a bare BezierPath object.
            if (mask.contains("path") && mask["path"].contains("static")) {
                mask["path"] = mask["path"]["static"];
            }
            mask.erase("feather");
            mask.erase("expansion");
        }
    }

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue()) << restored.error();
    const Layer &shapes = *(*restored)->compositions[1]->layers[0];
    EXPECT_EQ(shapes.trackMatteType, motion::TrackMatteType::None);
    EXPECT_FALSE(shapes.trackMatteLayerId.isValid());
    ASSERT_EQ(shapes.masks.size(), 1u);
    EXPECT_FALSE(shapes.masks[0].path.staticValue().vertices.empty());
    EXPECT_EQ(shapes.masks[0].feather.staticValue(), 0.0f);
    EXPECT_EQ(shapes.masks[0].expansion.staticValue(), 0.0f);
}

TEST(SerializerTest, ColorSerializesAsHexString) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->backgroundColor = Color{1, 0, 0.5f, 1};

    const auto json = nlohmann::json::parse(Serializer::serialize(document));
    EXPECT_EQ(json["compositions"][0]["backgroundColor"], "#FF0080FF");
}

TEST(SerializerTest, LegacyColorArrayStillAccepted) {
    auto document = BuildRichDocument();
    auto json = nlohmann::json::parse(Serializer::serialize(*document));
    json["compositions"][1]["backgroundColor"] = {0.2, 0.4, 0.6, 1.0};

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    EXPECT_EQ((*restored)->compositions[1]->backgroundColor,
              (Color{0.2f, 0.4f, 0.6f, 1.0f}));
}

TEST(SerializerTest, InvalidColorHexRejected) {
    auto document = BuildRichDocument();
    auto json = nlohmann::json::parse(Serializer::serialize(*document));
    json["compositions"][1]["backgroundColor"] = "#GGGGGGGG";

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(json.dump());
    EXPECT_FALSE(restored.hasValue());
}

TEST(SerializerTest, RestoredModelEvaluatesAndIndexes) {
    auto document = BuildRichDocument();
    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(Serializer::serialize(*document));
    ASSERT_TRUE(restored.hasValue());

    ASSERT_EQ((*restored)->compositions.size(), 2u);
    const Composition &main = *(*restored)->compositions[1];
    ASSERT_EQ(main.layers.size(), 8u);
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
    ASSERT_EQ(shapes.styles.size(), 2u);
    const auto *fillStyle = static_cast<const FillStyle *>(shapes.styles[0].get());
    EXPECT_TRUE(fillStyle->color.isAnimated());
    EXPECT_EQ(fillStyle->color.evaluate(10), (Color{0.2f, 0.4f, 0.6f, 1}));
    EXPECT_EQ(fillStyle->blendMode, motion::BlendMode::Overlay);
    ASSERT_EQ(shapes.styles.size(), 2u);
    const auto *strokeStyle =
        static_cast<const motion::StrokeStyle *>(shapes.styles[1].get());
    EXPECT_EQ(strokeStyle->width.staticValue(), 4.0f);
    EXPECT_EQ(strokeStyle->blendMode, motion::BlendMode::Multiply);
    EXPECT_EQ(strokeStyle->position, motion::StrokePosition::Inside);
    EXPECT_EQ(strokeStyle->trimStart.staticValue(), 0.25f);
    EXPECT_EQ(strokeStyle->trimEnd.staticValue(), 0.75f);
    EXPECT_TRUE(strokeStyle->trimOffset.isAnimated());
    EXPECT_EQ(strokeStyle->trimOffset.evaluate(20), 90.0f);

    // EntityIndex rebuilt: original IDs resolve correctly.
    EXPECT_NE((*restored)->entityIndex().findLayer(shapes.id), nullptr);
    const auto *originalContent =
        static_cast<const ShapeContent *>(originalShapes.content.get());
    ASSERT_NE(originalContent->geometry, nullptr);
    EXPECT_NE((*restored)->entityIndex().findShape(originalContent->geometry->id), nullptr);

    // Parent-child relationship preserved (null group at index 4, text at 5).
    const Layer &textLayer = *main.layers[5];
    EXPECT_EQ(textLayer.parentId, main.layers[4]->id);
}

TEST(SerializerTest, AnimatableJsonShape) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
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
    EXPECT_EQ(transform["position"]["keyframes"][0]["easing"], "linear");
    EXPECT_FALSE(transform["position"].contains("static"));
}

TEST(SerializerTest, EasingSerializesAsString) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));

    Keyframe<Vec2> presetKeyframe;
    presetKeyframe.time = 5;
    presetKeyframe.value = Vec2{1, 2};
    presetKeyframe.easing = Easing::EaseInOut();
    layer->transform.position.addKeyframe(presetKeyframe);

    Keyframe<float> customKeyframe;
    customKeyframe.time = 8;
    customKeyframe.value = 12;
    customKeyframe.easing = Easing::Bezier(0.25f, 0.5f, 0.75f, 1.0f);
    layer->transform.rotation.addKeyframe(customKeyframe);

    const auto json = nlohmann::json::parse(Serializer::serialize(document));
    const auto &transform = json["compositions"][0]["layers"][0]["transform"];
    EXPECT_EQ(transform["position"]["keyframes"][0]["easing"], "ease-in-out");
    EXPECT_EQ(transform["rotation"]["keyframes"][0]["easing"],
              "cubic-bezier(0.25,0.5,0.75,1)");

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(Serializer::serialize(document));
    ASSERT_TRUE(restored.hasValue());
    const auto &restoredLayer = *(*restored)->compositions[0]->layers[0];
    EXPECT_EQ(restoredLayer.transform.position.keyframes()[0].easing,
              Easing::EaseInOut());
    EXPECT_EQ(restoredLayer.transform.rotation.keyframes()[0].easing,
              Easing::Bezier(0.25f, 0.5f, 0.75f, 1.0f));
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

TEST(SerializerTest, LayerWithoutFollowPathDefaults) {
    auto document = BuildRichDocument();
    auto json = nlohmann::json::parse(Serializer::serialize(*document));
    for (auto &layer : json["compositions"][1]["layers"]) {
        layer.erase("followPath");
    }

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue()) << restored.error();
    const Layer &shapes = *(*restored)->compositions[1]->layers[0];
    EXPECT_FALSE(shapes.followPath.enabled);
    EXPECT_FALSE(shapes.followPath.pathLayerId.isValid());
    EXPECT_FLOAT_EQ(shapes.followPath.pathOffset.staticValue(), 0.0f);
    EXPECT_TRUE(shapes.followPath.orientAlongPath);
}

TEST(SerializerTest, FollowPathRoundTrip) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    Layer *pathLayer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *follower = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    follower->followPath.enabled = true;
    follower->followPath.pathLayerId = pathLayer->id;
    follower->followPath.orientAlongPath = false;
    Keyframe<float> offsetKeyframe;
    offsetKeyframe.time = 0;
    offsetKeyframe.value = 0.0f;
    Keyframe<float> offsetKeyframeEnd;
    offsetKeyframeEnd.time = 20;
    offsetKeyframeEnd.value = 1.0f;
    follower->followPath.pathOffset.addKeyframe(offsetKeyframe);
    follower->followPath.pathOffset.addKeyframe(offsetKeyframeEnd);
    original.refreshEntityIndex();

    const std::string text = Serializer::serialize(original);
    Expected<std::unique_ptr<Document>, std::string> loaded = Serializer::deserialize(text);
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    Layer *roundTrip = (*loaded)->compositions[0]->layers[1].get();
    EXPECT_TRUE(roundTrip->followPath.enabled);
    EXPECT_EQ(roundTrip->followPath.pathLayerId, pathLayer->id);
    EXPECT_FALSE(roundTrip->followPath.orientAlongPath);
    ASSERT_TRUE(roundTrip->followPath.pathOffset.isAnimated());
    ASSERT_EQ(roundTrip->followPath.pathOffset.keyframes().size(), 2u);
    EXPECT_FLOAT_EQ(roundTrip->followPath.pathOffset.keyframes()[1].value, 1.0f);
}

TEST(SerializerTextPathTest, RoundTrip) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    Layer *pathLayer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *textLayer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    auto *textContent = static_cast<motion::TextContent *>(textLayer->content.get());
    textContent->textPath.enabled = true;
    textContent->textPath.pathLayerId = pathLayer->id;
    textContent->textPath.reversed = true;
    textContent->textPath.perpendicular = false;
    textContent->textPath.forceAlignment = true;
    Keyframe<float> marginStart;
    marginStart.time = 0;
    marginStart.value = 0.0f;
    Keyframe<float> marginEnd;
    marginEnd.time = 10;
    marginEnd.value = 12.0f;
    textContent->textPath.firstMargin.addKeyframe(marginStart);
    textContent->textPath.firstMargin.addKeyframe(marginEnd);
    textContent->textPath.lastMargin.setStaticValue(4.0f);
    original.refreshEntityIndex();

    const std::string text = Serializer::serialize(original);
    Expected<std::unique_ptr<Document>, std::string> loaded = Serializer::deserialize(text);
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    auto *roundTrip =
        static_cast<motion::TextContent *>((*loaded)->compositions[0]->layers[1]->content.get());
    EXPECT_TRUE(roundTrip->textPath.enabled);
    EXPECT_EQ(roundTrip->textPath.pathLayerId, pathLayer->id);
    EXPECT_TRUE(roundTrip->textPath.reversed);
    EXPECT_FALSE(roundTrip->textPath.perpendicular);
    EXPECT_TRUE(roundTrip->textPath.forceAlignment);
    ASSERT_TRUE(roundTrip->textPath.firstMargin.isAnimated());
    ASSERT_EQ(roundTrip->textPath.firstMargin.keyframes().size(), 2u);
    EXPECT_FLOAT_EQ(roundTrip->textPath.firstMargin.keyframes()[1].value, 12.0f);
    EXPECT_FLOAT_EQ(roundTrip->textPath.lastMargin.staticValue(), 4.0f);
}

TEST(SerializerTest, ShapePathKeyframeRoundTrip) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto path = std::make_unique<ShapePath>();

    VectorNetwork n0 = BezierPathToVectorNetwork(
        MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{5, 8}, {}, {}}}, true));
    VectorNetwork n1 = n0;
    n1.vertices[2].point = {5, 20};

    Keyframe<VectorNetwork> kf0;
    kf0.time = 0;
    kf0.value = n0;
    Keyframe<VectorNetwork> kf1;
    kf1.time = 30;
    kf1.value = n1;
    path->path.addKeyframe(kf0);
    path->path.addKeyframe(kf1);
    content->geometry = std::move(path);
    original.refreshEntityIndex();

    const std::string text = Serializer::serialize(original);
    Expected<std::unique_ptr<Document>, std::string> loaded = Serializer::deserialize(text);
    ASSERT_TRUE(loaded.hasValue());
    ASSERT_FALSE((*loaded)->compositions.empty());
    ASSERT_FALSE((*loaded)->compositions[0]->layers.empty());

    Layer *roundTrip = (*loaded)->compositions[0]->layers[0].get();
    ASSERT_NE(roundTrip, nullptr);
    auto *shapeContent = static_cast<ShapeContent *>(roundTrip->content.get());
    ASSERT_NE(shapeContent->geometry, nullptr);
    ASSERT_EQ(shapeContent->geometry->type(), ShapeType::Path);
    const auto &anim = static_cast<ShapePath *>(shapeContent->geometry.get())->path;
    ASSERT_TRUE(anim.isAnimated());
    ASSERT_EQ(anim.keyframes().size(), 2u);
    EXPECT_EQ(anim.keyframes()[0].time, 0);
    EXPECT_EQ(anim.keyframes()[1].time, 30);
    EXPECT_EQ(anim.keyframes()[0].value, n0);
    EXPECT_EQ(anim.keyframes()[1].value, n1);
}

TEST(SerializerTest, LoadsLegacyBezierPathAsVectorNetwork) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    composition->width = 100;
    composition->height = 100;
    composition->duration = 30;
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto path = std::make_unique<ShapePath>();
    path->path.setStaticValue(BezierPathToVectorNetwork(
        MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true)));
    content->geometry = std::move(path);
    original.refreshEntityIndex();

    nlohmann::json root = nlohmann::json::parse(Serializer::serialize(original));
    // Force legacy single-contour path JSON (no edges).
    root["compositions"][0]["layers"][0]["content"]["geometry"]["path"] = nlohmann::json::parse(R"({
      "static": {
        "closed": true,
        "vertices": [
          {"point": [0, 0], "inTangent": [0, 0], "outTangent": [0, 0]},
          {"point": [10, 0], "inTangent": [0, 0], "outTangent": [0, 0]},
          {"point": [0, 10], "inTangent": [0, 0], "outTangent": [0, 0]}
        ]
      }
    })");

    Expected<std::unique_ptr<Document>, std::string> loaded = Serializer::deserialize(root.dump());
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    auto *shapeContent =
        static_cast<ShapeContent *>((*loaded)->compositions[0]->layers[0]->content.get());
    ASSERT_EQ(shapeContent->geometry->type(), ShapeType::Path);
    const VectorNetwork &network =
        static_cast<ShapePath *>(shapeContent->geometry.get())->path.staticValue();
    EXPECT_EQ(network.vertices.size(), 3u);
    EXPECT_EQ(network.edges.size(), 3u);
    EXPECT_EQ(SchemaMigrator::currentVersion(), 1);
}

TEST(SerializerTest, RoundTripVectorNetworkSharedVertex) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    composition->duration = 30;
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto path = std::make_unique<ShapePath>();
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}, {3, {-10, 0}}};
    network.edges = {{1, 1, 2, {1, 0}, {-1, 0}}, {2, 1, 3, {}, {}}};
    path->path.setStaticValue(network);
    content->geometry = std::move(path);
    original.refreshEntityIndex();

    Expected<std::unique_ptr<Document>, std::string> loaded =
        Serializer::deserialize(Serializer::serialize(original));
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    auto *shapeContent =
        static_cast<ShapeContent *>((*loaded)->compositions[0]->layers[0]->content.get());
    const VectorNetwork &roundTrip =
        static_cast<ShapePath *>(shapeContent->geometry.get())->path.staticValue();
    EXPECT_EQ(roundTrip, network);

    nlohmann::json root = nlohmann::json::parse(Serializer::serialize(*loaded.value()));
    EXPECT_EQ(root["schemaVersion"], 1);
    EXPECT_TRUE(root["compositions"][0]["layers"][0]["content"]["geometry"]["path"]["static"].contains(
        "edges"));
}

TEST(SerializerTest, RoundTripVertexMirrorMode) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    composition->duration = 30;
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto path = std::make_unique<ShapePath>();
    VectorNetwork network;
    network.vertices = {{1, {0, 0}, VertexMirrorMode::AngleLength},
                        {2, {10, 0}, VertexMirrorMode::Angle}};
    network.edges = {{1, 1, 2, {}, {}}};
    path->path.setStaticValue(network);
    content->geometry = std::move(path);
    original.refreshEntityIndex();

    Expected<std::unique_ptr<Document>, std::string> loaded =
        Serializer::deserialize(Serializer::serialize(original));
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    auto *shapeContent =
        static_cast<ShapeContent *>((*loaded)->compositions[0]->layers[0]->content.get());
    const VectorNetwork &roundTrip =
        static_cast<ShapePath *>(shapeContent->geometry.get())->path.staticValue();
    ASSERT_EQ(roundTrip.vertices.size(), 2u);
    EXPECT_EQ(roundTrip.vertices[0].mirrorMode, VertexMirrorMode::AngleLength);
    EXPECT_EQ(roundTrip.vertices[1].mirrorMode, VertexMirrorMode::Angle);
}

TEST(SerializerTest, MissingMirrorModeDefaultsToNone) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    composition->duration = 30;
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto path = std::make_unique<ShapePath>();
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}};
    network.edges = {{1, 1, 2, {}, {}}};
    path->path.setStaticValue(network);
    content->geometry = std::move(path);
    original.refreshEntityIndex();

    nlohmann::json root = nlohmann::json::parse(Serializer::serialize(original));
    auto &vertex0 =
        root["compositions"][0]["layers"][0]["content"]["geometry"]["path"]["static"]["vertices"][0];
    vertex0.erase("mirrorMode");
    Expected<std::unique_ptr<Document>, std::string> loaded =
        Serializer::deserialize(root.dump());
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    auto *shapeContent =
        static_cast<ShapeContent *>((*loaded)->compositions[0]->layers[0]->content.get());
    const VectorNetwork &parsed =
        static_cast<ShapePath *>(shapeContent->geometry.get())->path.staticValue();
    ASSERT_FALSE(parsed.vertices.empty());
    EXPECT_EQ(parsed.vertices[0].mirrorMode, VertexMirrorMode::None);
}

TEST(SerializerTest, ImageLayerRoundTripPreservesContainerAndScaleMode) {
    auto document = BuildRichDocument();
    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(Serializer::serialize(*document));
    ASSERT_TRUE(restored.hasValue());

    ASSERT_EQ((*restored)->assets.size(), 1u);
    EXPECT_EQ((*restored)->assets[0].width, 640);
    EXPECT_EQ((*restored)->assets[0].height, 480);

    Layer *imageLayer = nullptr;
    for (auto &layer : (*restored)->compositions[1]->layers) {
        if (layer->type() == LayerType::Image) {
            imageLayer = layer.get();
            break;
        }
    }
    ASSERT_NE(imageLayer, nullptr);
    auto *image = static_cast<motion::ImageContent *>(imageLayer->content.get());
    EXPECT_TRUE(image->assetId.isValid());
    EXPECT_EQ(image->assetId, (*restored)->assets[0].id);
    EXPECT_FALSE(image->size.isAnimated());
    EXPECT_FLOAT_EQ(image->size.staticValue().x, 320.0f);
    EXPECT_FLOAT_EQ(image->size.staticValue().y, 240.0f);
    EXPECT_EQ(image->scaleMode, motion::ImageScaleMode::Zoom);
}

TEST(SerializerTest, TextLayerRoundTripPreservesBoxFields) {
    auto document = BuildRichDocument();
    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(Serializer::serialize(*document));
    ASSERT_TRUE(restored.hasValue());

    Layer *textLayer = nullptr;
    for (auto &layer : (*restored)->compositions[1]->layers) {
        if (layer->type() == LayerType::Text) {
            textLayer = layer.get();
            break;
        }
    }
    ASSERT_NE(textLayer, nullptr);
    auto *text = static_cast<motion::TextContent *>(textLayer->content.get());
    EXPECT_EQ(text->text.staticValue(), "hello");
    EXPECT_EQ(text->fontFamily, "PingFang SC");
    EXPECT_EQ(text->fontStyle, "Regular");
    EXPECT_FLOAT_EQ(text->fontSize, 36.0f);
    EXPECT_FLOAT_EQ(text->size.x, 320.0f);
    EXPECT_FLOAT_EQ(text->size.y, 96.0f);
    EXPECT_TRUE(text->boxTextMode);
    EXPECT_EQ(text->align, motion::TextAlign::Center);
}

TEST(SerializerTest, TextLayerMissingFieldsUseDefaults) {
    auto document = BuildRichDocument();
    auto json = nlohmann::json::parse(Serializer::serialize(*document));
    for (auto &layer : json["compositions"][1]["layers"]) {
        if (layer["type"] == "text") {
            layer["content"].erase("size");
            layer["content"].erase("boxTextMode");
            layer["content"].erase("align");
            layer["content"].erase("fontStyle");
        }
    }

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue()) << restored.error();

    Layer *textLayer = nullptr;
    for (auto &layer : (*restored)->compositions[1]->layers) {
        if (layer->type() == LayerType::Text) {
            textLayer = layer.get();
            break;
        }
    }
    ASSERT_NE(textLayer, nullptr);
    auto *text = static_cast<motion::TextContent *>(textLayer->content.get());
    EXPECT_FLOAT_EQ(text->size.x, 400.0f);
    EXPECT_FLOAT_EQ(text->size.y, 120.0f);
    EXPECT_FALSE(text->boxTextMode);
    EXPECT_EQ(text->align, motion::TextAlign::Left);
    EXPECT_EQ(text->fontStyle, "");
}

TEST(SerializerTest, UnboundImageAssetIdSerializesAsNull) {
    auto document = std::make_unique<Document>();
    auto composition = std::make_unique<Composition>();
    composition->duration = 30;
    composition->width = 100;
    composition->height = 100;
    composition->layers.push_back(std::make_unique<Layer>(LayerType::Image));
    document->compositions.push_back(std::move(composition));
    document->refreshEntityIndex();

    const std::string jsonText = Serializer::serialize(*document);
    auto json = nlohmann::json::parse(jsonText);
    ASSERT_TRUE(json["compositions"][0]["layers"][0]["content"]["assetId"].is_null());

    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(jsonText);
    ASSERT_TRUE(restored.hasValue()) << restored.error();
    auto *image = static_cast<motion::ImageContent *>(
        (*restored)->compositions[0]->layers[0]->content.get());
    EXPECT_FALSE(image->assetId.isValid());
}

TEST(SerializerTest, ImageLayerMissingFieldsUseDefaults) {
    auto document = BuildRichDocument();
    auto json = nlohmann::json::parse(Serializer::serialize(*document));
    json["assets"][0].erase("width");
    json["assets"][0].erase("height");
    for (auto &layer : json["compositions"][1]["layers"]) {
        if (layer["type"] == "image") {
            layer["content"].erase("assetId");
            layer["content"].erase("size");
            layer["content"].erase("scaleMode");
        }
    }

    Expected<std::unique_ptr<Document>, std::string> restored =
        Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    EXPECT_EQ((*restored)->assets[0].width, 0);
    EXPECT_EQ((*restored)->assets[0].height, 0);

    Layer *imageLayer = nullptr;
    for (auto &layer : (*restored)->compositions[1]->layers) {
        if (layer->type() == LayerType::Image) {
            imageLayer = layer.get();
            break;
        }
    }
    ASSERT_NE(imageLayer, nullptr);
    auto *image = static_cast<motion::ImageContent *>(imageLayer->content.get());
    EXPECT_FALSE(image->assetId.isValid());
    EXPECT_FLOAT_EQ(image->size.staticValue().x, 200.0f);
    EXPECT_FLOAT_EQ(image->size.staticValue().y, 200.0f);
    EXPECT_EQ(image->scaleMode, motion::ImageScaleMode::LetterBox);
}

TEST(SerializerTest, ShaderLibraryRoundTrip) {
    Document doc;
    ShaderDefinition shader;
    shader.id = motion::EntityId::Generate();
    shader.name = "Ripple";
    shader.mainImage = "vec4 mainImage(vec2 uv){ return vec4(1.0); }";
    shader.uniforms.push_back(ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1});
    doc.shaders.push_back(shader);

    const std::string json = Serializer::serializeShaders(doc);
    Expected<std::vector<ShaderDefinition>, std::string> loaded =
        Serializer::deserializeShaders(json);
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    ASSERT_EQ(loaded->size(), 1u);
    EXPECT_EQ((*loaded)[0].id, shader.id);
    EXPECT_EQ((*loaded)[0].name, shader.name);
    EXPECT_EQ((*loaded)[0].mainImage, shader.mainImage);
    ASSERT_EQ((*loaded)[0].uniforms.size(), 1u);
    EXPECT_EQ((*loaded)[0].uniforms[0].name, "rippleCount");
    EXPECT_EQ((*loaded)[0].uniforms[0].format, UniformFormat::Float);
    EXPECT_EQ((*loaded)[0].uniforms[0].count, 1);

    nlohmann::json root = nlohmann::json::parse(json);
    EXPECT_EQ(root["schemaVersion"], motion::dto::SHADER_SCHEMA_VERSION);
}

TEST(SerializerTest, DocumentShaderPaintRoundTrip) {
    Document original;
    ShaderDefinition shader;
    shader.name = "Ripple";
    shader.mainImage = "vec4 mainImage(vec2 uv){ return vec4(uv,0.0,1.0); }";
    shader.uniforms.push_back(ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1});
    original.shaders.push_back(shader);

    Composition *composition = original.addComposition(std::make_unique<Composition>());
    composition->duration = 30;
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto fill = std::make_unique<FillStyle>();
    ASSERT_TRUE(BindShaderPaint(*fill, original.shaders[0]).hasValue());
    fill->uniformValues.entries[0].floatValue.setStaticValue(5.0f);
    layer->styles.push_back(std::move(fill));
    original.refreshEntityIndex();

    const std::string documentJson = Serializer::serialize(original);
    const std::string shadersJson = Serializer::serializeShaders(original);

    nlohmann::json documentRoot = nlohmann::json::parse(documentJson);
    EXPECT_EQ(documentRoot["schemaVersion"], motion::dto::SCHEMA_VERSION);

    Expected<std::unique_ptr<Document>, std::string> loaded =
        Serializer::deserialize(documentJson);
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    Expected<std::vector<ShaderDefinition>, std::string> shaders =
        Serializer::deserializeShaders(shadersJson);
    ASSERT_TRUE(shaders.hasValue()) << shaders.error();
    (*loaded)->shaders = std::move(*shaders);

    Expected<void, std::string> valid = ValidateShaderReferences(**loaded);
    ASSERT_TRUE(valid.hasValue()) << valid.error();

    ASSERT_EQ((*loaded)->shaders.size(), 1u);
    EXPECT_EQ((*loaded)->shaders[0].id, original.shaders[0].id);
    ASSERT_FALSE((*loaded)->compositions.empty());
    ASSERT_FALSE((*loaded)->compositions[0]->layers.empty());
    ASSERT_EQ((*loaded)->compositions[0]->layers[0]->styles.size(), 1u);
    const auto *restoredFill =
        static_cast<const FillStyle *>((*loaded)->compositions[0]->layers[0]->styles[0].get());
    EXPECT_EQ(restoredFill->paintMode, StylePaintMode::Shader);
    EXPECT_EQ(restoredFill->shaderId, original.shaders[0].id);
    ASSERT_EQ(restoredFill->uniformValues.entries.size(), 1u);
    EXPECT_FLOAT_EQ(restoredFill->uniformValues.entries[0].floatValue.staticValue(), 5.0f);

    nlohmann::json styleJson = documentRoot["compositions"][0]["layers"][0]["styles"][0];
    EXPECT_EQ(styleJson["paintMode"], "shader");
    EXPECT_TRUE(styleJson.contains("shaderId"));
    EXPECT_TRUE(styleJson.contains("uniformValues"));
}

TEST(SerializerTest, MissingPaintModeDefaultsToColor) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    composition->duration = 30;
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1.0f, 0.0f, 0.0f, 1.0f});
    layer->styles.push_back(std::move(fill));
    original.refreshEntityIndex();

    nlohmann::json root = nlohmann::json::parse(Serializer::serialize(original));
    EXPECT_EQ(root["schemaVersion"], 1);
    auto &style = root["compositions"][0]["layers"][0]["styles"][0];
    style.erase("paintMode");
    style.erase("shaderId");
    style.erase("uniformValues");

    Expected<std::unique_ptr<Document>, std::string> loaded =
        Serializer::deserialize(root.dump());
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    ASSERT_EQ((*loaded)->compositions[0]->layers[0]->styles.size(), 1u);
    const auto *restoredFill =
        static_cast<const FillStyle *>((*loaded)->compositions[0]->layers[0]->styles[0].get());
    EXPECT_EQ(restoredFill->paintMode, StylePaintMode::Color);
    EXPECT_FALSE(restoredFill->shaderId.isValid());
    EXPECT_TRUE(restoredFill->uniformValues.entries.empty());
}
