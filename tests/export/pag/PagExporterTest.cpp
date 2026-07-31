#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "FakeBitmapFrameSource.h"
#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "pag/file.h"

using motion::Asset;
using motion::AssetType;
using motion::BezierPath;
using motion::BlendMode;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FakeBitmapFrameSource;
using motion::FillStyle;
using motion::FrameTime;
using motion::ImageContent;
using motion::ImageScaleMode;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::Mask;
using motion::MaskMode;
using motion::PagExporter;
using motion::PagExportError;
using motion::PagExportOptions;
using motion::PrecompContent;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapePath;
using motion::ShapeRect;
using motion::StrokeStyle;
using motion::TextContent;
using motion::TrackMatteType;
using motion::Vec2;

namespace {

Document MakeEmptyDoc(int width, int height, FrameTime duration) {
    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = width;
    composition->height = height;
    composition->duration = duration;
    composition->frameRate = {30, 1};
    document.addComposition(std::move(composition));
    return document;
}

Composition *Primary(Document &document) {
    return document.compositions.front().get();
}

Layer *AddShapeRect(Document &document, Composition *composition, Vec2 position, Vec2 size) {
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "Rect";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(position);
    rect->size.setStaticValue(size);
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    content->geometry = std::move(rect);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1, 0, 0, 1});
    layer->styles.push_back(std::move(fill));
    return document.addLayer(composition->id, std::move(layer));
}

std::shared_ptr<pag::File> DecodeBytes(const std::vector<uint8_t> &bytes) {
    return pag::Codec::Decode(bytes.data(), static_cast<uint32_t>(bytes.size()), "");
}

}  // namespace

TEST(PagExporterTest, InvalidComposition) {
    Document document;
    PagExportOptions options;
    options.compositionId = EntityId::Generate();
    auto result = PagExporter::Export(document, options);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), PagExportError::InvalidComposition);
}

TEST(PagExporterTest, EmptyCompositionRoundTrip) {
    Document document = MakeEmptyDoc(200, 100, 60);
    Primary(document)->backgroundColor = Color{0.2f, 0.4f, 0.6f, 1.0f};
    PagExportOptions options;
    auto result = PagExporter::Export(document, options);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    EXPECT_FALSE(result.value().bytes.empty());

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->width(), 200);
    EXPECT_EQ(file->height(), 100);
    EXPECT_EQ(file->frameRate(), 30.0f);
    EXPECT_EQ(file->duration(), 60);
    EXPECT_EQ(file->backgroundColor().red, static_cast<uint8_t>(std::lround(0.2f * 255.0f)));
    EXPECT_EQ(file->backgroundColor().green, static_cast<uint8_t>(std::lround(0.4f * 255.0f)));
    EXPECT_EQ(file->backgroundColor().blue, static_cast<uint8_t>(std::lround(0.6f * 255.0f)));
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers.size(), 1u);
    EXPECT_EQ(vector->layers[0]->name, "CompositionBackground");
}

TEST(PagExporterTest, CompositionCornerRadiusAddsBackdrop) {
    Document document = MakeEmptyDoc(100, 80, 10);
    Composition *composition = Primary(document);
    composition->backgroundColor = Color{1.0f, 0.0f, 0.0f, 1.0f};
    composition->cornerRadius = 12.0f;
    AddShapeRect(document, composition, Vec2{10, 10}, Vec2{20, 20});

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *root = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(root->layers.size(), 1u);
    ASSERT_EQ(root->layers[0]->type(), pag::LayerType::PreCompose);
    auto *wrap = static_cast<pag::PreComposeLayer *>(root->layers[0]);
    ASSERT_FALSE(wrap->masks.empty());
    ASSERT_NE(wrap->composition, nullptr);
    auto *inner = static_cast<pag::VectorComposition *>(wrap->composition);
    ASSERT_GE(inner->layers.size(), 2u);
    // PAG File: index 0 = topmost; backdrop must be last so it stays under content.
    EXPECT_EQ(inner->layers.back()->type(), pag::LayerType::Shape);
    EXPECT_EQ(inner->layers.back()->name, "CompositionBackground");
    EXPECT_EQ(inner->layers.front()->name, "Rect");
    bool foundCornerWarning = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "CompositionCornerRadiusApproximated") {
            foundCornerWarning = true;
        }
    }
    EXPECT_TRUE(foundCornerWarning);
}

