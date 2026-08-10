#include "PagVideoSequenceEncode.h"

#include <gtest/gtest.h>

#include <memory>

#include "FakeBitmapFrameSource.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

using motion::Composition;
using motion::Document;
using motion::FakeBitmapFrameSource;
using motion::TimeRange;
using motion::pag_export::EncodeVideoSequence;

namespace {

Document MakeDoc(int width, int height, int64_t duration) {
    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = width;
    composition->height = height;
    composition->duration = duration;
    composition->frameRate = {30, 1};
    document.addComposition(std::move(composition));
    return document;
}

}  // namespace

#if defined(__APPLE__)

TEST(PagVideoSequenceEncodeTest, EncodesSideBySideAlphaHeaders) {
    Document document = MakeDoc(64, 64, 2);
    FakeBitmapFrameSource source(255, 0, 0, 200);
    source.setFrameColor(0, 255, 0, 0, 200);
    source.setFrameColor(1, 0, 255, 0, 128);
    TimeRange range{0, 2};
    ASSERT_TRUE(source.prepareComposition(document, document.compositions[0]->id, range, 64, 64)
                    .hasValue());

    auto *composition = new pag::VideoComposition();
    auto *sequence = new pag::VideoSequence();
    sequence->width = 64;
    sequence->height = 64;
    sequence->frameRate = 30;
    sequence->composition = composition;
    composition->sequences.push_back(sequence);
    composition->width = 64;
    composition->height = 64;
    composition->duration = 2;
    composition->frameRate = 30;

    auto encoded = EncodeVideoSequence(&source, sequence, 0, 2, 64, 64, 60, 80);
    ASSERT_TRUE(encoded.hasValue()) << encoded.error().message;
    EXPECT_EQ(sequence->alphaStartX, 64);
    EXPECT_EQ(sequence->alphaStartY, 0);
    EXPECT_EQ(sequence->width, 64);
    EXPECT_EQ(sequence->height, 64);
    ASSERT_GE(sequence->headers.size(), 2u);
    EXPECT_FALSE(sequence->headers[0]->length() < 5);
    EXPECT_EQ(sequence->headers[0]->data()[0], 0);
    EXPECT_EQ(sequence->headers[0]->data()[1], 0);
    EXPECT_EQ(sequence->headers[0]->data()[2], 0);
    EXPECT_EQ(sequence->headers[0]->data()[3], 1);
    ASSERT_EQ(sequence->frames.size(), 2u);
    EXPECT_EQ(sequence->frames[0]->frame, 0);
    EXPECT_EQ(sequence->frames[1]->frame, 1);
    EXPECT_NE(sequence->frames[0]->fileBytes, nullptr);
    EXPECT_GT(sequence->frames[0]->fileBytes->length(), 4u);

    delete composition;
}

TEST(PagVideoSequenceEncodeTest, CancelFlagAbortsWithCancelledMessage) {
    Document document = MakeDoc(64, 64, 4);
    FakeBitmapFrameSource source;
    TimeRange range{0, 4};
    ASSERT_TRUE(source.prepareComposition(document, document.compositions[0]->id, range, 64, 64)
                    .hasValue());

    auto *composition = new pag::VideoComposition();
    auto *sequence = new pag::VideoSequence();
    sequence->width = 64;
    sequence->height = 64;
    sequence->frameRate = 30;
    sequence->composition = composition;
    composition->sequences.push_back(sequence);

    volatile int cancelFlag = 1;
    auto encoded = EncodeVideoSequence(&source, sequence, 0, 4, 64, 64, 60, 80, &cancelFlag);
    ASSERT_FALSE(encoded.hasValue());
    EXPECT_EQ(encoded.error().message, "cancelled");

    delete composition;
}

#endif
