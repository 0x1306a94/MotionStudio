#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "FakeBitmapFrameSource.h"
#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/NullContent.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/StrokeMode.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "pag/file.h"

using motion::Animatable;
using motion::Asset;
using motion::AssetType;
using motion::BezierPath;
using motion::BezierPathToVectorNetwork;
using motion::BlendMode;
using motion::BrightnessContrastEffect;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::DropShadowStyle;
using motion::Easing;
using motion::EntityId;
using motion::FakeBitmapFrameSource;
using motion::FillStyle;
using motion::FrameTime;
using motion::GaussianBlurEffect;
using motion::GradientStop;
using motion::GradientType;
using motion::ImageContent;
using motion::ImageScaleMode;
using motion::Keyframe;
using motion::Layer;
using motion::LayerStrokeStyle;
using motion::LayerType;
using motion::MakeSingleContour;
using motion::Mask;
using motion::MaskMode;
using motion::NullContent;
using motion::OuterGlowStyle;
using motion::PagBmpSequenceType;
using motion::PagExporter;
using motion::PagExportError;
using motion::PagExportErrorKind;
using motion::PagExportOptions;
using motion::PrecompContent;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapePath;
using motion::ShapeRect;
using motion::StrokeMode;
using motion::StrokePosition;
using motion::StrokeStyle;
using motion::StylePaintMode;
using motion::TextContent;
using motion::TrackMatteType;
using motion::Vec2;
using motion::VectorNetwork;

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

void CountPathVerbs(const pag::PathHandle &handle, size_t *curves, size_t *lines) {
    *curves = 0;
    *lines = 0;
    if (handle == nullptr) {
        return;
    }
    for (pag::PathDataVerb verb : handle->verbs) {
        if (verb == pag::PathDataVerb::CurveTo) {
            ++(*curves);
        } else if (verb == pag::PathDataVerb::LineTo) {
            ++(*lines);
        }
    }
}

float PathAabbArea(const pag::PathHandle &handle) {
    if (handle == nullptr || handle->points.empty()) {
        return 0;
    }
    float minX = handle->points[0].x;
    float maxX = minX;
    float minY = handle->points[0].y;
    float maxY = minY;
    for (const pag::Point &point : handle->points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    return std::max(0.0f, maxX - minX) * std::max(0.0f, maxY - minY);
}

}  // namespace

TEST(PagExporterTest, InvalidCompositionHasStructuredError) {
    Document document;
    PagExportOptions options;
    options.compositionId = EntityId::Generate();
    auto result = PagExporter::Export(document, options);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().kind, PagExportErrorKind::InvalidComposition);
    EXPECT_FALSE(result.error().message.empty());
}

TEST(PagExporterTest, EmptyCompositionRoundTrip) {
    Document document = MakeEmptyDoc(200, 100, 60);
    Primary(document)->backgroundColor = Color{0.2f, 0.4f, 0.6f, 1.0f};
    PagExportOptions options;
    auto result = PagExporter::Export(document, options);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
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
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
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

TEST(PagExporterTest, FillColorAlphaMapsToOpacity) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 50});
    ASSERT_FALSE(layer->styles.empty());
    auto *fill = static_cast<FillStyle *>(layer->styles[0].get());
    // #E4F5FC1A → alpha 0x1A / 255
    fill->color.setStaticValue(Color{228.0f / 255.0f, 245.0f / 255.0f, 252.0f / 255.0f, 26.0f / 255.0f});

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Solid);
    auto *solid = static_cast<pag::SolidLayer *>(vector->layers[0]);
    EXPECT_EQ(solid->solidColor.red, 228);
    EXPECT_EQ(solid->solidColor.green, 245);
    EXPECT_EQ(solid->solidColor.blue, 252);
    EXPECT_EQ(solid->transform->opacity->value, 26);
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
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(file->compositions.size(), 1u);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions[0]);
    ASSERT_EQ(vector->layers.size(), 2u);
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Solid);
    EXPECT_EQ(vector->layers.back()->name, "CompositionBackground");
    pag::Layer *pagLayer = vector->layers[0];
    EXPECT_FLOAT_EQ(pagLayer->transform->position->value.x, 10.0f);
    EXPECT_FLOAT_EQ(pagLayer->transform->position->value.y, 20.0f);
    EXPECT_FLOAT_EQ(pagLayer->transform->rotation->value, 15.0f);
    EXPECT_EQ(pagLayer->transform->opacity->value, 128);
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
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions[0]);
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_TRUE(pagLayer->transform->position->animatable());
    auto *animated =
        static_cast<pag::AnimatableProperty<pag::Point> *>(pagLayer->transform->position);
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
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions[0]);
    ASSERT_EQ(vector->layers.size(), 3u);
    // Top-first PAG order: child Solid above Null group; backdrop last.
    EXPECT_EQ(vector->layers[0]->type(), pag::LayerType::Solid);
    EXPECT_EQ(vector->layers[1]->type(), pag::LayerType::Null);
    EXPECT_EQ(vector->layers[0]->parent, vector->layers[1]);
    EXPECT_EQ(vector->layers.back()->name, "CompositionBackground");
}

TEST(PagExporterTest, FollowPathWithoutBmpFailsWithDetails) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Layer *layer = AddShapeRect(document, Primary(document), Vec2{0, 0}, Vec2{20, 20});
    layer->name = "Follow";
    layer->followPath.enabled = true;

    auto result = PagExporter::Export(document, {});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().kind, PagExportErrorKind::MappingFailed);
    EXPECT_EQ(result.error().code, "UnsupportedFollowPath");
    EXPECT_EQ(result.error().entityName, "Follow");
    EXPECT_NE(result.error().message.find("Follow"), std::string::npos);
    EXPECT_NE(result.error().message.find("_bmp"), std::string::npos);
}

TEST(PagExporterTest, FollowPathLayerBmpExportsBitmap) {
    Document document = MakeEmptyDoc(40, 30, 3);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{20, 20});
    layer->name = "Follow_bmp";
    layer->followPath.enabled = true;

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapExport = true;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;

    bool foundLayerBmpWarning = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "BitmapForcedByLayerName") {
            foundLayerBmpWarning = true;
        }
    }
    EXPECT_TRUE(foundLayerBmpWarning);

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

TEST(PagExporterTest, CompositionNameBmpExportsBitmap) {
    Document document = MakeEmptyDoc(40, 30, 2);
    Primary(document)->name = "Main_bmp";
    FakeBitmapFrameSource frameSource;
    auto result = PagExporter::Export(document, {}, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->compositions.back()->type(), pag::CompositionType::Bitmap);
    EXPECT_EQ(file->compositions.back()->duration, 2);
}

TEST(PagExporterTest, BitmapSequenceSkipsUnchangedFramePayload) {
    Document document = MakeEmptyDoc(40, 30, 3);
    Primary(document)->name = "Main_bmp";
    FakeBitmapFrameSource frameSource(255, 0, 0, 255);
    frameSource.setFrameColor(0, 255, 0, 0, 255);
    frameSource.setFrameColor(1, 255, 0, 0, 255);
    frameSource.setFrameColor(2, 0, 255, 0, 255);
    PagExportOptions options;
    options.bmpSequenceType = PagBmpSequenceType::Bitmap;
    options.bitmapKeyFrameInterval = 60;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *bitmap = static_cast<pag::BitmapComposition *>(file->compositions.back());
    ASSERT_FALSE(bitmap->sequences.empty());
    ASSERT_EQ(bitmap->sequences[0]->frames.size(), 3u);
    EXPECT_TRUE(bitmap->sequences[0]->frames[0]->isKeyframe);
    EXPECT_FALSE(bitmap->sequences[0]->frames[0]->bitmaps.empty());
    EXPECT_FALSE(bitmap->sequences[0]->frames[1]->isKeyframe);
    EXPECT_TRUE(bitmap->sequences[0]->frames[1]->bitmaps.empty());
    EXPECT_TRUE(bitmap->sequences[0]->frames[2]->isKeyframe);
    EXPECT_FALSE(bitmap->sequences[0]->frames[2]->bitmaps.empty());
}

TEST(PagExporterTest, BitmapMaxResolutionCapsShortSide) {
    Document document = MakeEmptyDoc(2000, 1000, 1);
    Primary(document)->name = "Huge_bmp";
    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.bitmapScale = 1.0f;
    options.bitmapMaxResolution = 720;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *bitmap = static_cast<pag::BitmapComposition *>(file->compositions.back());
    ASSERT_FALSE(bitmap->sequences.empty());
    const int shortSide = std::min(bitmap->sequences[0]->width, bitmap->sequences[0]->height);
    EXPECT_LE(shortSide, 720);
}

TEST(PagExporterTest, LayerBmpMaxResolutionScalesPrecomposeToHost) {
    // 1920x1080 host + maxResolution 720 → bitmap 1280x720; PreCompose must scale 1.5×.
    Document document = MakeEmptyDoc(1920, 1080, 2);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 100});
    layer->name = "Rect_bmp";

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapExport = true;
    options.bitmapScale = 1.0f;
    options.bitmapMaxResolution = 720;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    EXPECT_EQ(main->width, 1920);
    EXPECT_EQ(main->height, 1080);
    ASSERT_GE(main->layers.size(), 1u);
    ASSERT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    auto *precomp = static_cast<pag::PreComposeLayer *>(main->layers[0]);
    ASSERT_NE(precomp->composition, nullptr);
    EXPECT_EQ(precomp->composition->type(), pag::CompositionType::Bitmap);
    EXPECT_EQ(precomp->composition->width, 1280);
    EXPECT_EQ(precomp->composition->height, 720);
    ASSERT_NE(precomp->transform, nullptr);
    ASSERT_NE(precomp->transform->scale, nullptr);
    EXPECT_FLOAT_EQ(precomp->transform->scale->value.x, 1.5f);
    EXPECT_FLOAT_EQ(precomp->transform->scale->value.y, 1.5f);
}