TEST(PagExporterTest, ShapeRectStaticTransform) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    layer->transform.position.setStaticValue(Vec2{10, 20});
    layer->transform.scale.setStaticValue(Vec2{1.5f, 1.5f});
    layer->transform.rotation.setStaticValue(15.0f);
    layer->transform.opacity.setStaticValue(0.5f);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(file->compositions.size(), 1u);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions[0]);
    ASSERT_EQ(vector->layers.size(), 2u);
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Shape);
    EXPECT_EQ(vector->layers.back()->name, "CompositionBackground");
    auto *shapeLayer = static_cast<pag::ShapeLayer *>(vector->layers[0]);
    ASSERT_FALSE(shapeLayer->contents.empty());
    EXPECT_FLOAT_EQ(shapeLayer->transform->position->value.x, 10.0f);
    EXPECT_FLOAT_EQ(shapeLayer->transform->position->value.y, 20.0f);
    EXPECT_FLOAT_EQ(shapeLayer->transform->rotation->value, 15.0f);
    EXPECT_EQ(shapeLayer->transform->opacity->value, 128);
}

TEST(PagExporterTest, PositionKeyframes) {
    Document document = MakeEmptyDoc(400, 300, 60);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{40, 40});

    Keyframe<Vec2> from;
    from.time = 0;
    from.value = Vec2{0, 0};
    from.easing = Easing::EaseInOut();
    Keyframe<Vec2> to;
    to.time = 30;
    to.value = Vec2{100, 50};
    layer->transform.position.addKeyframe(from);
    layer->transform.position.addKeyframe(to);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions[0]);
    auto *shapeLayer = static_cast<pag::ShapeLayer *>(vector->layers[0]);
    ASSERT_TRUE(shapeLayer->transform->position->animatable());
    auto *animated =
        static_cast<pag::AnimatableProperty<pag::Point> *>(shapeLayer->transform->position);
    ASSERT_EQ(animated->keyframes.size(), 1u);
    EXPECT_EQ(animated->keyframes[0]->startTime, 0);
    EXPECT_EQ(animated->keyframes[0]->endTime, 30);
    EXPECT_FLOAT_EQ(animated->keyframes[0]->startValue.x, 0.0f);
    EXPECT_FLOAT_EQ(animated->keyframes[0]->endValue.x, 100.0f);
}

TEST(PagExporterTest, GroupParent) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);

    auto group = std::make_unique<Layer>(LayerType::Group);
    group->name = "Group";
    group->inPoint = 0;
    group->outPoint = composition->duration;
    Layer *groupLayer = document.addLayer(composition->id, std::move(group));

    Layer *child = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{20, 20});
    ASSERT_TRUE(child->setParent(groupLayer->id, document));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions[0]);
    ASSERT_EQ(vector->layers.size(), 3u);
    // Top-first PAG order: child Shape above Null group; backdrop last.
    EXPECT_EQ(vector->layers[0]->type(), pag::LayerType::Shape);
    EXPECT_EQ(vector->layers[1]->type(), pag::LayerType::Null);
    EXPECT_EQ(vector->layers[0]->parent, vector->layers[1]);
    EXPECT_EQ(vector->layers.back()->name, "CompositionBackground");
}

TEST(PagExporterTest, FollowPathSkippedWhenFallbackDisabled) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *kept = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{20, 20});
    kept->name = "Kept";
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{20, 20});
    layer->name = "Follow";
    layer->followPath.enabled = true;

    PagExportOptions options;
    options.allowBitmapFallback = false;
    auto result = PagExporter::Export(document, options);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    bool foundSkip = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "UnsupportedFollowPath") {
            foundSkip = true;
        }
    }
    EXPECT_TRUE(foundSkip);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers.size(), 2u);
    EXPECT_EQ(vector->layers[0]->name, "Kept");
    EXPECT_EQ(vector->layers.back()->name, "CompositionBackground");
}

TEST(PagExporterTest, FollowPathSkippedWithoutFrameSource) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{20, 20});
    layer->followPath.enabled = true;

    PagExportOptions options;
    options.allowBitmapFallback = true;
    auto result = PagExporter::Export(document, options, nullptr);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    bool found = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "BitmapFallbackUnavailable") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PagExporterTest, FollowPathBitmapFallback) {
    Document document = MakeEmptyDoc(40, 30, 3);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{20, 20});
    layer->followPath.enabled = true;

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapFallback = true;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());

    bool foundFollowPathWarning = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "UnsupportedFollowPath") {
            foundFollowPathWarning = true;
        }
    }
    EXPECT_TRUE(foundFollowPathWarning);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    bool foundBitmap = false;
    for (pag::Composition *comp : file->compositions) {
        if (comp->type() == pag::CompositionType::Bitmap) {
            foundBitmap = true;
        }
    }
    EXPECT_TRUE(foundBitmap);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(main->layers.size(), 2u);
    EXPECT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    EXPECT_EQ(main->layers.back()->name, "CompositionBackground");
}

