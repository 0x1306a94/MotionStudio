#include <gtest/gtest.h>

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"

using motion::Asset;
using motion::ImageContent;
using motion::ImageScaleMode;

TEST(ImageContentTest, DefaultsMatchSpec) {
    ImageContent content;
    EXPECT_FALSE(content.assetId.isValid());
    EXPECT_FALSE(content.size.isAnimated());
    EXPECT_FLOAT_EQ(content.size.staticValue().x, 200.0f);
    EXPECT_FLOAT_EQ(content.size.staticValue().y, 200.0f);
    EXPECT_EQ(content.scaleMode, ImageScaleMode::LetterBox);
}

TEST(AssetTest, DefaultSizeZero) {
    Asset asset;
    EXPECT_EQ(asset.width, 0);
    EXPECT_EQ(asset.height, 0);
}
