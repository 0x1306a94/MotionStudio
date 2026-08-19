#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/import/svg/SvgImporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/undo/UndoManager.h"
#include "SvgPathConvert.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"

using motion::svg::BuildSvgLayers;
using motion::svg::ImportOptions;

TEST(SvgImporterTest, EmptyBufferFails) {
    const auto result = BuildSvgLayers("", 0);
    ASSERT_FALSE(result.hasValue());
}

TEST(SvgImporterTest, NonSvgRootFails) {
    const std::string xml = "<html></html>";
    const auto result = BuildSvgLayers(xml.data(), xml.size());
    ASSERT_FALSE(result.hasValue());
}

TEST(SvgImporterTest, EmptySvgReportsSourceSizeAndRootGroup) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\"></svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().sourceWidth, 200);
    EXPECT_EQ(result.value().sourceHeight, 100);
    ASSERT_EQ(result.value().layers.size(), 1u);
    EXPECT_EQ(result.value().layers[0]->type(), motion::LayerType::Group);
    EXPECT_EQ(result.value().layers[0]->name, "SVG");
}

TEST(SvgPathConvertTest, ClosedRectHasFourEdges) {
    tgfx::Path path;
    path.addRect(tgfx::Rect::MakeXYWH(10, 20, 40, 30));
    bool usedConic = false;
    const motion::VectorNetwork network = motion::svg::PathToVectorNetwork(path, &usedConic);
    EXPECT_FALSE(usedConic);
    EXPECT_EQ(network.vertices.size(), 4u);
    EXPECT_EQ(network.edges.size(), 4u);
    EXPECT_EQ(network.vertices.front().id, 1u);
    EXPECT_EQ(network.edges.front().id, 1u);
    EXPECT_EQ(network.vertices.front().mirrorMode, motion::VertexMirrorMode::None);
}

TEST(SvgPathConvertTest, OpenLineHasOneEdge) {
    tgfx::Path path;
    path.moveTo(0, 0);
    path.lineTo(10, 0);
    bool usedConic = false;
    const motion::VectorNetwork network = motion::svg::PathToVectorNetwork(path, &usedConic);
    EXPECT_EQ(network.vertices.size(), 2u);
    EXPECT_EQ(network.edges.size(), 1u);
}

TEST(SvgImporterTest, PathFillStrokeBecomesShapePath) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<path d=\"M0 0 H10 V10 H0 Z\" fill=\"#ff0000\" stroke=\"#0000ff\" stroke-width=\"4\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().layers.size(), 2u);
    const motion::Layer *shape = result.value().layers[1].get();
    EXPECT_EQ(shape->type(), motion::LayerType::Shape);
    EXPECT_EQ(shape->parentId, result.value().layers[0]->id);
    auto *content = static_cast<motion::ShapeContent *>(shape->content.get());
    ASSERT_NE(content->geometry, nullptr);
    EXPECT_EQ(content->geometry->type(), motion::ShapeType::Path);
    auto *path = static_cast<motion::ShapePath *>(content->geometry.get());
    const motion::VectorNetwork network = path->path.staticValue();
    EXPECT_FALSE(network.edges.empty());
    ASSERT_EQ(shape->styles.size(), 2u);
    auto *fill = static_cast<motion::FillStyle *>(shape->styles[0].get());
    EXPECT_EQ(fill->type(), motion::LayerStyleType::Fill);
    EXPECT_NEAR(fill->color.staticValue().r, 1.f, 1e-3f);
    auto *stroke = static_cast<motion::StrokeStyle *>(shape->styles[1].get());
    EXPECT_NEAR(stroke->width.staticValue(), 4.f, 1e-3f);
    EXPECT_EQ(stroke->position, motion::StrokePosition::Center);
}

TEST(SvgImporterTest, RectCircleEllipseAreVectorNetworks) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<rect x=\"10\" y=\"20\" width=\"40\" height=\"30\" fill=\"#00ff00\"/>"
        "<circle cx=\"50\" cy=\"50\" r=\"10\" fill=\"#000000\"/>"
        "<ellipse cx=\"80\" cy=\"40\" rx=\"20\" ry=\"10\" fill=\"#000000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().layers.size(), 4u);
    for (size_t i = 1; i < result.value().layers.size(); ++i) {
        auto *content =
            static_cast<motion::ShapeContent *>(result.value().layers[i]->content.get());
        ASSERT_EQ(content->geometry->type(), motion::ShapeType::Path);
    }
}

TEST(SvgImporterTest, AxisAlignedRectUsesCenterAnchor) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<rect x=\"10\" y=\"20\" width=\"40\" height=\"30\" fill=\"#000000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers[1].get();
    const motion::Vec2 anchor = shape->transform.anchorPoint.staticValue();
    const motion::Vec2 position = shape->transform.position.staticValue();
    EXPECT_NEAR(anchor.x, 30.f, 1e-3f);
    EXPECT_NEAR(anchor.y, 35.f, 1e-3f);
    EXPECT_NEAR(position.x, 30.f, 1e-3f);
    EXPECT_NEAR(position.y, 35.f, 1e-3f);
}