TEST(PagExporterTest, GroupFollowPathRasterizesSubtree) {
    Document document = MakeEmptyDoc(40, 30, 2);
    Composition *composition = Primary(document);

    auto group = std::make_unique<Layer>(LayerType::Group);
    group->name = "Group";
    group->inPoint = 0;
    group->outPoint = composition->duration;
    group->followPath.enabled = true;
    Layer *groupLayer = document.addLayer(composition->id, std::move(group));

    Layer *child = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{10, 10});
    ASSERT_TRUE(child->setParent(groupLayer->id, document));

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());

    bool foundGroupWarning = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "GroupSubtreeRasterized") {
            foundGroupWarning = true;
        }
    }
    EXPECT_TRUE(foundGroupWarning);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(main->layers.size(), 2u);
    EXPECT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    EXPECT_EQ(main->layers.back()->name, "CompositionBackground");
}

TEST(PagExporterTest, TextLayerExports) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Text);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->text.setStaticValue("Hello PAG");
    content->fontFamily = "PingFang SC";
    content->fontSize.setStaticValue(36.0f);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.0f, 210.0f / 255.0f, 186.0f / 255.0f, 1.0f});
    layer->styles.push_back(std::move(fill));
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 0, 0, 1});
    stroke->width.setStaticValue(2.0f);
    layer->styles.push_back(std::move(stroke));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers.size(), 2u);
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Text);
    auto *textLayer = static_cast<pag::TextLayer *>(vector->layers[0]);
    ASSERT_NE(textLayer->sourceText, nullptr);
    EXPECT_EQ(textLayer->sourceText->value->text, "Hello PAG");
    EXPECT_FLOAT_EQ(textLayer->sourceText->value->fontSize, 36.0f);
    EXPECT_TRUE(textLayer->sourceText->value->applyFill);
    EXPECT_EQ(textLayer->sourceText->value->fillColor.red, 0);
    EXPECT_EQ(textLayer->sourceText->value->fillColor.green, 210);
    EXPECT_EQ(textLayer->sourceText->value->fillColor.blue, 186);
    EXPECT_TRUE(textLayer->sourceText->value->applyStroke);
    EXPECT_EQ(textLayer->sourceText->value->strokeColor.red, 0);
    EXPECT_EQ(textLayer->sourceText->value->strokeColor.green, 0);
    EXPECT_EQ(textLayer->sourceText->value->strokeColor.blue, 0);
    EXPECT_FLOAT_EQ(textLayer->sourceText->value->strokeWidth, 2.0f);
    EXPECT_TRUE(textLayer->sourceText->value->strokeOverFill);
}

TEST(PagExporterTest, ImageLayerExports) {
    // Valid 1x1 RGB PNG (red).
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
        0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    const std::string dir = "/tmp/motionstudio_pag_export_test";
    std::error_code error;
    std::filesystem::create_directories(dir + "/assets", error);
    ASSERT_FALSE(error) << error.message();
    const std::string relative = "assets/pixel.png";
    {
        std::ofstream output(dir + "/" + relative, std::ios::binary);
        ASSERT_TRUE(output);
        output.write(reinterpret_cast<const char *>(kPng), sizeof(kPng));
    }

    Document document = MakeEmptyDoc(400, 300, 30);
    document.projectRoot = dir;
    Asset asset;
    asset.type = AssetType::Image;
    asset.path = relative;
    asset.width = 1;
    asset.height = 1;
    document.assets.push_back(asset);

    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Image);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.anchorPoint.setStaticValue(Vec2{50, 50});
    layer->transform.scale.setStaticValue(Vec2{1, 1});
    auto *content = static_cast<ImageContent *>(layer->content.get());
    content->assetId = asset.id;
    content->size.setStaticValue(Vec2{100, 100});
    content->scaleMode = ImageScaleMode::LetterBox;
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(file->images.size(), 1u);
    EXPECT_EQ(file->images[0]->width, 1);
    EXPECT_EQ(file->images[0]->height, 1);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Image);
    auto *imageLayer = static_cast<pag::ImageLayer *>(vector->layers[0]);
    // 1x1 source into 100x100 LetterBox → fit scale 100 baked into transform.
    ASSERT_NE(imageLayer->transform, nullptr);
    ASSERT_NE(imageLayer->transform->scale, nullptr);
    EXPECT_FLOAT_EQ(imageLayer->transform->scale->value.x, 100.0f);
    EXPECT_FLOAT_EQ(imageLayer->transform->scale->value.y, 100.0f);
    EXPECT_FLOAT_EQ(imageLayer->transform->anchorPoint->value.x, 0.5f);
    EXPECT_FLOAT_EQ(imageLayer->transform->anchorPoint->value.y, 0.5f);
}