TEST(PagExporterTest, AllowBitmapExportFalseWithBmpFails) {
    Document document = MakeEmptyDoc(40, 30, 2);
    Primary(document)->name = "Main_bmp";
    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapExport = false;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().kind, PagExportErrorKind::MappingFailed);
    EXPECT_NE(result.error().message.find("allowBitmapExport"), std::string::npos);
}

TEST(PagExporterTest, PrecompLayerBmpForcesNestedBitmap) {
    Document document;
    auto nested = std::make_unique<Composition>();
    nested->name = "Nested";
    nested->width = 40;
    nested->height = 30;
    nested->duration = 2;
    nested->frameRate = {30, 1};
    Composition *nestedPtr = document.addComposition(std::move(nested));
    AddShapeRect(document, nestedPtr, Vec2{0, 0}, Vec2{10, 10});

    auto root = std::make_unique<Composition>();
    root->name = "Root";
    root->width = 40;
    root->height = 30;
    root->duration = 2;
    root->frameRate = {30, 1};
    Composition *rootPtr = document.addComposition(std::move(root));

    auto layer = std::make_unique<Layer>(LayerType::Precomp);
    layer->name = "Nested_bmp";
    layer->inPoint = 0;
    layer->outPoint = rootPtr->duration;
    static_cast<PrecompContent *>(layer->content.get())->compositionId = nestedPtr->id;
    document.addLayer(rootPtr->id, std::move(layer));

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.compositionId = rootPtr->id;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    bool foundNestedBitmap = false;
    for (pag::Composition *comp : file->compositions) {
        if (comp != file->compositions.back() && comp->type() == pag::CompositionType::Bitmap) {
            foundNestedBitmap = true;
        }
    }
    EXPECT_TRUE(foundNestedBitmap);
}

TEST(PagExporterTest, GroupBmpRasterizesSubtree) {
    Document document = MakeEmptyDoc(40, 30, 2);
    Composition *composition = Primary(document);

    auto group = std::make_unique<Layer>(LayerType::Group);
    group->name = "Group_bmp";
    group->inPoint = 0;
    group->outPoint = composition->duration;
    group->followPath.enabled = true;
    Layer *groupLayer = document.addLayer(composition->id, std::move(group));

    Layer *child = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{10, 10});
    ASSERT_TRUE(child->setParent(groupLayer->id, document));

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;

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
    layer->transform.position.setStaticValue(Vec2{40.0f, 80.0f});
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->text.setStaticValue("Hello PAG");
    content->fontFamily = "PingFang SC";
    content->fontSize = 36.0f;
    content->size = Vec2{400.0f, 120.0f};
    content->boxTextMode = false;
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.0f, 210.0f / 255.0f, 186.0f / 255.0f, 1.0f});
    layer->styles.push_back(std::move(fill));
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 0, 0, 1});
    stroke->width.setStaticValue(2.0f);
    layer->styles.push_back(std::move(stroke));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers.size(), 2u);
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Text);
    auto *textLayer = static_cast<pag::TextLayer *>(vector->layers[0]);
    ASSERT_NE(textLayer->sourceText, nullptr);
    const auto &textDocument = *textLayer->sourceText->value;
    EXPECT_EQ(textDocument.text, "Hello PAG");
    EXPECT_FLOAT_EQ(textDocument.fontSize, 36.0f);
    // Default point text: boxTextMode false → PAG point text.
    EXPECT_FALSE(textDocument.boxText);
    EXPECT_FLOAT_EQ(textDocument.boxTextSize.x, 0.0f);
    EXPECT_FLOAT_EQ(textDocument.boxTextSize.y, 0.0f);
    EXPECT_FLOAT_EQ(textDocument.firstBaseLine, 0.0f);
    // Without textAscentResolver: MS top → PAG baseline via fontSize * 0.8 heuristic.
    EXPECT_FLOAT_EQ(textLayer->transform->position->value.x, 40.0f);
    EXPECT_FLOAT_EQ(textLayer->transform->position->value.y, 80.0f + 36.0f * 0.8f);
    EXPECT_TRUE(textDocument.applyFill);
    EXPECT_EQ(textDocument.fillColor.red, 0);
    EXPECT_EQ(textDocument.fillColor.green, 210);
    EXPECT_EQ(textDocument.fillColor.blue, 186);
    EXPECT_TRUE(textDocument.applyStroke);
    EXPECT_EQ(textDocument.strokeColor.red, 0);
    EXPECT_EQ(textDocument.strokeColor.green, 0);
    EXPECT_EQ(textDocument.strokeColor.blue, 0);
    EXPECT_FLOAT_EQ(textDocument.strokeWidth, 2.0f);
    EXPECT_TRUE(textDocument.strokeOverFill);
}

TEST(PagExporterTest, TextPathExportsPathOption) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);

    auto pathLayer = std::make_unique<Layer>(LayerType::Shape);
    pathLayer->name = "Path";
    pathLayer->inPoint = 0;
    pathLayer->outPoint = composition->duration;
    pathLayer->visible = false;
    auto ellipse = std::make_unique<ShapeEllipse>();
    ellipse->position.setStaticValue(Vec2{200.0f, 150.0f});
    ellipse->size.setStaticValue(Vec2{240.0f, 120.0f});
    auto *shapeContent = static_cast<ShapeContent *>(pathLayer->content.get());
    shapeContent->geometry = std::move(ellipse);
    Layer *path = document.addLayer(composition->id, std::move(pathLayer));

    auto textLayer = std::make_unique<Layer>(LayerType::Text);
    textLayer->name = "PathText";
    textLayer->inPoint = 0;
    textLayer->outPoint = composition->duration;
    textLayer->transform.position.setStaticValue(Vec2{40.0f, 80.0f});
    auto *content = static_cast<TextContent *>(textLayer->content.get());
    content->text.setStaticValue("On Path");
    content->fontFamily = "PingFang SC";
    content->fontSize = 28.0f;
    content->size = Vec2{300.0f, 100.0f};
    // Stored box mode must be ignored when textPath resolves.
    content->boxTextMode = true;
    content->textPath.enabled = true;
    content->textPath.pathLayerId = path->id;
    content->textPath.reversed = true;
    content->textPath.perpendicular = false;
    content->textPath.forceAlignment = true;
    content->textPath.firstMargin.setStaticValue(12.0f);
    content->textPath.lastMargin.setStaticValue(8.0f);
    document.addLayer(composition->id, std::move(textLayer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    for (const auto &warning : result.value().warnings) {
        EXPECT_NE(warning.code, "TextPathUnresolved");
    }
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::TextLayer *pagText = nullptr;
    for (pag::Layer *layer : vector->layers) {
        if (layer->type() == pag::LayerType::Text) {
            pagText = static_cast<pag::TextLayer *>(layer);
            break;
        }
    }
    ASSERT_NE(pagText, nullptr);
    ASSERT_NE(pagText->pathOption, nullptr);
    ASSERT_NE(pagText->pathOption->path, nullptr);
    ASSERT_NE(pagText->pathOption->path->maskPath, nullptr);
    ASSERT_NE(pagText->pathOption->reversedPath, nullptr);
    ASSERT_NE(pagText->pathOption->perpendicularToPath, nullptr);
    ASSERT_NE(pagText->pathOption->forceAlignment, nullptr);
    ASSERT_NE(pagText->pathOption->firstMargin, nullptr);
    ASSERT_NE(pagText->pathOption->lastMargin, nullptr);
    EXPECT_TRUE(pagText->pathOption->reversedPath->value);
    EXPECT_FALSE(pagText->pathOption->perpendicularToPath->value);
    EXPECT_TRUE(pagText->pathOption->forceAlignment->value);
    EXPECT_FLOAT_EQ(pagText->pathOption->firstMargin->value, 12.0f);
    EXPECT_FLOAT_EQ(pagText->pathOption->lastMargin->value, 8.0f);
    ASSERT_NE(pagText->sourceText, nullptr);
    ASSERT_NE(pagText->sourceText->value, nullptr);
    EXPECT_FALSE(pagText->sourceText->value->boxText);
    // Path is already in text-local space; do not apply the point-text top→baseline shift.
    EXPECT_FLOAT_EQ(pagText->transform->position->value.x, 40.0f);
    EXPECT_FLOAT_EQ(pagText->transform->position->value.y, 80.0f);
}

TEST(PagExporterTest, TextPathUnresolvedWarns) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto textLayer = std::make_unique<Layer>(LayerType::Text);
    textLayer->inPoint = 0;
    textLayer->outPoint = composition->duration;
    auto *content = static_cast<TextContent *>(textLayer->content.get());
    content->text.setStaticValue("Missing path");
    content->fontSize = 24.0f;
    content->boxTextMode = false;
    content->textPath.enabled = true;
    content->textPath.pathLayerId = EntityId{999999};
    document.addLayer(composition->id, std::move(textLayer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    bool sawUnresolved = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "TextPathUnresolved") {
            sawUnresolved = true;
        }
    }
    EXPECT_TRUE(sawUnresolved);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    auto *pagText = static_cast<pag::TextLayer *>(vector->layers[0]);
    EXPECT_EQ(pagText->pathOption, nullptr);
    // Unresolved path falls back to ordinary point text, including the ascent shift.
    EXPECT_FLOAT_EQ(pagText->transform->position->value.y, 24.0f * 0.8f);
}