TEST(SvgImporterTest, LineIsOpenStrokeOnly) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" stroke=\"#000000\" stroke-width=\"2\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers[1].get();
    ASSERT_EQ(shape->styles.size(), 1u);
    EXPECT_EQ(shape->styles[0]->type(), motion::LayerStyleType::Stroke);
}

TEST(SvgImporterTest, GroupTransformParentsChild) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<g transform=\"translate(10 20) rotate(90)\">"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"10\" fill=\"#000000\"/>"
        "</g></svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_GE(result.value().layers.size(), 3u);
    const motion::Layer *group = result.value().layers[1].get();
    const motion::Layer *shape = result.value().layers[2].get();
    EXPECT_EQ(group->type(), motion::LayerType::Group);
    EXPECT_EQ(shape->parentId, group->id);
    EXPECT_NEAR(group->transform.rotation.staticValue(), 90.f, 1e-2f);
    const motion::Mat3 local = group->localTransform(0);
    const motion::Vec2 mapped = local.transformPoint({0.f, 0.f});
    EXPECT_NEAR(mapped.x, 10.f, 0.05f);
    EXPECT_NEAR(mapped.y, 20.f, 0.05f);
}

TEST(SvgImporterTest, ViewBoxDoesNotResizeCallerComposition) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\" "
        "viewBox=\"10 20 80 40\"></svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().sourceWidth, 100);
    EXPECT_EQ(result.value().sourceHeight, 50);
    const motion::Vec2 scale = result.value().layers[0]->transform.scale.staticValue();
    EXPECT_GT(scale.x, 0.f);
}

TEST(SvgImporterTest, ImportIntoExistingCompositionIsUndoable) {
    motion::Document document;
    auto composition = std::make_unique<motion::Composition>();
    composition->width = 640;
    composition->height = 480;
    composition->duration = 90;
    composition->frameRate = {30, 1};
    const motion::EntityId compositionId = composition->id;
    document.addComposition(std::move(composition));

    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"80\" height=\"40\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000000\"/>"
        "</svg>";
    motion::UndoManager undo;
    const auto imported = motion::svg::ImportSvgInto(document, undo, compositionId, svg.data(),
                                                     svg.size());
    ASSERT_TRUE(imported.hasValue());
    motion::Composition *host = document.entityIndex().findComposition(compositionId);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(host->width, 640);
    EXPECT_EQ(host->height, 480);
    EXPECT_EQ(host->duration, 90);
    EXPECT_GE(host->layers.size(), 2u);
    EXPECT_EQ(host->layers.back()->outPoint, 90);
    EXPECT_EQ(imported.value().rootLayerId, host->layers[host->layers.size() - 2]->id);

    undo.undo(document);
    EXPECT_TRUE(host->layers.empty());
    EXPECT_EQ(host->width, 640);
}

TEST(SvgImporterTest, MissingCompositionFailsWithoutMutation) {
    motion::Document document;
    motion::UndoManager undo;
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"></svg>";
    const auto imported =
        motion::svg::ImportSvgInto(document, undo, motion::EntityId{}, svg.data(), svg.size());
    ASSERT_FALSE(imported.hasValue());
    EXPECT_TRUE(document.compositions.empty());
}

TEST(SvgImporterTest, FillInheritsFromGroup) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<g fill=\"#ff0000\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/></g>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers.back().get();
    auto *fill = static_cast<motion::FillStyle *>(shape->styles[0].get());
    EXPECT_NEAR(fill->color.staticValue().r, 1.f, 1e-3f);
    EXPECT_EQ(result.value().layers[1]->styles.size(), 0u);
}

TEST(SvgImporterTest, DisplayNoneDropsSubtree) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<g display=\"none\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000\"/></g>"
        "<rect x=\"20\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.size(), 2u);
}

TEST(SvgImporterTest, HiddenStaysInvisible) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000\" visibility=\"hidden\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().layers.back()->visible);
}

TEST(SvgImporterTest, StrokeDashIsWarned) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" stroke=\"#000\" stroke-dasharray=\"2 2\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "stroke.dash") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(SvgImporterTest, LinearGradientMapsToPaint) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient></defs>"
        "<rect x=\"0\" y=\"0\" width=\"100\" height=\"20\" fill=\"url(#g)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers.back().get();
    auto *fill = static_cast<motion::FillStyle *>(shape->styles[0].get());
    EXPECT_EQ(fill->paintMode, motion::StylePaintMode::Gradient);
    EXPECT_EQ(fill->gradient.type, motion::GradientType::Linear);
    ASSERT_GE(fill->gradient.stops.size(), 2u);
}

TEST(SvgImporterTest, UseExpandsWithoutDefsLayer) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><rect id=\"r\" x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#0f0\"/></defs>"
        "<use href=\"#r\" x=\"5\" y=\"6\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_GE(result.value().layers.size(), 2u);
    for (const auto &layer : result.value().layers) {
        EXPECT_NE(layer->name, "defs");
    }
}
