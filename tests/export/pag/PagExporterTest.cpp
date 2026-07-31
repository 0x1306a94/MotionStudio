#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/TextContent.h"
#include "pag/file.h"

using motion::BlendMode;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FillStyle;
using motion::FrameTime;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::PagExporter;
using motion::PagExportError;
using motion::PagExportOptions;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapePath;
using motion::ShapeRect;
using motion::StrokeStyle;
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
    ASSERT_EQ(vector->layers.size(), 1u);
    ASSERT_EQ(vector->layers[0]->type(), pag::LayerType::Shape);
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
    ASSERT_EQ(vector->layers.size(), 2u);
    EXPECT_EQ(vector->layers[0]->type(), pag::LayerType::Null);
    EXPECT_EQ(vector->layers[1]->type(), pag::LayerType::Shape);
    EXPECT_EQ(vector->layers[1]->parent, vector->layers[0]);
}

TEST(PagExporterTest, FollowPathFails) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    Layer *layer = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{20, 20});
    layer->followPath.enabled = true;

    auto result = PagExporter::Export(document, {});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), PagExportError::MappingFailed);
}

TEST(PagExporterTest, TextLayerFails) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Composition *composition = Primary(document);
    auto layer = std::make_unique<Layer>(LayerType::Text);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    document.addLayer(composition->id, std::move(layer));

    auto result = PagExporter::Export(document, {});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), PagExportError::MappingFailed);
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