TEST(PagExporterTest, TextPointUsesResolvedAscent) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Text);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.position.setStaticValue(Vec2{10.0f, 20.0f});
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->text.setStaticValue("Ascent");
    content->fontFamily = "PingFang SC";
    content->fontSize = 28.0f;
    content->boxTextMode = false;
    document.addLayer(composition->id, std::move(layer));

    PagExportOptions options;
    options.textAscentResolver = [](const std::string &, const std::string &, float fontSize) {
        return fontSize * 1.06f;
    };
    auto result = PagExporter::Export(document, options);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    auto *textLayer = static_cast<pag::TextLayer *>(vector->layers[0]);
    EXPECT_FLOAT_EQ(textLayer->transform->position->value.y, 20.0f + 28.0f * 1.06f);
}

TEST(PagExporterTest, TextBoxTextModeExportsBoxText) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Text);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->text.setStaticValue("Shrink");
    content->fontSize = 48.0f;
    content->size = Vec2{320.0f, 96.0f};
    content->boxTextMode = true;
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    for (const auto &warning : result.value().warnings) {
        EXPECT_NE(warning.code, "TextFeatureApproximated");
    }
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Text);
    auto *textLayer = static_cast<pag::TextLayer *>(vector->layers[0]);
    const auto &textDocument = *textLayer->sourceText->value;
    EXPECT_TRUE(textDocument.boxText);
    EXPECT_FLOAT_EQ(textDocument.boxTextSize.x, 320.0f);
    EXPECT_FLOAT_EQ(textDocument.boxTextSize.y, 96.0f);
    EXPECT_FLOAT_EQ(textDocument.firstBaseLine, 48.0f * 0.8f);
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
    content->size = Vec2{100, 100};
    content->scaleMode = ImageScaleMode::LetterBox;
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
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

TEST(PagExporterTest, ImageCornerRadiusAddsMask) {
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
        0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    const std::string dir = "/tmp/motionstudio_pag_export_image_radius_test";
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
    auto *content = static_cast<ImageContent *>(layer->content.get());
    content->assetId = asset.id;
    content->size = Vec2{100, 100};
    content->cornerRadius.setStaticValue(12.0f);
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Image);
    auto *imageLayer = static_cast<pag::ImageLayer *>(vector->layers[0]);
    EXPECT_FALSE(imageLayer->masks.empty());
}

TEST(PagExporterTest, ImageCornerRadiusAnimationBakedWarning) {
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
        0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    const std::string dir = "/tmp/motionstudio_pag_export_image_radius_kf_test";
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
    auto *content = static_cast<ImageContent *>(layer->content.get());
    content->assetId = asset.id;
    content->size = Vec2{100, 100};
    Keyframe<float> from;
    from.time = 0;
    from.value = 4.0f;
    Keyframe<float> to;
    to.time = 15;
    to.value = 20.0f;
    content->cornerRadius.addKeyframe(from);
    content->cornerRadius.addKeyframe(to);
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    bool found = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "ImageCornerRadiusAnimationBaked") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PagExporterTest, GroupCornerRadiusApproximatedWarning) {
    Document document = MakeEmptyDoc(200, 200, 30);
    Composition *composition = Primary(document);
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    group->inPoint = 0;
    group->outPoint = composition->duration;
    static_cast<NullContent *>(group->content.get())->cornerRadius.setStaticValue(10.0f);
    Layer *rect = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{80, 80});
    rect->parentId = group->id;
    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    bool found = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "GroupCornerRadiusApproximated") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PagExporterTest, ImageMaskRemappedToSourceSpace) {
    // MS masks live in container space; PAG ImageLayer masks live in source pixels.
    // 1x1 source into 100x100 LetterBox → fit 100; (50,50) must become (0.5, 0.5).
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
        0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    const std::string dir = "/tmp/motionstudio_pag_export_mask_test";
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
    auto *content = static_cast<ImageContent *>(layer->content.get());
    content->assetId = asset.id;
    content->size = Vec2{100, 100};
    content->scaleMode = ImageScaleMode::LetterBox;
    Mask mask;
    BezierPath path =
        MakeSingleContour({{Vec2{0, 0}, {}, {}}, {Vec2{50, 0}, {}, {}}, {Vec2{50, 50}, {}, {}}}, true);
    mask.path.setStaticValue(BezierPathToVectorNetwork(path));
    mask.mode = MaskMode::Add;
    layer->masks.push_back(mask);
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Image);
    ASSERT_EQ(vector->layers[0]->masks.size(), 1u);
    ASSERT_NE(vector->layers[0]->masks[0]->maskPath, nullptr);
    const pag::PathHandle handle = vector->layers[0]->masks[0]->maskPath->value;
    ASSERT_NE(handle, nullptr);
    ASSERT_FALSE(handle->points.empty());
    float maxX = handle->points[0].x;
    float maxY = handle->points[0].y;
    for (const pag::Point &point : handle->points) {
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
    EXPECT_NEAR(maxX, 0.5f, 0.001f);
    EXPECT_NEAR(maxY, 0.5f, 0.001f);
}

TEST(PagExporterTest, MaskExports) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 100});
    Mask mask;
    BezierPath path = MakeSingleContour({{Vec2{0, 0}, {}, {}}, {Vec2{50, 0}, {}, {}}, {Vec2{50, 50}, {}, {}}}, true);
    mask.path.setStaticValue(BezierPathToVectorNetwork(path));
    mask.mode = MaskMode::Add;
    layer->masks.push_back(mask);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
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
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
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

// Group matte sources keep children as siblings in the same PAG composition. Only setting the
// NullLayer inactive leaves Path/Image children drawing as a silhouette, while matte sampling
// of the empty NullLayer hides the target. Wrap the Group subtree into an export-only Precomp.
TEST(PagExporterTest, GroupTrackMatteSourceWrappedInPrecomp) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);

    auto matteGroup = std::make_unique<Layer>(LayerType::Group);
    matteGroup->name = "MatteGroup";
    matteGroup->inPoint = 0;
    matteGroup->outPoint = composition->duration;
    Layer *matteGroupLayer = document.addLayer(composition->id, std::move(matteGroup));

    Layer *matteChild = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{80, 80});
    matteChild->name = "MatteChild";
    ASSERT_TRUE(matteChild->setParent(matteGroupLayer->id, document));

    Layer *target = AddShapeRect(document, composition, Vec2{10, 10}, Vec2{80, 80});
    target->name = "Target";
    target->trackMatteType = TrackMatteType::Alpha;
    target->trackMatteLayerId = matteGroupLayer->id;

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    bool warned = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "GroupTrackMatteSourceWrapped") {
            warned = true;
            break;
        }
    }
    EXPECT_TRUE(warned);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_GE(vector->layers.size(), 3u);

    pag::Layer *matteLayer = nullptr;
    pag::Layer *targetLayer = nullptr;
    for (pag::Layer *layer : vector->layers) {
        if (layer->name == "Target") {
            targetLayer = layer;
        }
        if (layer->name == "MatteGroup") {
            matteLayer = layer;
        }
    }
    ASSERT_NE(targetLayer, nullptr);
    ASSERT_NE(matteLayer, nullptr);
    EXPECT_EQ(targetLayer->trackMatteType, pag::TrackMatteType::Alpha);
    EXPECT_EQ(targetLayer->trackMatteLayer, matteLayer);
    ASSERT_EQ(matteLayer->type(), pag::LayerType::PreCompose);
    EXPECT_FALSE(matteLayer->isActive);

    // Matte child must not remain a drawable sibling on the host composition.
    for (pag::Layer *layer : vector->layers) {
        EXPECT_NE(layer->name, "MatteChild");
    }

    auto *mattePrecomp = static_cast<pag::PreComposeLayer *>(matteLayer);
    ASSERT_NE(mattePrecomp->composition, nullptr);
    ASSERT_EQ(mattePrecomp->composition->type(), pag::CompositionType::Vector);
    auto *inner = static_cast<pag::VectorComposition *>(mattePrecomp->composition);
    bool foundChild = false;
    for (pag::Layer *innerLayer : inner->layers) {
        if (innerLayer->name == "MatteChild") {
            foundChild = true;
            break;
        }
    }
    EXPECT_TRUE(foundChild);
}

