#include "PagBitmapSequenceEncode.h"

#include <gtest/gtest.h>

#include <memory>

#include "FakeBitmapFrameSource.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

using motion::Composition;
using motion::Document;
using motion::FakeBitmapFrameSource;
using motion::TimeRange;
using motion::pag_export::EncodeBitmapSequence;

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

TEST(PagBitmapSequenceEncodeTest, CancelFlagAbortsWithCancelledMessage) {
    Document document = MakeDoc(32, 32, 4);
    FakeBitmapFrameSource source;
    TimeRange range{0, 4};
    ASSERT_TRUE(source.prepareComposition(document, document.compositions[0]->id, range, 32, 32)
                    .hasValue());

    auto *composition = new pag::BitmapComposition();
    auto *sequence = new pag::BitmapSequence();
    sequence->width = 32;
    sequence->height = 32;
    sequence->frameRate = 30;
    sequence->composition = composition;
    composition->sequences.push_back(sequence);

    volatile int cancelFlag = 1;
    auto encoded = EncodeBitmapSequence(&source, sequence, 0, 4, 32, 32, 60, 80, &cancelFlag);
    ASSERT_FALSE(encoded.hasValue());
    EXPECT_EQ(encoded.error().message, "cancelled");

    delete composition;
}
