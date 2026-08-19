#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/import/svg/SvgImporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/NullContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/StrokeMode.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/TextContent.h"
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

TEST(SvgImporterTest, RectIsShapeRectCircleEllipseArePaths) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<rect x=\"10\" y=\"20\" width=\"40\" height=\"30\" fill=\"#00ff00\"/>"
        "<circle cx=\"50\" cy=\"50\" r=\"10\" fill=\"#000000\"/>"
        "<ellipse cx=\"80\" cy=\"40\" rx=\"20\" ry=\"10\" fill=\"#000000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().layers.size(), 4u);
    auto *rectContent =
        static_cast<motion::ShapeContent *>(result.value().layers[1]->content.get());
    ASSERT_EQ(rectContent->geometry->type(), motion::ShapeType::Rect);
    auto *rect = static_cast<motion::ShapeRect *>(rectContent->geometry.get());
    EXPECT_NEAR(rect->position.staticValue().x, 30.f, 1e-3f);
    EXPECT_NEAR(rect->position.staticValue().y, 35.f, 1e-3f);
    EXPECT_NEAR(rect->size.staticValue().x, 40.f, 1e-3f);
    EXPECT_NEAR(rect->size.staticValue().y, 30.f, 1e-3f);
    EXPECT_NEAR(rect->cornerRadius.staticValue(), 0.f, 1e-3f);
    auto *circleContent =
        static_cast<motion::ShapeContent *>(result.value().layers[2]->content.get());
    EXPECT_EQ(circleContent->geometry->type(), motion::ShapeType::Path);
    auto *ellipseContent =
        static_cast<motion::ShapeContent *>(result.value().layers[3]->content.get());
    EXPECT_EQ(ellipseContent->geometry->type(), motion::ShapeType::Path);
}

TEST(SvgImporterTest, UniformRoundedRectBecomesShapeRect) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" rx=\"8\" fill=\"#000000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    auto *content = static_cast<motion::ShapeContent *>(result.value().layers[1]->content.get());
    ASSERT_EQ(content->geometry->type(), motion::ShapeType::Rect);
    auto *rect = static_cast<motion::ShapeRect *>(content->geometry.get());
    EXPECT_NEAR(rect->position.staticValue().x, 20.f, 1e-3f);
    EXPECT_NEAR(rect->position.staticValue().y, 15.f, 1e-3f);
    EXPECT_NEAR(rect->size.staticValue().x, 40.f, 1e-3f);
    EXPECT_NEAR(rect->size.staticValue().y, 30.f, 1e-3f);
    EXPECT_NEAR(rect->cornerRadius.staticValue(), 8.f, 1e-3f);
    EXPECT_TRUE(result.value().layers[1]->masks.empty());
}

TEST(SvgImporterTest, UnequalRadiusRectStaysPath) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" rx=\"8\" ry=\"4\" fill=\"#000000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    auto *content = static_cast<motion::ShapeContent *>(result.value().layers[1]->content.get());
    ASSERT_EQ(content->geometry->type(), motion::ShapeType::Path);
    EXPECT_TRUE(result.value().layers[1]->masks.empty());
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
    EXPECT_EQ(imported.value().rootLayerId, host->layers.back()->id);

    undo.undo(document);
    EXPECT_TRUE(host->layers.empty());
    EXPECT_EQ(host->width, 640);
}

TEST(SvgImporterTest, ImportedGroupSitsAboveChildren) {
    motion::Document document;
    auto composition = std::make_unique<motion::Composition>();
    composition->duration = 90;
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

    int rootIndex = -1;
    int childIndex = -1;
    for (int index = 0; index < static_cast<int>(host->layers.size()); ++index) {
        if (host->layers[static_cast<size_t>(index)]->id == imported.value().rootLayerId) {
            rootIndex = index;
        } else if (host->layers[static_cast<size_t>(index)]->parentId ==
                   imported.value().rootLayerId) {
            childIndex = index;
        }
    }
    ASSERT_GE(rootIndex, 0);
    ASSERT_GE(childIndex, 0);
    EXPECT_GT(rootIndex, childIndex);
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

TEST(SvgImporterTest, StrokeDashArrayMapsToDashedStroke) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" stroke=\"#000\" stroke-dasharray=\"2 2\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    for (const auto &d : result.value().diagnostics) {
        EXPECT_NE(d.code, "stroke.dash");
    }
    const motion::Layer *shape = result.value().layers.back().get();
    ASSERT_FALSE(shape->styles.empty());
    auto *stroke = static_cast<motion::StrokeStyle *>(shape->styles.back().get());
    EXPECT_EQ(stroke->strokeMode, motion::StrokeMode::Dashed);
    ASSERT_EQ(stroke->dashes.size(), 2u);
    EXPECT_FLOAT_EQ(stroke->dashes[0], 2.0f);
    EXPECT_FLOAT_EQ(stroke->dashes[1], 2.0f);
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

