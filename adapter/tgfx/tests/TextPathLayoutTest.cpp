#include <cmath>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "TextPathLayout.h"

using motion::BezierPath;
using motion::LayoutTextOnPath;
using motion::TextAlign;
using motion::TextPathLayoutInput;
using motion::TextPathLayoutResult;
using motion::Vec2;

namespace {

BezierPath MakeHorizontalPath(float length = 100.0f) {
    BezierPath path;
    path.closed = false;
    path.vertices.push_back({{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    path.vertices.push_back({{length, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    return path;
}

TextPathLayoutInput MakeInput(const std::string &text) {
    TextPathLayoutInput input;
    input.text = text;
    input.fontSize = 24.0f;
    input.align = TextAlign::Left;
    input.fontFamily = "Helvetica";
    input.path = MakeHorizontalPath();
    input.perpendicular = true;
    return input;
}

float TranslationX(const motion::Mat3 &matrix) {
    return matrix.values[2];
}

float TranslationY(const motion::Mat3 &matrix) {
    return matrix.values[5];
}

float RotationDegrees(const motion::Mat3 &matrix) {
    return std::atan2(matrix.values[3], matrix.values[0]) * (180.0f / 3.14159265358979323846f);
}

}  // namespace

TEST(TextPathLayoutTest, HorizontalPathPlacesGlyphsWithIncreasingTranslation) {
    const TextPathLayoutResult result = LayoutTextOnPath(MakeInput("AB"));
    ASSERT_EQ(result.glyphs.size(), 2u);
    EXPECT_GT(TranslationX(result.glyphs[1].matrix), TranslationX(result.glyphs[0].matrix));
    EXPECT_NEAR(TranslationY(result.glyphs[0].matrix), 0.0f, 0.5f);
    EXPECT_NEAR(TranslationY(result.glyphs[1].matrix), 0.0f, 0.5f);
}

TEST(TextPathLayoutTest, PerpendicularOnHorizontalPathHasNearZeroRotation) {
    const TextPathLayoutResult result = LayoutTextOnPath(MakeInput("AB"));
    ASSERT_EQ(result.glyphs.size(), 2u);
    EXPECT_NEAR(RotationDegrees(result.glyphs[0].matrix), 0.0f, 1.0f);
    EXPECT_NEAR(RotationDegrees(result.glyphs[1].matrix), 0.0f, 1.0f);
}

TEST(TextPathLayoutTest, EmptyPathYieldsNoGlyphs) {
    TextPathLayoutInput input = MakeInput("AB");
    input.path = {};
    const TextPathLayoutResult result = LayoutTextOnPath(input);
    EXPECT_TRUE(result.glyphs.empty());
}