TEST(PagExporterTest, TrackMatteWrapsMultipleTrimStrokesInPrecomp) {
    // PAG decode only binds track matte to layers[index-1]. Multiple trim-stroke siblings
    // cannot all share one matte — wrap them in an export-only Precomp so the host alone
    // carries trackMatte (MS has no Precomp/container for this yet).
    Document document = MakeEmptyDoc(400, 300, 90);
    Composition *composition = Primary(document);

    Layer *matte = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{120, 80});
    matte->name = "Matte";

    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "PathDualTrim";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.position.setStaticValue(Vec2{200, 150});
    layer->trackMatteType = TrackMatteType::Alpha;
    layer->trackMatteLayerId = matte->id;

    auto path = std::make_unique<ShapePath>();
    BezierPath geometry =
        MakeSingleContour({{{-40, 0}, {}, {}}, {{40, 0}, {}, {}}}, false);
    path->path.setStaticValue(BezierPathToVectorNetwork(geometry));
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);

    auto black = std::make_unique<StrokeStyle>();
    black->color.setStaticValue(Color{0, 0, 0, 1});
    black->width.setStaticValue(20.0f);
    Keyframe<float> blackTrim0;
    blackTrim0.time = 0;
    blackTrim0.value = 0.0f;
    Keyframe<float> blackTrim1;
    blackTrim1.time = 60;
    blackTrim1.value = 1.0f;
    black->trimEnd.addKeyframe(blackTrim0);
    black->trimEnd.addKeyframe(blackTrim1);
    layer->styles.push_back(std::move(black));

    auto yellow = std::make_unique<StrokeStyle>();
    yellow->color.setStaticValue(Color{0.97f, 0.77f, 0.02f, 1});
    yellow->width.setStaticValue(10.0f);
    Keyframe<float> yellowTrim0;
    yellowTrim0.time = 0;
    yellowTrim0.value = 0.0f;
    Keyframe<float> yellowTrim1;
    yellowTrim1.time = 60;
    yellowTrim1.value = 1.0f;
    yellow->trimEnd.addKeyframe(yellowTrim0);
    yellow->trimEnd.addKeyframe(yellowTrim1);
    layer->styles.push_back(std::move(yellow));

    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << result.error().message;

    bool wrapped = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "StrokeSiblingsWrappedForTrackMatte") {
            wrapped = true;
            break;
        }
    }
    EXPECT_TRUE(wrapped);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);

    pag::PreComposeLayer *host = nullptr;
    pag::Layer *matteLayer = nullptr;
    auto *root = static_cast<pag::VectorComposition *>(file->compositions.back());
    for (pag::Layer *pagLayer : root->layers) {
        if (pagLayer->name == "Matte") {
            matteLayer = pagLayer;
        }
        if (pagLayer->name == "PathDualTrim" && pagLayer->type() == pag::LayerType::PreCompose) {
            host = static_cast<pag::PreComposeLayer *>(pagLayer);
        }
        // Must not leave bare stroke siblings in the root (they would steal / break matte).
        EXPECT_EQ(pagLayer->name.find("PathDualTrim / Stroke"), std::string::npos);
    }
    ASSERT_NE(host, nullptr);
    ASSERT_NE(matteLayer, nullptr);
    EXPECT_EQ(host->trackMatteType, pag::TrackMatteType::Alpha);
    EXPECT_EQ(host->trackMatteLayer, matteLayer);
    EXPECT_FALSE(matteLayer->isActive);
    ASSERT_NE(host->transform, nullptr);
    ASSERT_NE(host->transform->position, nullptr);
    // Host spatial is identity; layer position stays on inner stroke layers (nested comps
    // clip to [0,0,w,h], so negative local path coords need the layer position).
    EXPECT_FLOAT_EQ(host->transform->position->value.x, 0);
    EXPECT_FLOAT_EQ(host->transform->position->value.y, 0);

    ASSERT_NE(host->composition, nullptr);
    ASSERT_EQ(host->composition->type(), pag::CompositionType::Vector);
    auto *inner = static_cast<pag::VectorComposition *>(host->composition);
    // No fill on the MS layer → empty Path-only main is dropped; only the two trim strokes.
    ASSERT_EQ(inner->layers.size(), 2u);

    int strokeElementCount = 0;
    bool sawBlack = false;
    bool sawYellow = false;
    for (pag::Layer *innerLayer : inner->layers) {
        EXPECT_EQ(innerLayer->trackMatteType, pag::TrackMatteType::None);
        EXPECT_EQ(innerLayer->trackMatteLayer, nullptr);
        ASSERT_NE(innerLayer->transform, nullptr);
        ASSERT_NE(innerLayer->transform->position, nullptr);
        EXPECT_FLOAT_EQ(innerLayer->transform->position->value.x, 200);
        EXPECT_FLOAT_EQ(innerLayer->transform->position->value.y, 150);
        if (innerLayer->type() != pag::LayerType::Shape) {
            continue;
        }
        auto *shape = static_cast<pag::ShapeLayer *>(innerLayer);
        ASSERT_FALSE(shape->contents.empty());
        auto *group = static_cast<pag::ShapeGroupElement *>(shape->contents[0]);
        for (pag::ShapeElement *element : group->elements) {
            if (element->type() != pag::ShapeType::Stroke) {
                continue;
            }
            ++strokeElementCount;
            auto *stroke = static_cast<pag::StrokeElement *>(element);
            ASSERT_NE(stroke->color, nullptr);
            if (stroke->color->value.red == 0 && stroke->color->value.green == 0 &&
                stroke->color->value.blue == 0) {
                sawBlack = true;
            }
            if (stroke->color->value.red > 200 && stroke->color->value.green > 150) {
                sawYellow = true;
            }
        }
    }
    EXPECT_EQ(strokeElementCount, 2);
    EXPECT_TRUE(sawBlack);
    EXPECT_TRUE(sawYellow);
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
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
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

TEST(PagExporterTest, MissingImageAssetFailsWithDetails) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Image);
    layer->name = "Photo";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().kind, PagExportErrorKind::MappingFailed);
    EXPECT_EQ(result.error().code, "ImageAssetMissing");
    EXPECT_EQ(result.error().entityName, "Photo");
    EXPECT_NE(result.error().message.find("Photo"), std::string::npos);
}

TEST(PagExporterTest, ShapePath) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto path = std::make_unique<ShapePath>();
    motion::BezierPath bezier = MakeSingleContour({{Vec2{0, 0}, {}, {}}, {Vec2{40, 0}, {}, {}}, {Vec2{40, 40}, {}, {}}}, true);
    path->path.setStaticValue(BezierPathToVectorNetwork(bezier));
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0, 0, 1, 1});
    layer->styles.push_back(std::move(fill));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    ASSERT_NE(DecodeBytes(result.value().bytes), nullptr);
}

TEST(PagExporterTest, SharedVertexNetworkFlattened) {
    // Triangle fan (degree-3 hub) must export as compiled fill faces, not Network.
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto path = std::make_unique<ShapePath>();
    motion::VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {40, 0}}, {3, {0, 40}}, {4, {-40, 0}}};
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 1, 3, {}, {}},
        {3, 1, 4, {}, {}},
        {4, 2, 3, {}, {}},
        {5, 3, 4, {}, {}},
        {6, 4, 2, {}, {}},
    };
    path->path.setStaticValue(network);
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0, 0.5f, 1, 1});
    layer->styles.push_back(std::move(fill));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    ASSERT_NE(DecodeBytes(result.value().bytes), nullptr);
}

TEST(PagExporterTest, AnimatedPathExportsCubicMorphStableNotSampledFill) {
    // Path 19 class: same 4 vertices, changing handles. CompileFillFaces polylines
    // mis-align across keys; export must keep cubic stroke topology for PAG morph.
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "PathMorph";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;

    VectorNetwork kf0;
    kf0.vertices = {{1, {147.5f, 82.5f}}, {2, {236.5f, 174.5f}}, {3, {147.5f, 266.5f}}, {4, {58.5f, 174.5f}}};
    kf0.edges = {
        {1, 1, 2, {44.6667f, 0}, {-44.6667f, 0}},
        {2, 2, 3, {0, 44.6667f}, {0, -44.6667f}},
        {3, 3, 4, {-44.6667f, 0}, {44.6667f, 0}},
        {4, 4, 1, {0, -44.6667f}, {0, 44.6667f}},
    };
    VectorNetwork kf19 = kf0;
    kf19.edges = {
        {1, 1, 2, {62.8333f, -39.6667f}, {15.3333f, -60.1667f}},
        {2, 2, 3, {-39.6667f, 62.8333f}, {-60.1667f, 15.3333f}},
        {3, 3, 4, {-62.8333f, 39.6667f}, {-15.3333f, 60.1667f}},
        {4, 4, 1, {39.6667f, -62.8333f}, {60.1667f, -15.3333f}},
    };

    auto path = std::make_unique<ShapePath>();
    Keyframe<VectorNetwork> from;
    from.time = 0;
    from.value = kf0;
    Keyframe<VectorNetwork> to;
    to.time = 19;
    to.value = kf19;
    path->path.addKeyframe(from);
    path->path.addKeyframe(to);
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.97f, 0.23f, 0.86f, 1});
    layer->styles.push_back(std::move(fill));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::ShapeLayer *shapeLayer = nullptr;
    for (pag::Layer *candidate : vector->layers) {
        if (candidate->name == "PathMorph") {
            shapeLayer = static_cast<pag::ShapeLayer *>(candidate);
            break;
        }
    }
    ASSERT_NE(shapeLayer, nullptr);
    ASSERT_EQ(shapeLayer->contents.size(), 1u);
    auto *group = static_cast<pag::ShapeGroupElement *>(shapeLayer->contents[0]);
    ASSERT_FALSE(group->elements.empty());
    ASSERT_EQ(group->elements[0]->type(), pag::ShapeType::ShapePath);
    auto *pathElement = static_cast<pag::ShapePathElement *>(group->elements[0]);
    ASSERT_NE(pathElement->shapePath, nullptr);
    ASSERT_TRUE(pathElement->shapePath->animatable());

    pag::PathHandle atStart = pathElement->shapePath->getValueAt(0);
    pag::PathHandle atMid = pathElement->shapePath->getValueAt(9);
    pag::PathHandle atEnd = pathElement->shapePath->getValueAt(19);
    ASSERT_NE(atStart, nullptr);
    ASSERT_NE(atMid, nullptr);
    ASSERT_NE(atEnd, nullptr);

    size_t curves = 0;
    size_t lines = 0;
    CountPathVerbs(atStart, &curves, &lines);
    EXPECT_GE(curves, 3u);
    EXPECT_EQ(lines, 0u);
    EXPECT_LT(atStart->points.size(), 20u);

    CountPathVerbs(atMid, &curves, &lines);
    EXPECT_GE(curves, 3u);
    EXPECT_EQ(lines, 0u);
    EXPECT_LT(atMid->points.size(), 20u);

    CountPathVerbs(atEnd, &curves, &lines);
    EXPECT_GE(curves, 3u);
    EXPECT_EQ(lines, 0u);

    // Collapsed crescent morph had ~7% of correct area; cubic morph stays large.
    EXPECT_GT(PathAabbArea(atMid), PathAabbArea(atStart) * 0.5f);
}

