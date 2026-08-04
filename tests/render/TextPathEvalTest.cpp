#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/BezierPathTransform.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::ApproxEqual;
using motion::BezierPath;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::Mat3;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapePath;
using motion::TextContent;
using motion::TransformBezierPath;
using motion::Vec2;

namespace {

BezierPath MakeHorizontalPath() {
    BezierPath path;
    path.closed = false;
    path.vertices.push_back({{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    path.vertices.push_back({{100.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    return path;
}

struct TextPathScene {
    Document document;
    Composition *composition = nullptr;
    Layer *pathLayer = nullptr;
    Layer *textLayer = nullptr;
    ShapePath *pathShape = nullptr;
    TextContent *textContent = nullptr;

    TextPathScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;

        pathLayer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        pathLayer->name = "path";
        pathLayer->outPoint = 100;
        auto *pathContent = static_cast<ShapeContent *>(pathLayer->content.get());
        auto pathElement = std::make_unique<ShapePath>();
        pathShape = pathElement.get();
        pathShape->path.setStaticValue(MakeHorizontalPath());
        pathContent->geometry = std::move(pathElement);
        auto pathFill = std::make_unique<FillStyle>();
        pathFill->color.setStaticValue(Color{0, 1, 0, 1});
        pathLayer->styles.push_back(std::move(pathFill));

        textLayer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
        textLayer->name = "text";
        textLayer->outPoint = 100;
        textContent = static_cast<TextContent *>(textLayer->content.get());
        textContent->text.setStaticValue("AB");
        textContent->textPath.enabled = true;
        textContent->textPath.pathLayerId = pathLayer->id;

        document.refreshEntityIndex();
    }

    motion::Expected<SceneState, std::string> Evaluate(motion::FrameTime time) {
        return SceneEvaluator::Evaluate(document, composition->id, time);
    }
};

const motion::EvaluatedLayer *FindLayer(const SceneState &state, motion::EntityId id) {
    for (const motion::EvaluatedLayer &layer : state.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

}  // namespace

TEST(TextPathEvalTest, IdentityLayersKeepLocalEndpoints) {
    TextPathScene scene;
    auto state = scene.Evaluate(0);
    ASSERT_TRUE(state.hasValue()) << state.error();
    const motion::EvaluatedLayer *text = FindLayer(*state, scene.textLayer->id);
    ASSERT_NE(text, nullptr);
    ASSERT_TRUE(text->textItem.has_value());
    ASSERT_TRUE(text->textItem->textPath.has_value());
    const BezierPath &path = text->textItem->textPath->path;
    ASSERT_EQ(path.vertices.size(), 2u);
    EXPECT_TRUE(ApproxEqual(path.vertices[0].point, {0.0f, 0.0f}));
    EXPECT_TRUE(ApproxEqual(path.vertices[1].point, {100.0f, 0.0f}));
}

TEST(TextPathEvalTest, PathLayerTranslationChangesLocalPoints) {
    TextPathScene scene;
    scene.pathLayer->transform.position.setStaticValue({30.0f, 10.0f});
    auto state = scene.Evaluate(0);
    ASSERT_TRUE(state.hasValue()) << state.error();
    const motion::EvaluatedLayer *text = FindLayer(*state, scene.textLayer->id);
    ASSERT_NE(text, nullptr);
    ASSERT_TRUE(text->textItem->textPath.has_value());
    const BezierPath &path = text->textItem->textPath->path;
    ASSERT_EQ(path.vertices.size(), 2u);
    EXPECT_TRUE(ApproxEqual(path.vertices[0].point, {30.0f, 10.0f}));
    EXPECT_TRUE(ApproxEqual(path.vertices[1].point, {130.0f, 10.0f}));
}

TEST(TextPathEvalTest, HiddenPathLayerStillEvaluatesTextPath) {
    TextPathScene scene;
    scene.pathLayer->visible = false;
    auto state = scene.Evaluate(0);
    ASSERT_TRUE(state.hasValue()) << state.error();
    EXPECT_EQ(FindLayer(*state, scene.pathLayer->id), nullptr);
    const motion::EvaluatedLayer *text = FindLayer(*state, scene.textLayer->id);
    ASSERT_NE(text, nullptr);
    ASSERT_TRUE(text->textItem.has_value());
    ASSERT_TRUE(text->textItem->textPath.has_value());
    EXPECT_EQ(text->textItem->textPath->path.vertices.size(), 2u);
}

TEST(TextPathEvalTest, SelfReferenceYieldsNoTextPath) {
    TextPathScene scene;
    scene.textContent->textPath.pathLayerId = scene.textLayer->id;
    auto state = scene.Evaluate(0);
    ASSERT_TRUE(state.hasValue()) << state.error();
    const motion::EvaluatedLayer *text = FindLayer(*state, scene.textLayer->id);
    ASSERT_NE(text, nullptr);
    ASSERT_TRUE(text->textItem.has_value());
    EXPECT_FALSE(text->textItem->textPath.has_value());
}

TEST(TextPathEvalTest, TransformBezierPathMapsTangentsWithoutTranslation) {
    BezierPath path;
    path.vertices.push_back({{10.0f, 0.0f}, {0.0f, 5.0f}, {2.0f, 0.0f}});
    const Mat3 matrix = Mat3::Translate({100.0f, 50.0f}) * Mat3::Scale({2.0f, 2.0f});
    const BezierPath transformed = TransformBezierPath(path, matrix);
    ASSERT_EQ(transformed.vertices.size(), 1u);
    EXPECT_TRUE(ApproxEqual(transformed.vertices[0].point, {120.0f, 50.0f}));
    EXPECT_TRUE(ApproxEqual(transformed.vertices[0].inTangent, {0.0f, 10.0f}));
    EXPECT_TRUE(ApproxEqual(transformed.vertices[0].outTangent, {4.0f, 0.0f}));
}