TEST(PagExporterTest, MaskExports) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 100});
    Mask mask;
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({Vec2{0, 0}, {}, {}});
    path.vertices.push_back({Vec2{50, 0}, {}, {}});
    path.vertices.push_back({Vec2{50, 50}, {}, {}});
    mask.path.setStaticValue(path);
    mask.mode = MaskMode::Add;
    layer->masks.push_back(mask);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers[0]->masks.size(), 1u);
    EXPECT_EQ(vector->layers[0]->masks[0]->maskMode, pag::MaskMode::Add);
}

TEST(PagExporterTest, TrackMatteExports) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *matte = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{80, 80});
    matte->name = "Matte";
    Layer *target = AddShapeRect(document, composition, Vec2{10, 10}, Vec2{80, 80});
    target->name = "Target";
    target->trackMatteType = TrackMatteType::Alpha;
    target->trackMatteLayerId = matte->id;

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers.size(), 3u);
    EXPECT_EQ(vector->layers[1]->trackMatteType, pag::TrackMatteType::Alpha);
    EXPECT_EQ(vector->layers[1]->trackMatteLayer, vector->layers[0]);
    EXPECT_FALSE(vector->layers[0]->isActive);
    EXPECT_TRUE(vector->layers[1]->isActive);
    EXPECT_EQ(vector->layers.back()->name, "CompositionBackground");
}

TEST(PagExporterTest, PrecompExports) {
    Document document;
    auto nested = std::make_unique<Composition>();
    nested->width = 100;
    nested->height = 80;
    nested->duration = 20;
    nested->frameRate = {30, 1};
    Composition *nestedPtr = document.addComposition(std::move(nested));
    AddShapeRect(document, nestedPtr, Vec2{0, 0}, Vec2{40, 40});

    auto root = std::make_unique<Composition>();
    root->width = 400;
    root->height = 300;
    root->duration = 30;
    root->frameRate = {30, 1};
    Composition *rootPtr = document.addComposition(std::move(root));

    auto layer = std::make_unique<Layer>(LayerType::Precomp);
    layer->inPoint = 0;
    layer->outPoint = rootPtr->duration;
    layer->startTime = 5;
    layer->timeStretch = 1.0;
    static_cast<PrecompContent *>(layer->content.get())->compositionId = nestedPtr->id;
    document.addLayer(rootPtr->id, std::move(layer));

    PagExportOptions options;
    options.compositionId = rootPtr->id;
    auto result = PagExporter::Export(document, options);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(file->compositions.size(), 2u);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(main->layers.size(), 2u);
    ASSERT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    EXPECT_EQ(main->layers.back()->name, "CompositionBackground");
    auto *precomp = static_cast<pag::PreComposeLayer *>(main->layers[0]);
    ASSERT_NE(precomp->composition, nullptr);
    EXPECT_EQ(precomp->compositionStartTime, 5);
}

TEST(PagExporterTest, MissingImageAssetSkipped) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Image);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    bool foundSkip = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "LayerSkipped" || warning.code == "ImageAssetMissing") {
            foundSkip = true;
        }
    }
    EXPECT_TRUE(foundSkip);
}

TEST(PagExporterTest, ShapePath) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto path = std::make_unique<ShapePath>();
    motion::BezierPath bezier;
    bezier.closed = true;
    bezier.vertices.push_back({Vec2{0, 0}, {}, {}});
    bezier.vertices.push_back({Vec2{40, 0}, {}, {}});
    bezier.vertices.push_back({Vec2{40, 40}, {}, {}});
    path->path.setStaticValue(bezier);
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0, 0, 1, 1});
    layer->styles.push_back(std::move(fill));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    ASSERT_NE(DecodeBytes(result.value().bytes), nullptr);
}

TEST(PagExporterTest, EllipseAndStroke) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto ellipse = std::make_unique<ShapeEllipse>();
    ellipse->position.setStaticValue(Vec2{100, 100});
    ellipse->size.setStaticValue(Vec2{80, 60});
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(ellipse);
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 1, 0, 1});
    stroke->width.setStaticValue(4.0f);
    layer->styles.push_back(std::move(stroke));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error());
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
}
