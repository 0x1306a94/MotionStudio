#include "PagAlmostStatic.h"

#include <gtest/gtest.h>

#include <memory>

#include "FakeBitmapFrameSource.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

using motion::Composition;
using motion::Document;
using motion::FakeBitmapFrameSource;
using motion::TimeRange;
using motion::pag_export::IsAlmostStaticSequence;

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

TEST(PagAlmostStaticTest, SolidFakeIsStatic) {
    Document document = MakeDoc(16, 16, 4);
    FakeBitmapFrameSource source(10, 20, 30, 255);
    TimeRange range{0, 4};
    ASSERT_TRUE(source.prepareComposition(document, document.compositions[0]->id, range, 16, 16)
                    .hasValue());
    EXPECT_TRUE(IsAlmostStaticSequence(&source, 0, 4));
}

TEST(PagAlmostStaticTest, TintChangeIsNotStatic) {
    Document document = MakeDoc(16, 16, 2);
    FakeBitmapFrameSource source(255, 0, 0, 255);
    source.setFrameColor(0, 255, 0, 0, 255);
    source.setFrameColor(1, 0, 0, 255, 255);
    TimeRange range{0, 2};
    ASSERT_TRUE(source.prepareComposition(document, document.compositions[0]->id, range, 16, 16)
                    .hasValue());
    EXPECT_FALSE(IsAlmostStaticSequence(&source, 0, 2));
}