TEST(SvgImporterTest, DataUriImageCreatesAsset) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"data:image/png;base64,"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\""
        " width=\"16\" height=\"16\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool hasImage = false;
    for (const auto &layer : result.value().layers) {
        if (layer->type() == motion::LayerType::Image) {
            hasImage = true;
        }
    }
    EXPECT_TRUE(hasImage);
    EXPECT_FALSE(result.value().assets.empty());
    EXPECT_FALSE(result.value().embeddedImages.empty());
}

TEST(SvgImporterTest, ExternalImageIsSkipped) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"photo.png\" width=\"16\" height=\"16\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "image.external") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

constexpr const char *kOnePixelPng =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

bool HasDiagnostic(const motion::svg::SvgLayerTree &tree, const std::string &code) {
    for (const auto &diagnostic : tree.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

const motion::Layer *FindLayerByType(const motion::svg::SvgLayerTree &tree, motion::LayerType type) {
    for (const auto &layer : tree.layers) {
        if (layer->type() == type) {
            return layer.get();
        }
    }
    return nullptr;
}

TEST(SvgImporterTest, PatternImageFillCreatesImageLayer) {
    const std::string svg =
        std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
                    "<defs><pattern id=\"p\" patternContentUnits=\"objectBoundingBox\" "
                    "width=\"1\" height=\"1\">"
                    "<image href=\"data:image/png;base64,") +
        kOnePixelPng +
        "\" width=\"1\" height=\"1\"/>"
        "</pattern></defs>"
        "<rect id=\"photo\" x=\"0\" y=\"0\" width=\"20\" height=\"10\" fill=\"url(#p)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *image = FindLayerByType(result.value(), motion::LayerType::Image);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->name, "photo");
    auto *content = static_cast<motion::ImageContent *>(image->content.get());
    EXPECT_NEAR(content->size.staticValue().x, 20.f, 1e-3f);
    EXPECT_NEAR(content->size.staticValue().y, 10.f, 1e-3f);
    EXPECT_EQ(content->scaleMode, motion::ImageScaleMode::Stretch);
    EXPECT_TRUE(image->masks.empty());
    EXPECT_FALSE(result.value().assets.empty());
    EXPECT_FALSE(result.value().embeddedImages.empty());
    EXPECT_FALSE(HasDiagnostic(result.value(), "paint.unresolved"));
}

TEST(SvgImporterTest, PatternRoundedRectUsesImageCornerRadius) {
    const std::string svg =
        std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                    "width=\"200\" height=\"200\">"
                    "<defs>"
                    "<pattern id=\"p\" patternContentUnits=\"objectBoundingBox\" width=\"1\" height=\"1\">"
                    "<use xlink:href=\"#img\" transform=\"scale(0.5 0.5)\"/>"
                    "</pattern>"
                    "<image id=\"img\" width=\"2\" height=\"2\" preserveAspectRatio=\"none\" "
                    "xlink:href=\"data:image/png;base64,") +
        kOnePixelPng +
        "\"/>"
        "</defs>"
        "<rect id=\"fg\" x=\"10\" y=\"10\" width=\"100\" height=\"100\" rx=\"8\" fill=\"url(#p)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *image = FindLayerByType(result.value(), motion::LayerType::Image);
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(image->masks.empty());
    auto *content = static_cast<motion::ImageContent *>(image->content.get());
    EXPECT_NEAR(content->size.staticValue().x, 100.f, 1e-3f);
    EXPECT_NEAR(content->size.staticValue().y, 100.f, 1e-3f);
    EXPECT_NEAR(content->cornerRadius.staticValue(), 8.f, 1e-3f);
    EXPECT_EQ(content->scaleMode, motion::ImageScaleMode::Stretch);
    EXPECT_NEAR(image->transform.scale.staticValue().x, 1.f, 1e-3f);
    EXPECT_NEAR(image->transform.scale.staticValue().y, 1.f, 1e-3f);
}

