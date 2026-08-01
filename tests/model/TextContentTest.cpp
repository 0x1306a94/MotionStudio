#include <gtest/gtest.h>

#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"

using motion::TextAlign;
using motion::TextContent;

TEST(TextContentTest, DefaultsMatchSpec) {
    TextContent content;
    EXPECT_EQ(content.text.staticValue(), "Text");
    EXPECT_EQ(content.fontFamily, "PingFang SC");
    EXPECT_EQ(content.fontStyle, "");
    EXPECT_FLOAT_EQ(content.fontSize, 48.0f);
    EXPECT_FLOAT_EQ(content.size.x, 400.0f);
    EXPECT_FLOAT_EQ(content.size.y, 120.0f);
    EXPECT_FALSE(content.boxTextMode);
    EXPECT_EQ(content.align, TextAlign::Left);
}
