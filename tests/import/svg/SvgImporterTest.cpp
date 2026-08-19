#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/import/svg/SvgImporter.h"
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
