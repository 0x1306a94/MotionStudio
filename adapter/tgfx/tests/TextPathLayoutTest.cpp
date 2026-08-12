#include <cmath>

#include <gtest/gtest.h>

#include <cstdlib>

#include "MeasurePointTextSize.h"
#include "MeasureTextPathBounds.h"
#include "MotionStudio/common/BezierPath.h"
#include "TextPathLayout.h"

using motion::BezierPath;
using motion::LayoutTextOnPath;
using motion::MakeSingleContour;
using motion::MeasurePointTextSize;
using motion::MeasureTextPathBounds;
using motion::TextAlign;
using motion::TextPathBounds;
using motion::TextPathLayoutInput;
using motion::TextPathLayoutResult;
using motion::Vec2;

namespace {

BezierPath MakeHorizontalPath(float length = 100.0f) {
    BezierPath path = MakeSingleContour({{{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}, {{length, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}}, false);
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

TEST(TextPathLayoutTest, MeasurePointTextSizeIsStableAcrossCalls) {
    const Vec2 first = MeasurePointTextSize("CacheMe", 24.0f, TextAlign::Left, "Helvetica", "");
    const Vec2 second = MeasurePointTextSize("CacheMe", 24.0f, TextAlign::Left, "Helvetica", "");
    EXPECT_FLOAT_EQ(first.x, second.x);
    EXPECT_FLOAT_EQ(first.y, second.y);
    EXPECT_GE(first.x, 1.0f);
    EXPECT_GE(first.y, 1.0f);
}

TEST(TextPathLayoutTest, ArcPathBoundsDifferFromPointTextBox) {
    BezierPath arc = MakeSingleContour({{{0.0f, 100.0f}, {0.0f, 0.0f}, {55.0f, 0.0f}},
                                        {{100.0f, 0.0f}, {0.0f, 55.0f}, {0.0f, 0.0f}}},
                                       false);

    const TextPathBounds pathBounds =
        MeasureTextPathBounds("Hello", 24.0f, TextAlign::Left, "Helvetica", "", arc, false, true,
                              false, 0.0f, 0.0f);
    const Vec2 pointSize =
        MeasurePointTextSize("Hello", 24.0f, TextAlign::Left, "Helvetica", "");
    EXPECT_GT(pathBounds.max.x - pathBounds.min.x, 1.0f);
    EXPECT_GT(pathBounds.max.y - pathBounds.min.y, 1.0f);
    // Curved layout should not match the axis-aligned point-text box origin..size.
    const bool sameBox = std::fabs(pathBounds.min.x) < 0.5f && std::fabs(pathBounds.min.y) < 0.5f &&
        std::fabs((pathBounds.max.x - pathBounds.min.x) - pointSize.x) < 1.0f &&
        std::fabs((pathBounds.max.y - pathBounds.min.y) - pointSize.y) < 1.0f;
    EXPECT_FALSE(sameBox);
}

TEST(TextPathLayoutTest, HorizontalPathBaselineNearBoundsBottom) {
    const TextPathLayoutResult result = LayoutTextOnPath(MakeInput("Hello"));
    ASSERT_FALSE(result.glyphs.empty());
    // Path at y=0; baseline origin ty~0; glyph body mostly above (negative Y).
    EXPECT_NEAR(TranslationY(result.glyphs[0].matrix), 0.0f, 0.5f);
    EXPECT_LT(result.boundsMin.y, -1.0f);
    const float midY = 0.5f * (result.boundsMin.y + result.boundsMax.y);
    EXPECT_LT(std::fabs(0.0f - result.boundsMax.y), std::fabs(0.0f - midY));
}

TEST(TextPathLayoutTest, VerticalPathKeepsBaselineCenterOnPath) {
    BezierPath vertical = MakeSingleContour({{{50.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}, {{50.0f, 200.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}}, false);
    TextPathLayoutInput input = MakeInput("AB");
    input.path = vertical;
    const TextPathLayoutResult result = LayoutTextOnPath(input);
    ASSERT_EQ(result.glyphs.size(), 2u);

    // Local baseline mid (halfWidth, 0) must land on the path x=50 (not shifted by halfWidth).
    for (const motion::TextPathGlyph &glyph : result.glyphs) {
        const float halfWidth = glyph.advance * 0.5f;
        const Vec2 onPath = glyph.matrix.transformPoint({halfWidth, 0.0f});
        EXPECT_NEAR(onPath.x, 50.0f, 1.0f) << "baseline center must stay on the path";
    }
}