TEST(SvgImporterTest, PatternOffsetImageUsesHostRectSize) {
    const std::string svg =
        std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
                    "<defs><pattern id=\"p\" patternContentUnits=\"objectBoundingBox\" "
                    "width=\"1\" height=\"1\">"
                    "<image href=\"data:image/png;base64,") +
        kOnePixelPng +
        "\" width=\"2\" height=\"1\" transform=\"translate(-0.5 0)\"/>"
        "</pattern></defs>"
        "<rect width=\"20\" height=\"10\" fill=\"url(#p)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *image = FindLayerByType(result.value(), motion::LayerType::Image);
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(image->masks.empty());
    auto *content = static_cast<motion::ImageContent *>(image->content.get());
    EXPECT_NEAR(content->size.staticValue().x, 20.f, 1e-3f);
    EXPECT_NEAR(content->size.staticValue().y, 10.f, 1e-3f);
    EXPECT_NEAR(content->cornerRadius.staticValue(), 0.f, 1e-3f);
    EXPECT_EQ(content->scaleMode, motion::ImageScaleMode::Stretch);
}

TEST(SvgImporterTest, PatternUseImageFillCreatesImageLayer) {
    const std::string svg =
        std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                    "width=\"20\" height=\"10\">"
                    "<defs>"
                    "<pattern id=\"p\" patternContentUnits=\"objectBoundingBox\" width=\"1\" height=\"1\">"
                    "<use xlink:href=\"#img\"/>"
                    "</pattern>"
                    "<image id=\"img\" width=\"1\" height=\"1\" xlink:href=\"data:image/png;base64,") +
        kOnePixelPng +
        "\"/>"
        "</defs>"
        "<rect width=\"20\" height=\"10\" fill=\"url(#p)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(FindLayerByType(result.value(), motion::LayerType::Image), nullptr);
    EXPECT_FALSE(HasDiagnostic(result.value(), "paint.unresolved"));
}

TEST(SvgImporterTest, TiledPatternFillStaysUnresolved) {
    const std::string svg =
        std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
                    "<defs><pattern id=\"p\" patternContentUnits=\"objectBoundingBox\" "
                    "width=\"0.5\" height=\"1\">"
                    "<image href=\"data:image/png;base64,") +
        kOnePixelPng +
        "\" width=\"1\" height=\"1\"/>"
        "</pattern></defs>"
        "<rect width=\"20\" height=\"10\" fill=\"url(#p)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(FindLayerByType(result.value(), motion::LayerType::Image), nullptr);
    EXPECT_TRUE(HasDiagnostic(result.value(), "paint.unresolved"));
}

TEST(SvgImporterTest, SimpleClipPathBecomesMask) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><clipPath id=\"c\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/></clipPath></defs>"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" fill=\"#000\" clip-path=\"url(#c)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.back()->masks.size(), 1u);
}

TEST(SvgImporterTest, RoundedGroupClipUsesCornerRadius) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
        "<defs><clipPath id=\"c\"><rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"8\"/></clipPath></defs>"
        "<g clip-path=\"url(#c)\"><rect width=\"40\" height=\"40\" fill=\"#000\"/></g>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *group = nullptr;
    for (const auto &layer : result.value().layers) {
        if (layer->type() == motion::LayerType::Group && layer->name != "SVG") {
            group = layer.get();
        }
    }
    ASSERT_NE(group, nullptr);
    EXPECT_TRUE(group->masks.empty());
    auto *content = static_cast<motion::NullContent *>(group->content.get());
    EXPECT_NEAR(content->cornerRadius.staticValue(), 8.f, 1e-3f);
}