TEST(PagExporterTest, InsideOutsideStrokeBakedAsParallelLayers) {
    // Shape Stroke has no position; bake Outside/Inside to parallel ShapeLayers
    // (outline Path + Fill) so viewers draw them and stroke animation can keyframe the path.
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "PathDualStroke";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto path = std::make_unique<ShapePath>();
    BezierPath geometry = MakeSingleContour({{{-50, -50}, {}, {}}, {{50, -50}, {}, {}}, {{50, 50}, {}, {}}, {{-50, 50}, {}, {}}}, true);
    path->path.setStaticValue(BezierPathToVectorNetwork(geometry));
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);

    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.2f, 0.6f, 0.4f, 1.0f});
    layer->styles.push_back(std::move(fill));

    auto inside = std::make_unique<StrokeStyle>();
    inside->color.setStaticValue(Color{1, 1, 1, 1});
    inside->width.setStaticValue(9.0f);
    inside->position = StrokePosition::Inside;
    layer->styles.push_back(std::move(inside));

    auto outside = std::make_unique<StrokeStyle>();
    outside->color.setStaticValue(Color{0, 0, 0, 1});
    outside->width.setStaticValue(9.0f);
    outside->position = StrokePosition::Outside;
    layer->styles.push_back(std::move(outside));

    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    int bakedCount = 0;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "StrokePositionBaked") {
            ++bakedCount;
        }
        EXPECT_NE(warning.code, "UnsupportedStrokePosition");
        EXPECT_NE(warning.code, "StrokePositionBakeFailed");
        EXPECT_NE(warning.code, "StrokePositionApproximated");
    }
    EXPECT_EQ(bakedCount, 2);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    // PAG order: index 0 = topmost. After reverse: Outside, Inside, Main, …, backdrop.
    ASSERT_GE(vector->layers.size(), 4u);
    auto *outsideLayer = static_cast<pag::ShapeLayer *>(vector->layers[0]);
    auto *insideLayer = static_cast<pag::ShapeLayer *>(vector->layers[1]);
    auto *mainLayer = static_cast<pag::ShapeLayer *>(vector->layers[2]);
    EXPECT_EQ(outsideLayer->name, "PathDualStroke / Stroke Outside");
    EXPECT_EQ(insideLayer->name, "PathDualStroke / Stroke Inside");
    EXPECT_EQ(mainLayer->name, "PathDualStroke");
    EXPECT_TRUE(mainLayer->layerStyles.empty());

    ASSERT_EQ(mainLayer->contents.size(), 1u);
    auto *mainGroup = static_cast<pag::ShapeGroupElement *>(mainLayer->contents[0]);
    ASSERT_EQ(mainGroup->elements.size(), 2u);
    EXPECT_EQ(mainGroup->elements[0]->type(), pag::ShapeType::ShapePath);
    EXPECT_EQ(mainGroup->elements[1]->type(), pag::ShapeType::Fill);

    auto expectOutlineLayer = [](pag::ShapeLayer *shapeLayer, uint8_t red, uint8_t green,
                                 uint8_t blue) {
        ASSERT_EQ(shapeLayer->contents.size(), 1u);
        auto *group = static_cast<pag::ShapeGroupElement *>(shapeLayer->contents[0]);
        ASSERT_EQ(group->elements.size(), 2u);
        EXPECT_EQ(group->elements[0]->type(), pag::ShapeType::ShapePath);
        ASSERT_EQ(group->elements[1]->type(), pag::ShapeType::Fill);
        auto *fillElement = static_cast<pag::FillElement *>(group->elements[1]);
        EXPECT_EQ(fillElement->color->value.red, red);
        EXPECT_EQ(fillElement->color->value.green, green);
        EXPECT_EQ(fillElement->color->value.blue, blue);
    };
    expectOutlineLayer(insideLayer, 255, 255, 255);
    expectOutlineLayer(outsideLayer, 0, 0, 0);
}

TEST(PagExporterTest, PositionedStrokeTrimDoesNotClipMainFill) {
    // TrimPaths on the main group would clip Fill; trimmed Outside must be a sibling layer.
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "PathTrimOutside";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto path = std::make_unique<ShapePath>();
    BezierPath geometry = MakeSingleContour({{{-50, -50}, {}, {}}, {{50, -50}, {}, {}}, {{50, 50}, {}, {}}, {{-50, 50}, {}, {}}}, true);
    path->path.setStaticValue(BezierPathToVectorNetwork(geometry));
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);

    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.2f, 0.6f, 0.4f, 1.0f});
    layer->styles.push_back(std::move(fill));

    auto outside = std::make_unique<StrokeStyle>();
    outside->color.setStaticValue(Color{0, 0, 0, 1});
    outside->width.setStaticValue(9.0f);
    outside->position = StrokePosition::Outside;
    outside->trimStart.setStaticValue(0.05f);
    outside->trimEnd.setStaticValue(0.35f);
    Keyframe<float> offset0;
    offset0.time = 0;
    offset0.value = 0.0f;
    Keyframe<float> offset1;
    offset1.time = 149;
    offset1.value = 3600.0f;
    outside->trimOffset.addKeyframe(offset0);
    outside->trimOffset.addKeyframe(offset1);
    layer->styles.push_back(std::move(outside));

    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_GE(vector->layers.size(), 3u);

    pag::ShapeLayer *mainLayer = nullptr;
    pag::ShapeLayer *strokeLayer = nullptr;
    for (pag::Layer *pagLayer : vector->layers) {
        if (pagLayer->name == "PathTrimOutside") {
            mainLayer = static_cast<pag::ShapeLayer *>(pagLayer);
        } else if (pagLayer->name == "PathTrimOutside / Stroke Outside") {
            strokeLayer = static_cast<pag::ShapeLayer *>(pagLayer);
        }
    }
    ASSERT_NE(mainLayer, nullptr);
    ASSERT_NE(strokeLayer, nullptr);

    auto *mainGroup = static_cast<pag::ShapeGroupElement *>(mainLayer->contents[0]);
    for (pag::ShapeElement *element : mainGroup->elements) {
        EXPECT_NE(element->type(), pag::ShapeType::TrimPaths);
        EXPECT_NE(element->type(), pag::ShapeType::Stroke);
    }
    ASSERT_EQ(mainGroup->elements.size(), 2u);
    EXPECT_EQ(mainGroup->elements[0]->type(), pag::ShapeType::ShapePath);
    EXPECT_EQ(mainGroup->elements[1]->type(), pag::ShapeType::Fill);

    auto *strokeGroup = static_cast<pag::ShapeGroupElement *>(strokeLayer->contents[0]);
    ASSERT_EQ(strokeGroup->elements.size(), 2u);
    EXPECT_EQ(strokeGroup->elements[0]->type(), pag::ShapeType::ShapePath);
    EXPECT_EQ(strokeGroup->elements[1]->type(), pag::ShapeType::Fill);
    auto *pathElement = static_cast<pag::ShapePathElement *>(strokeGroup->elements[0]);
    ASSERT_TRUE(pathElement->shapePath->animatable());
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
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
}

