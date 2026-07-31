#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "pag/file.h"

namespace {

std::shared_ptr<pag::File> MakeMinimalSolidFile() {
    auto *composition = new pag::VectorComposition();
    composition->id = 1;
    composition->width = 100;
    composition->height = 100;
    composition->duration = 30;
    composition->frameRate = 30.0f;

    auto *layer = new pag::SolidLayer();
    layer->id = 1;
    layer->containingComposition = composition;
    layer->startTime = 0;
    layer->duration = composition->duration;
    layer->transform = pag::Transform2D::MakeDefault().release();
    layer->width = composition->width;
    layer->height = composition->height;
    layer->solidColor = pag::Red;
    composition->layers.push_back(layer);

    return pag::Codec::VerifyAndMake({composition}, {});
}

}  // namespace

TEST(PagCodecTest, EncodeDecodeRoundTrip) {
    auto file = MakeMinimalSolidFile();
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->width(), 100);
    EXPECT_EQ(file->height(), 100);

    auto encoded = pag::Codec::Encode(file);
    ASSERT_NE(encoded, nullptr);
    ASSERT_GT(encoded->length(), 0u);

    auto decoded =
        pag::Codec::Decode(encoded->data(), static_cast<uint32_t>(encoded->length()), "");
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decoded->width(), 100);
    EXPECT_EQ(decoded->height(), 100);
    EXPECT_EQ(decoded->frameRate(), 30.0f);
    EXPECT_EQ(decoded->duration(), 30);
}