TEST(SvgImporterTest, MaskAttributeBecomesMask) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><mask id=\"m\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#fff\"/></mask></defs>"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" fill=\"#000\" mask=\"url(#m)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.back()->masks.size(), 1u);
    EXPECT_FALSE(HasDiagnostic(result.value(), "mask.skipped"));
}

TEST(SvgImporterTest, ComplexMaskAttributeIsSkipped) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><mask id=\"m\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#fff\"/>"
        "<rect x=\"5\" y=\"5\" width=\"10\" height=\"10\" fill=\"#fff\"/>"
        "</mask></defs>"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" fill=\"#000\" mask=\"url(#m)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(HasDiagnostic(result.value(), "mask.skipped"));
    EXPECT_TRUE(result.value().layers.back()->masks.empty());
}

TEST(SvgImporterTest, MaskOnGroupBecomesMask) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><mask id=\"m\" maskUnits=\"userSpaceOnUse\">"
        "<path d=\"M0 0H20V20H0Z\" fill=\"#fff\"/>"
        "</mask></defs>"
        "<g mask=\"url(#m)\"><rect width=\"20\" height=\"20\" fill=\"#000\"/></g>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &layer : result.value().layers) {
        if (layer->type() == motion::LayerType::Group && layer->masks.size() == 1u) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_FALSE(HasDiagnostic(result.value(), "mask.skipped"));
}

TEST(SvgImporterTest, FilterAttributeIsSkipped) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><filter id=\"f\"><feGaussianBlur stdDeviation=\"2\"/></filter></defs>"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" fill=\"#000\" filter=\"url(#f)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "filter.skipped") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(SvgImporterTest, ComplexClipPathIsUnsupported) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><clipPath id=\"c\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/>"
        "<circle cx=\"5\" cy=\"5\" r=\"4\"/>"
        "</clipPath></defs>"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" fill=\"#000\" clip-path=\"url(#c)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "clip.unsupported") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(SvgImporterTest, TextBecomesPointTextLayer) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"20\" y=\"40\" font-size=\"24\" fill=\"#333333\">hello</text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().layers.size(), 2u);
    const motion::Layer *text = result.value().layers[1].get();
    EXPECT_EQ(text->type(), motion::LayerType::Text);
    auto *content = static_cast<motion::TextContent *>(text->content.get());
    EXPECT_EQ(content->text.staticValue(), "hello");
    EXPECT_FALSE(content->boxTextMode);
    EXPECT_NEAR(content->fontSize, 24.f, 1e-3f);
    EXPECT_EQ(content->align, motion::TextAlign::Left);
    ASSERT_FALSE(text->styles.empty());
    EXPECT_EQ(text->styles[0]->type(), motion::LayerStyleType::Fill);
}

TEST(SvgImporterTest, TextAnchorMiddleMapsToCenter) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"100\" y=\"50\" text-anchor=\"middle\" font-size=\"16\">ab</text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_GE(result.value().layers.size(), 2u);
    auto *content = static_cast<motion::TextContent *>(result.value().layers[1]->content.get());
    EXPECT_EQ(content->align, motion::TextAlign::Center);
}

TEST(SvgImporterTest, SameStyleTspanConcatenates) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"0\" y=\"20\" font-size=\"16\">foo<tspan>bar</tspan></text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.size(), 2u);
    auto *content = static_cast<motion::TextContent *>(result.value().layers[1]->content.get());
    EXPECT_EQ(content->text.staticValue(), "foobar");
}

TEST(SvgImporterTest, DifferentFillTspanSplitsLayer) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"0\" y=\"20\" font-size=\"16\" fill=\"#000000\">"
        "ab<tspan fill=\"#ff0000\">cd</tspan></text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    int textCount = 0;
    for (const auto &layer : result.value().layers) {
        if (layer->type() == motion::LayerType::Text) {
            textCount += 1;
        }
    }
    EXPECT_EQ(textCount, 2);
}

TEST(SvgImporterTest, TextPathIsSkipped) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><path id=\"p\" d=\"M0 50 H100\"/></defs>"
        "<text><textPath href=\"#p\">curve</textPath></text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "textPath.skipped") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    for (const auto &layer : result.value().layers) {
        EXPECT_NE(layer->type(), motion::LayerType::Text);
    }
}
