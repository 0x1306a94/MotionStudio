#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EnsureDefaultGradient;
using motion::Expected;
using motion::FillStyle;
using motion::GradientType;
using motion::Layer;
using motion::LayerType;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::StylePaintMode;
using motion::Vec2;

namespace {

struct GradientFillScene {
    Document document;
    Composition *composition = nullptr;
    Layer *layer = nullptr;
    FillStyle *fill = nullptr;

    GradientFillScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;
        composition->frameRate = {30, 1};
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = 100;
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        auto rect = std::make_unique<ShapeRect>();
        rect->position.setStaticValue(Vec2{50, 50});
        rect->size.setStaticValue(Vec2{40, 20});
        content->geometry = std::move(rect);

        auto fillElement = std::make_unique<FillStyle>();
        fill = fillElement.get();
        fill->paintMode = StylePaintMode::Gradient;
        EnsureDefaultGradient(fill->gradient, Vec2{0, 0}, Vec2{100, 0});
        fill->gradient.type = GradientType::Linear;
        fill->color.setStaticValue(Color{1, 0, 0, 1});
        layer->styles.push_back(std::move(fillElement));
    }
};

}  // namespace

TEST(SceneEvaluatorGradientPaintTest, EvaluatesGradientFillSnapshot) {
    GradientFillScene scene;
    Expected<SceneState, std::string> result =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    ASSERT_EQ(result->layers[0].shapeItems.size(), 1u);
    const auto &paint = result->layers[0].shapeItems[0].paint;
    EXPECT_EQ(paint.paintMode, StylePaintMode::Gradient);
    EXPECT_EQ(paint.gradient.type, GradientType::Linear);
    EXPECT_EQ(paint.gradient.start, (Vec2{0, 0}));
    EXPECT_EQ(paint.gradient.end, (Vec2{100, 0}));
    ASSERT_EQ(paint.gradient.stops.size(), 2u);
    EXPECT_FLOAT_EQ(paint.gradient.stops[0].position, 0.f);
    EXPECT_FLOAT_EQ(paint.gradient.stops[1].position, 1.f);
    // Must not fall back to solid color even though color is set.
    EXPECT_NE(paint.color, (Color{1, 0, 0, 1}));
}

TEST(SceneEvaluatorGradientPaintTest, InvalidStopsSkipStyle) {
    GradientFillScene scene;
    scene.fill->gradient.stops.clear();
    Expected<SceneState, std::string> result =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}

TEST(SceneEvaluatorGradientPaintTest, ZeroRadiusRadialSkipsStyle) {
    GradientFillScene scene;
    scene.fill->gradient.type = GradientType::Radial;
    scene.fill->gradient.end.setStaticValue(Vec2{0, 0});
    Expected<SceneState, std::string> result =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}