TEST(PagExporterTest, LinearGradientFillExportsGradientFillElement) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 50});
    auto *fill = static_cast<FillStyle *>(layer->styles[0].get());
    fill->paintMode = StylePaintMode::Gradient;
    fill->gradient.type = GradientType::Linear;
    // Stored in AABB top-left space; rect center (0,0) size (100,50) → min (-50,-25).
    fill->gradient.start.setStaticValue(Vec2{0, 25});
    fill->gradient.end.setStaticValue(Vec2{100, 25});
    fill->gradient.stops.resize(2);
    fill->gradient.stops[0].color.setStaticValue(Color{1, 0, 0, 1});
    fill->gradient.stops[0].position.setStaticValue(0.f);
    fill->gradient.stops[1].color.setStaticValue(Color{0, 0, 1, 1});
    fill->gradient.stops[1].position.setStaticValue(1.f);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    auto *shapeLayer = static_cast<pag::ShapeLayer *>(vector->layers[0]);
    auto *group = static_cast<pag::ShapeGroupElement *>(shapeLayer->contents[0]);
    pag::GradientFillElement *pagFill = nullptr;
    for (pag::ShapeElement *element : group->elements) {
        if (element->type() == pag::ShapeType::GradientFill) {
            pagFill = static_cast<pag::GradientFillElement *>(element);
            break;
        }
    }
    ASSERT_NE(pagFill, nullptr);
    EXPECT_EQ(pagFill->fillType, pag::GradientFillType::Linear);
    ASSERT_NE(pagFill->startPoint, nullptr);
    ASSERT_NE(pagFill->endPoint, nullptr);
    // Exported in shape-path space (aabbMin + stored).
    EXPECT_FLOAT_EQ(pagFill->startPoint->value.x, -50);
    EXPECT_FLOAT_EQ(pagFill->startPoint->value.y, 0);
    EXPECT_FLOAT_EQ(pagFill->endPoint->value.x, 50);
    EXPECT_FLOAT_EQ(pagFill->endPoint->value.y, 0);
    ASSERT_NE(pagFill->colors, nullptr);
    ASSERT_NE(pagFill->colors->value, nullptr);
    ASSERT_EQ(pagFill->colors->value->colorStops.size(), 2u);
}

TEST(PagExporterTest, ConicGradientFillSkippedWithWarning) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 50});
    auto *fill = static_cast<FillStyle *>(layer->styles[0].get());
    fill->paintMode = StylePaintMode::Gradient;
    fill->gradient.type = GradientType::Conic;
    fill->gradient.stops.resize(2);
    fill->gradient.stops[0].color.setStaticValue(Color{1, 0, 0, 1});
    fill->gradient.stops[0].position.setStaticValue(0.f);
    fill->gradient.stops[1].color.setStaticValue(Color{0, 1, 0, 1});
    fill->gradient.stops[1].position.setStaticValue(1.f);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    bool found = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "SkippedUnsupportedPaint") {
            found = true;
            EXPECT_NE(warning.message.find("BMP"), std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(found);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    auto *shapeLayer = static_cast<pag::ShapeLayer *>(vector->layers[0]);
    auto *group = static_cast<pag::ShapeGroupElement *>(shapeLayer->contents[0]);
    for (pag::ShapeElement *element : group->elements) {
        EXPECT_NE(element->type(), pag::ShapeType::GradientFill);
        EXPECT_NE(element->type(), pag::ShapeType::Fill);
    }
}

TEST(PagExporterTest, BrightnessContrastExportsAsPagEffect) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    auto effect = std::make_unique<BrightnessContrastEffect>();
    effect->brightness.setStaticValue(20.0f);
    effect->contrast.setStaticValue(-10.0f);
    layer->effects.push_back(std::move(effect));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->effects.size(), 1u);
    ASSERT_EQ(pagLayer->effects[0]->type(), pag::EffectType::BrightnessContrast);
    auto *pagEffect = static_cast<pag::BrightnessContrastEffect *>(pagLayer->effects[0]);
    ASSERT_NE(pagEffect->brightness, nullptr);
    ASSERT_NE(pagEffect->contrast, nullptr);
    ASSERT_NE(pagEffect->useOldVersion, nullptr);
    EXPECT_FLOAT_EQ(pagEffect->brightness->value, 20.0f);
    EXPECT_FLOAT_EQ(pagEffect->contrast->value, -10.0f);
    EXPECT_FALSE(pagEffect->useOldVersion->value);
}

TEST(PagExporterTest, GaussianBlurExportsAsFastBlur) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    auto effect = std::make_unique<GaussianBlurEffect>();
    effect->blurriness.setStaticValue(8.0f);
    effect->repeatEdgePixels = true;
    layer->effects.push_back(std::move(effect));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->effects.size(), 1u);
    ASSERT_EQ(pagLayer->effects[0]->type(), pag::EffectType::FastBlur);
    auto *pagEffect = static_cast<pag::FastBlurEffect *>(pagLayer->effects[0]);
    ASSERT_NE(pagEffect->blurriness, nullptr);
    ASSERT_NE(pagEffect->blurDimensions, nullptr);
    ASSERT_NE(pagEffect->repeatEdgePixels, nullptr);
    EXPECT_FLOAT_EQ(pagEffect->blurriness->value, 8.0f);
    EXPECT_EQ(pagEffect->blurDimensions->value, pag::BlurDimensionsDirection::All);
    EXPECT_TRUE(pagEffect->repeatEdgePixels->value);
}

TEST(PagExporterTest, DisabledEffectIsSkipped) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    auto disabled = std::make_unique<BrightnessContrastEffect>();
    disabled->enabled = false;
    disabled->brightness.setStaticValue(40.0f);
    layer->effects.push_back(std::move(disabled));
    auto enabled = std::make_unique<GaussianBlurEffect>();
    enabled->blurriness.setStaticValue(4.0f);
    layer->effects.push_back(std::move(enabled));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->effects.size(), 1u);
    EXPECT_EQ(pagLayer->effects[0]->type(), pag::EffectType::FastBlur);
}

TEST(PagExporterTest, IdentityBrightnessContrastStillExports) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    layer->effects.push_back(std::make_unique<BrightnessContrastEffect>());

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->effects.size(), 1u);
    EXPECT_EQ(pagLayer->effects[0]->type(), pag::EffectType::BrightnessContrast);
}

TEST(PagExporterTest, BrightnessContrastKeyframes) {
    Document document = MakeEmptyDoc(400, 300, 60);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{40, 40});
    auto effect = std::make_unique<BrightnessContrastEffect>();
    Keyframe<float> from;
    from.time = 0;
    from.value = 0.0f;
    from.easing = Easing::EaseInOut();
    Keyframe<float> to;
    to.time = 30;
    to.value = 50.0f;
    effect->brightness.addKeyframe(from);
    effect->brightness.addKeyframe(to);
    layer->effects.push_back(std::move(effect));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->effects.size(), 1u);
    auto *pagEffect = static_cast<pag::BrightnessContrastEffect *>(pagLayer->effects[0]);
    ASSERT_TRUE(pagEffect->brightness->animatable());
    auto *animated = static_cast<pag::AnimatableProperty<float> *>(pagEffect->brightness);
    ASSERT_EQ(animated->keyframes.size(), 1u);
    EXPECT_EQ(animated->keyframes[0]->startTime, 0);
    EXPECT_EQ(animated->keyframes[0]->endTime, 30);
    EXPECT_FLOAT_EQ(animated->keyframes[0]->startValue, 0.0f);
    EXPECT_FLOAT_EQ(animated->keyframes[0]->endValue, 50.0f);
}

TEST(PagExporterTest, GroupEffectsAreSkipped) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto group = std::make_unique<Layer>(LayerType::Group);
    group->name = "Group";
    group->inPoint = 0;
    group->outPoint = composition->duration;
    auto effect = std::make_unique<BrightnessContrastEffect>();
    effect->brightness.setStaticValue(20.0f);
    group->effects.push_back(std::move(effect));
    document.addLayer(composition->id, std::move(group));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *groupLayer = nullptr;
    for (pag::Layer *pagLayer : vector->layers) {
        if (pagLayer->name == "Group") {
            groupLayer = pagLayer;
            break;
        }
    }
    ASSERT_NE(groupLayer, nullptr);
    EXPECT_TRUE(groupLayer->effects.empty());
}

TEST(PagExporterTest, PrecompEffectsAreSkipped) {
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
    layer->name = "Precomp";
    layer->inPoint = 0;
    layer->outPoint = rootPtr->duration;
    static_cast<PrecompContent *>(layer->content.get())->compositionId = nestedPtr->id;
    auto effect = std::make_unique<GaussianBlurEffect>();
    effect->blurriness.setStaticValue(6.0f);
    layer->effects.push_back(std::move(effect));
    document.addLayer(rootPtr->id, std::move(layer));

    PagExportOptions options;
    options.compositionId = rootPtr->id;
    auto result = PagExporter::Export(document, options);
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    EXPECT_TRUE(main->layers[0]->effects.empty());
}

TEST(PagExporterTest, SplitStrokesWrapForEffects) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "PathDualStroke";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto path = std::make_unique<ShapePath>();
    BezierPath geometry =
        MakeSingleContour({{{-50, -50}, {}, {}}, {{50, -50}, {}, {}}, {{50, 50}, {}, {}}, {{-50, 50}, {}, {}}}, true);
    path->path.setStaticValue(BezierPathToVectorNetwork(geometry));
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);

    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.2f, 0.6f, 0.4f, 1.0f});
    layer->styles.push_back(std::move(fill));

    auto outside = std::make_unique<StrokeStyle>();
    outside->color.setStaticValue(Color{0, 0, 0, 1});
    outside->width.setStaticValue(9.0f);
    outside->position = StrokePosition::Outside;
    layer->styles.push_back(std::move(outside));

    auto effect = std::make_unique<BrightnessContrastEffect>();
    effect->brightness.setStaticValue(15.0f);
    layer->effects.push_back(std::move(effect));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    bool wrapped = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "StrokeSiblingsWrappedForEffects") {
            wrapped = true;
            break;
        }
    }
    EXPECT_TRUE(wrapped);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *root = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::PreComposeLayer *host = nullptr;
    for (pag::Layer *pagLayer : root->layers) {
        if (pagLayer->name == "PathDualStroke" && pagLayer->type() == pag::LayerType::PreCompose) {
            host = static_cast<pag::PreComposeLayer *>(pagLayer);
        }
        EXPECT_EQ(pagLayer->name.find("PathDualStroke / Stroke"), std::string::npos);
    }
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(host->effects.size(), 1u);
    EXPECT_EQ(host->effects[0]->type(), pag::EffectType::BrightnessContrast);
    ASSERT_NE(host->composition, nullptr);
    auto *inner = static_cast<pag::VectorComposition *>(host->composition);
    ASSERT_GE(inner->layers.size(), 2u);
    for (pag::Layer *innerLayer : inner->layers) {
        EXPECT_TRUE(innerLayer->effects.empty());
    }
}

