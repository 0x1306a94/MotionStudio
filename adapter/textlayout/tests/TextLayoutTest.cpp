#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/textlayout/GlyphMetrics.h"
#include "MotionStudio/textlayout/TextLayout.h"

using motion::textlayout::Align;
using motion::textlayout::FontMetrics;
using motion::textlayout::GlyphMetrics;
using motion::textlayout::LayoutText;
using motion::textlayout::TextLayoutInput;
using motion::textlayout::TextLayoutResult;

namespace {

class FakeGlyphMetrics : public GlyphMetrics {
  public:
    FontMetrics metrics(float fontSize) const override {
        FontMetrics result;
        result.ascent = fontSize * 0.8f;
        result.descent = fontSize * 0.2f;
        result.leading = 0.0f;
        return result;
    }

    float advance(uint32_t /*unichar*/, float fontSize) const override {
        return fontSize * 0.5f;
    }
};

TextLayoutInput MakeInput(const std::string &text, float boxWidth, float fontSize = 20.0f) {
    TextLayoutInput input;
    input.text = text;
    input.boxWidth = boxWidth;
    input.fontSize = fontSize;
    return input;
}

}  // namespace

TEST(TextLayoutTest, SingleLine) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("abcd", 100.0f, 20.0f);
    input.metrics = &metrics;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_FLOAT_EQ(result.appliedFontSize, 20.0f);
    EXPECT_EQ(result.lines[0].text, "abcd");
    EXPECT_FLOAT_EQ(result.lines[0].width, 40.0f);  // 4 * 10
    EXPECT_FLOAT_EQ(result.lines[0].x, 0.0f);
    EXPECT_FLOAT_EQ(result.measuredSize.y, 20.0f);  // ascent+descent
}

TEST(TextLayoutTest, HardLineBreak) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("a\nb", 100.0f, 20.0f);
    input.metrics = &metrics;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[0].text, "a");
    EXPECT_EQ(result.lines[1].text, "b");
    EXPECT_FLOAT_EQ(result.measuredSize.y, 40.0f);
}

TEST(TextLayoutTest, SoftWrapByWidth) {
    FakeGlyphMetrics metrics;
    // Each glyph = 10px at size 20; box fits 3 glyphs.
    TextLayoutInput input = MakeInput("abcdef", 30.0f, 20.0f);
    input.metrics = &metrics;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[0].text, "abc");
    EXPECT_EQ(result.lines[1].text, "def");
}

TEST(TextLayoutTest, SoftWrapPrefersWhitespace) {
    FakeGlyphMetrics metrics;
    // "aa bb" — each char 10px; width 35 fits "aa " then wraps before bb ideally.
    TextLayoutInput input = MakeInput("aa bb", 35.0f, 20.0f);
    input.metrics = &metrics;

    TextLayoutResult result = LayoutText(input);
    ASSERT_GE(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[0].text, "aa");
    EXPECT_EQ(result.lines[1].text, "bb");
}

TEST(TextLayoutTest, CenterAlign) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("ab", 100.0f, 20.0f);
    input.metrics = &metrics;
    input.align = Align::Center;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_FLOAT_EQ(result.lines[0].x, 40.0f);  // (100-20)/2
}

TEST(TextLayoutTest, ShrinkToFitReducesFont) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("abcdefgh", 40.0f, 40.0f);
    input.metrics = &metrics;
    input.boxHeight = 20.0f;  // one line of size-20 metrics height
    input.shrinkToFit = true;

    TextLayoutResult result = LayoutText(input);
    EXPECT_LT(result.appliedFontSize, 40.0f);
    EXPECT_FLOAT_EQ(result.measuredSize.y, 20.0f);
    EXPECT_LE(result.measuredSize.y, 20.01f);
}

TEST(TextLayoutTest, WithoutShrinkKeepsFontSize) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("abcdefgh", 40.0f, 40.0f);
    input.metrics = &metrics;
    input.boxHeight = 20.0f;
    input.shrinkToFit = false;

    TextLayoutResult result = LayoutText(input);
    EXPECT_FLOAT_EQ(result.appliedFontSize, 40.0f);
}

TEST(TextLayoutTest, PointTextNoSoftWrapMeasuresContent) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("abcdef", 30.0f, 20.0f);  // 30 would soft-wrap
    input.metrics = &metrics;
    input.softWrap = false;
    input.align = Align::Left;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.lines[0].text, "abcdef");
    EXPECT_FLOAT_EQ(result.measuredSize.x, 60.0f);  // 6 * 10
    EXPECT_FLOAT_EQ(result.measuredSize.y, 20.0f);
}

TEST(TextLayoutTest, PointTextHardBreakAndCenter) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("aa\nbbbb", 10.0f, 20.0f);
    input.metrics = &metrics;
    input.softWrap = false;
    input.align = Align::Center;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_FLOAT_EQ(result.measuredSize.x, 40.0f);  // longer line "bbbb"
    EXPECT_FLOAT_EQ(result.lines[0].x, 10.0f);      // (40-20)/2
    EXPECT_FLOAT_EQ(result.lines[1].x, 0.0f);
}
