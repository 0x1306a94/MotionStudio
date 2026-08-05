#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/MaskPathBake.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BakeMaskPathFromLayer;
using motion::BezierPath;
using motion::Composition;
using motion::Document;
using motion::Layer;
using motion::LayerType;
using motion::MakeEllipseGeometry;
using motion::MakeRectGeometry;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapeGeometryToBezierPath;
using motion::ShapeRect;
using motion::Vec2;

TEST(MaskPathBakeTest, BakesRectGeometryAtPlayhead) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto rect = std::make_unique<ShapeRect>();
    rect->size.setStaticValue(Vec2{120, 80});
    content->geometry = std::move(rect);

    const BezierPath baked = BakeMaskPathFromLayer(*layer, 0);
    const BezierPath expected =
        ShapeGeometryToBezierPath(MakeRectGeometry(Vec2{0, 0}, Vec2{120, 80}, 0));
    EXPECT_EQ(baked, expected);
}

TEST(MaskPathBakeTest, BakesEllipseGeometry) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto ellipse = std::make_unique<ShapeEllipse>();
    ellipse->size.setStaticValue(Vec2{200, 200});
    content->geometry = std::move(ellipse);

    const BezierPath baked = BakeMaskPathFromLayer(*layer, 0);
    const BezierPath expected =
        ShapeGeometryToBezierPath(MakeEllipseGeometry(Vec2{0, 0}, Vec2{200, 200}));
    EXPECT_EQ(baked, expected);
    EXPECT_EQ(baked.contours[0].vertices.size(), 4u);
}

TEST(MaskPathBakeTest, FallsBackWhenGeometryMissing) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));

    const BezierPath baked = BakeMaskPathFromLayer(*layer, 0);
    EXPECT_TRUE(baked.contours[0].closed);
    ASSERT_EQ(baked.contours[0].vertices.size(), 4u);
    EXPECT_EQ(baked.contours[0].vertices[0].point, (Vec2{-100, -100}));
    EXPECT_EQ(baked.contours[0].vertices[2].point, (Vec2{100, 100}));
}