TEST(PagExporterTest, LayerBmpDoesNotAttachEffects) {
    Document document = MakeEmptyDoc(400, 300, 2);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 100});
    layer->name = "Rect_bmp";
    auto effect = std::make_unique<BrightnessContrastEffect>();
    effect->brightness.setStaticValue(30.0f);
    layer->effects.push_back(std::move(effect));

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapExport = true;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    EXPECT_TRUE(main->layers[0]->effects.empty());
}

TEST(PagExporterTest, DropShadowExportsAsPagLayerStyle) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    auto style = std::make_unique<DropShadowStyle>();
    style->color.setStaticValue(Color{0.1f, 0.2f, 0.3f, 1.0f});
    style->opacity.setStaticValue(0.5f);
    style->angle.setStaticValue(90.0f);
    style->distance.setStaticValue(12.0f);
    style->size.setStaticValue(8.0f);
    style->spread.setStaticValue(0.25f);
    layer->layerStyles.push_back(std::move(style));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->layerStyles.size(), 1u);
    ASSERT_EQ(pagLayer->layerStyles[0]->type(), pag::LayerStyleType::DropShadow);
    auto *pagStyle = static_cast<pag::DropShadowStyle *>(pagLayer->layerStyles[0]);
    ASSERT_NE(pagStyle->blendMode, nullptr);
    ASSERT_NE(pagStyle->color, nullptr);
    ASSERT_NE(pagStyle->opacity, nullptr);
    ASSERT_NE(pagStyle->angle, nullptr);
    ASSERT_NE(pagStyle->distance, nullptr);
    ASSERT_NE(pagStyle->size, nullptr);
    ASSERT_NE(pagStyle->spread, nullptr);
    EXPECT_EQ(pagStyle->blendMode->value, pag::BlendMode::Multiply);
    EXPECT_EQ(pagStyle->color->value.red, 26);
    EXPECT_EQ(pagStyle->color->value.green, 51);
    EXPECT_EQ(pagStyle->color->value.blue, 77);
    EXPECT_EQ(pagStyle->opacity->value, 128);
    EXPECT_FLOAT_EQ(pagStyle->angle->value, 90.0f);
    EXPECT_FLOAT_EQ(pagStyle->distance->value, 12.0f);
    EXPECT_FLOAT_EQ(pagStyle->size->value, 8.0f);
    EXPECT_FLOAT_EQ(pagStyle->spread->value, 0.25f);
}

TEST(PagExporterTest, OuterGlowExportsAsPagLayerStyle) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    auto style = std::make_unique<OuterGlowStyle>();
    style->size.setStaticValue(10.0f);
    style->spread.setStaticValue(0.4f);
    style->range.setStaticValue(0.8f);
    layer->layerStyles.push_back(std::move(style));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->layerStyles.size(), 1u);
    ASSERT_EQ(pagLayer->layerStyles[0]->type(), pag::LayerStyleType::OuterGlow);
    auto *pagStyle = static_cast<pag::OuterGlowStyle *>(pagLayer->layerStyles[0]);
    ASSERT_NE(pagStyle->blendMode, nullptr);
    ASSERT_NE(pagStyle->color, nullptr);
    ASSERT_NE(pagStyle->opacity, nullptr);
    ASSERT_NE(pagStyle->size, nullptr);
    ASSERT_NE(pagStyle->spread, nullptr);
    ASSERT_NE(pagStyle->range, nullptr);
    ASSERT_NE(pagStyle->noise, nullptr);
    ASSERT_NE(pagStyle->colorType, nullptr);
    ASSERT_NE(pagStyle->technique, nullptr);
    ASSERT_NE(pagStyle->jitter, nullptr);
    EXPECT_EQ(pagStyle->blendMode->value, pag::BlendMode::Screen);
    EXPECT_EQ(pagStyle->opacity->value, 191);
    EXPECT_FLOAT_EQ(pagStyle->size->value, 10.0f);
    EXPECT_FLOAT_EQ(pagStyle->spread->value, 0.4f);
    EXPECT_FLOAT_EQ(pagStyle->range->value, 0.8f);
    EXPECT_FLOAT_EQ(pagStyle->noise->value, 0.0f);
    EXPECT_EQ(pagStyle->colorType->value, pag::GlowColorType::SingleColor);
    EXPECT_EQ(pagStyle->technique->value, pag::GlowTechniqueType::Softer);
    EXPECT_FLOAT_EQ(pagStyle->jitter->value, 0.0f);
}

TEST(PagExporterTest, LayerStrokePositionMapsToPagOrder) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    auto center = std::make_unique<LayerStrokeStyle>();
    center->position = StrokePosition::Center;
    center->size.setStaticValue(2.0f);
    layer->layerStyles.push_back(std::move(center));
    auto inside = std::make_unique<LayerStrokeStyle>();
    inside->position = StrokePosition::Inside;
    inside->size.setStaticValue(3.0f);
    layer->layerStyles.push_back(std::move(inside));
    auto outside = std::make_unique<LayerStrokeStyle>();
    outside->position = StrokePosition::Outside;
    outside->size.setStaticValue(4.0f);
    layer->layerStyles.push_back(std::move(outside));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->layerStyles.size(), 3u);
    auto *centerStyle = static_cast<pag::StrokeStyle *>(pagLayer->layerStyles[0]);
    auto *insideStyle = static_cast<pag::StrokeStyle *>(pagLayer->layerStyles[1]);
    auto *outsideStyle = static_cast<pag::StrokeStyle *>(pagLayer->layerStyles[2]);
    EXPECT_EQ(centerStyle->position->value, pag::StrokePosition::Center);
    EXPECT_EQ(insideStyle->position->value, pag::StrokePosition::Inside);
    EXPECT_EQ(outsideStyle->position->value, pag::StrokePosition::Outside);
    EXPECT_NE(static_cast<int>(StrokePosition::Center),
              static_cast<int>(pag::StrokePosition::Center));
}

TEST(PagExporterTest, DisabledLayerStyleIsSkipped) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{50, 60}, Vec2{120, 80});
    auto disabled = std::make_unique<DropShadowStyle>();
    disabled->enabled = false;
    disabled->distance.setStaticValue(10.0f);
    layer->layerStyles.push_back(std::move(disabled));
    auto enabled = std::make_unique<OuterGlowStyle>();
    enabled->size.setStaticValue(6.0f);
    layer->layerStyles.push_back(std::move(enabled));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *pagLayer = vector->layers[0];
    ASSERT_EQ(pagLayer->type(), pag::LayerType::Solid);
    ASSERT_EQ(pagLayer->layerStyles.size(), 1u);
    EXPECT_EQ(pagLayer->layerStyles[0]->type(), pag::LayerStyleType::OuterGlow);
}

TEST(PagExporterTest, SplitStrokesWrapForLayerStyles) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "PathDualStroke";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto path = std::make_unique<ShapePath>();
    BezierPath geometry =
        MakeSingleContour({{{-50, -50}, {}, {}}, {{50, -50}, {}, {}}, {{50, 50}, {}, {}}, {{-50, 50}, {}, {}}}, true);
    path->path.setStaticValue(BezierPathToVectorNetwork(geometry));
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(path);

    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.2f, 0.6f, 0.4f, 1.0f});
    layer->styles.push_back(std::move(fill));

    auto outside = std::make_unique<StrokeStyle>();
    outside->color.setStaticValue(Color{0, 0, 0, 1});
    outside->width.setStaticValue(9.0f);
    outside->position = StrokePosition::Outside;
    layer->styles.push_back(std::move(outside));

    auto style = std::make_unique<DropShadowStyle>();
    style->distance.setStaticValue(8.0f);
    layer->layerStyles.push_back(std::move(style));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    bool wrapped = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "StrokeSiblingsWrappedForLayerStyles") {
            wrapped = true;
            break;
        }
    }
    EXPECT_TRUE(wrapped);

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *root = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::PreComposeLayer *host = nullptr;
    for (pag::Layer *pagLayer : root->layers) {
        if (pagLayer->name == "PathDualStroke" && pagLayer->type() == pag::LayerType::PreCompose) {
            host = static_cast<pag::PreComposeLayer *>(pagLayer);
        }
        EXPECT_EQ(pagLayer->name.find("PathDualStroke / Stroke"), std::string::npos);
    }
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(host->layerStyles.size(), 1u);
    EXPECT_EQ(host->layerStyles[0]->type(), pag::LayerStyleType::DropShadow);
    ASSERT_NE(host->composition, nullptr);
    auto *inner = static_cast<pag::VectorComposition *>(host->composition);
    ASSERT_GE(inner->layers.size(), 2u);
    for (pag::Layer *innerLayer : inner->layers) {
        EXPECT_TRUE(innerLayer->layerStyles.empty());
    }
}

