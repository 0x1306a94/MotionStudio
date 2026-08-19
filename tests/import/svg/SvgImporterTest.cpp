#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/import/svg/SvgImporter.h"

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