TEST(PagExporterTest, GroupLayerStylesAreSkipped) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto group = std::make_unique<Layer>(LayerType::Group);
    group->name = "Group";
    group->inPoint = 0;
    group->outPoint = composition->duration;
    group->layerStyles.push_back(std::make_unique<DropShadowStyle>());
    document.addLayer(composition->id, std::move(group));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *groupLayer = nullptr;
    for (pag::Layer *pagLayer : vector->layers) {
        if (pagLayer->name == "Group") {
            groupLayer = pagLayer;
            break;
        }
    }
    ASSERT_NE(groupLayer, nullptr);
    EXPECT_TRUE(groupLayer->layerStyles.empty());
}

TEST(PagExporterTest, LayerBmpDoesNotAttachLayerStyles) {
    Document document = MakeEmptyDoc(400, 300, 2);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 100});
    layer->name = "Rect_bmp";
    layer->layerStyles.push_back(std::make_unique<DropShadowStyle>());

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapExport = true;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    EXPECT_TRUE(main->layers[0]->layerStyles.empty());
}

TEST(PagExporterTest, ColorRectFillExportsAsSolidLayer) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{120, 80});
    layer->transform.position.setStaticValue(Vec2{200, 150});
    auto *fill = static_cast<FillStyle *>(layer->styles[0].get());
    fill->color.setStaticValue(Color{1.0f, 0.0f, 0.0f, 1.0f});

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Solid);
    auto *solid = static_cast<pag::SolidLayer *>(vector->layers[0]);
    EXPECT_EQ(solid->width, 120);
    EXPECT_EQ(solid->height, 80);
    EXPECT_EQ(solid->solidColor.red, 255);
    EXPECT_EQ(solid->solidColor.green, 0);
    EXPECT_EQ(solid->solidColor.blue, 0);
    EXPECT_FLOAT_EQ(solid->transform->position->value.x, 200.0f);
    EXPECT_FLOAT_EQ(solid->transform->position->value.y, 150.0f);
    EXPECT_FLOAT_EQ(solid->transform->anchorPoint->value.x, 60.0f);
    EXPECT_FLOAT_EQ(solid->transform->anchorPoint->value.y, 40.0f);
}

TEST(PagExporterTest, RoundedRectStaysShapeLayer) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{120, 80});
    auto *rect = static_cast<ShapeRect *>(static_cast<ShapeContent *>(layer->content.get())->geometry.get());
    rect->cornerRadius.setStaticValue(8.0f);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    EXPECT_EQ(vector->layers[0]->type(), pag::LayerType::Shape);
}

TEST(PagExporterTest, EllipseStaysShapeLayer) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "Ellipse";
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    auto ellipse = std::make_unique<ShapeEllipse>();
    ellipse->size.setStaticValue(Vec2{80, 80});
    static_cast<ShapeContent *>(layer->content.get())->geometry = std::move(ellipse);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0, 1, 0, 1});
    layer->styles.push_back(std::move(fill));
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    EXPECT_EQ(vector->layers[0]->type(), pag::LayerType::Shape);
}

TEST(PagExporterTest, StrokePlusFillStaysShapeLayer) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{120, 80});
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 0, 0, 1});
    stroke->width.setStaticValue(2.0f);
    layer->styles.push_back(std::move(stroke));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    EXPECT_EQ(vector->layers[0]->type(), pag::LayerType::Shape);
}

pag::StrokeElement *FindFirstPagStroke(pag::File *file) {
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    for (pag::Layer *layer : vector->layers) {
        if (layer->type() != pag::LayerType::Shape) {
            continue;
        }
        auto *shape = static_cast<pag::ShapeLayer *>(layer);
        for (pag::ShapeElement *content : shape->contents) {
            if (content->type() != pag::ShapeType::ShapeGroup) {
                continue;
            }
            auto *group = static_cast<pag::ShapeGroupElement *>(content);
            for (pag::ShapeElement *element : group->elements) {
                if (element->type() == pag::ShapeType::Stroke) {
                    return static_cast<pag::StrokeElement *>(element);
                }
            }
        }
    }
    return nullptr;
}

TEST(PagExporterTest, CenterDashedStrokeExportsDashIntervals) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{120, 80});
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 0, 0, 1});
    stroke->width.setStaticValue(2.0f);
    stroke->strokeMode = StrokeMode::Dashed;
    stroke->dashes = {8.0f, 8.0f};
    layer->styles.push_back(std::move(stroke));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    pag::StrokeElement *pagStroke = FindFirstPagStroke(file.get());
    ASSERT_NE(pagStroke, nullptr);
    ASSERT_EQ(pagStroke->dashes.size(), 2u);
    ASSERT_NE(pagStroke->dashes[0], nullptr);
    EXPECT_FLOAT_EQ(pagStroke->dashes[0]->value, 8.0f);
    ASSERT_NE(pagStroke->dashes[1], nullptr);
    EXPECT_FLOAT_EQ(pagStroke->dashes[1]->value, 8.0f);
}

TEST(PagExporterTest, SolidStrokeOmitsStoredDashes) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{120, 80});
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 0, 0, 1});
    stroke->width.setStaticValue(2.0f);
    stroke->strokeMode = StrokeMode::Solid;
    stroke->dashes = {8.0f, 8.0f};
    layer->styles.push_back(std::move(stroke));

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    pag::StrokeElement *pagStroke = FindFirstPagStroke(file.get());
    ASSERT_NE(pagStroke, nullptr);
    EXPECT_TRUE(pagStroke->dashes.empty());
}

TEST(PagExporterTest, AnimatedFillColorStaysShapeLayer) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{120, 80});
    auto *fill = static_cast<FillStyle *>(layer->styles[0].get());
    Keyframe<Color> from;
    from.time = 0;
    from.value = Color{1, 0, 0, 1};
    Keyframe<Color> to;
    to.time = 15;
    to.value = Color{0, 0, 1, 1};
    fill->color.addKeyframe(from);
    fill->color.addKeyframe(to);

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    EXPECT_EQ(vector->layers[0]->type(), pag::LayerType::Shape);
}

#if defined(__APPLE__)

TEST(PagExporterTest, BmpSequenceTypeVideoExportsVideoComposition) {
    Document document = MakeEmptyDoc(64, 64, 2);
    Primary(document)->name = "Main_bmp";
    FakeBitmapFrameSource frameSource(255, 0, 0, 255);
    PagExportOptions options;
    options.allowBitmapExport = true;
    options.bmpSequenceType = PagBmpSequenceType::Video;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(file->compositions.back()->type(), pag::CompositionType::Video);
    auto *video = static_cast<pag::VideoComposition *>(file->compositions.back());
    ASSERT_FALSE(video->sequences.empty());
    EXPECT_EQ(video->sequences[0]->alphaStartX, video->sequences[0]->width);
    EXPECT_EQ(video->sequences[0]->alphaStartY, 0);
}

TEST(PagExporterTest, BmpSequenceTypeAutoStaticUsesBitmap) {
    Document document = MakeEmptyDoc(40, 30, 3);
    Primary(document)->name = "Main_bmp";
    FakeBitmapFrameSource frameSource(10, 20, 30, 255);
    PagExportOptions options;
    options.bmpSequenceType = PagBmpSequenceType::Auto;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->compositions.back()->type(), pag::CompositionType::Bitmap);
}

TEST(PagExporterTest, BmpSequenceTypeAutoMotionUsesVideo) {
    Document document = MakeEmptyDoc(40, 30, 2);
    Primary(document)->name = "Main_bmp";
    FakeBitmapFrameSource frameSource(255, 0, 0, 255);
    frameSource.setFrameColor(0, 255, 0, 0, 255);
    frameSource.setFrameColor(1, 0, 0, 255, 255);
    PagExportOptions options;
    options.bmpSequenceType = PagBmpSequenceType::Auto;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->compositions.back()->type(), pag::CompositionType::Video);
}

TEST(PagExporterTest, LayerBmpVideoMaxResolutionScalesPrecompose) {
    Document document = MakeEmptyDoc(1920, 1080, 2);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{100, 100});
    layer->name = "Rect_bmp";

    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapExport = true;
    options.bmpSequenceType = PagBmpSequenceType::Video;
    options.bitmapScale = 1.0f;
    options.bitmapMaxResolution = 720;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;

    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *main = static_cast<pag::VectorComposition *>(file->compositions.back());
    ASSERT_GE(main->layers.size(), 1u);
    ASSERT_EQ(main->layers[0]->type(), pag::LayerType::PreCompose);
    auto *precomp = static_cast<pag::PreComposeLayer *>(main->layers[0]);
    ASSERT_NE(precomp->composition, nullptr);
    EXPECT_EQ(precomp->composition->type(), pag::CompositionType::Video);
    EXPECT_EQ(precomp->composition->width, 1280);
    EXPECT_EQ(precomp->composition->height, 720);
    ASSERT_NE(precomp->transform, nullptr);
    ASSERT_NE(precomp->transform->scale, nullptr);
    EXPECT_FLOAT_EQ(precomp->transform->scale->value.x, 1.5f);
    EXPECT_FLOAT_EQ(precomp->transform->scale->value.y, 1.5f);
}

#endif
