#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::BindShaderPaint;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Expected;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShaderDefinition;
using motion::ShaderUniformDecl;
using motion::ShaderUniformValueKind;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::StylePaintMode;
using motion::UniformFormat;
using motion::Vec2;

namespace {

struct ShaderFillScene {
    Document document;
    Composition *composition = nullptr;
    Layer *layer = nullptr;
    FillStyle *fill = nullptr;
    EntityId shaderId{};

    ShaderFillScene() {
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

        ShaderDefinition shader;
        shader.name = "Ripple";
        shader.mainImage = "vec4 mainImage(vec2 uv) { return vec4(uv, 0.0, 1.0); }";
        shader.uniforms.push_back(ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1});
        shaderId = shader.id;
        document.shaders.push_back(shader);

        auto fillElement = std::make_unique<FillStyle>();
        fill = fillElement.get();
        const auto bound = BindShaderPaint(*fill, document.shaders[0]);
        if (!bound.hasValue()) {
            fill = nullptr;
            return;
        }
        fill->uniformValues.entries[0].floatValue.setStaticValue(3.5f);
        layer->styles.push_back(std::move(fillElement));
    }
};

}  // namespace

TEST(SceneEvaluatorShaderPaintTest, EvaluatesShaderFillSnapshot) {
    ShaderFillScene scene;
    ASSERT_NE(scene.fill, nullptr);
    Expected<SceneState, std::string> result =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_FLOAT_EQ(result->frameRate, 30.f);
    EXPECT_EQ(result->frameIndex, 0);
    EXPECT_FLOAT_EQ(result->timeSeconds, 0.f);

    ASSERT_EQ(result->layers.size(), 1u);
    ASSERT_EQ(result->layers[0].shapeItems.size(), 1u);
    const auto &paint = result->layers[0].shapeItems[0].paint;
    EXPECT_EQ(paint.paintMode, StylePaintMode::Shader);
    EXPECT_EQ(paint.shader.shaderId, scene.shaderId);
    EXPECT_FALSE(paint.shader.mainImage.empty());
    ASSERT_EQ(paint.shader.uniforms.size(), 1u);
    EXPECT_EQ(paint.shader.uniforms[0].name, "rippleCount");
    ASSERT_EQ(paint.shader.values.size(), 1u);
    EXPECT_EQ(paint.shader.values[0].kind, ShaderUniformValueKind::AnimFloat);
    EXPECT_FLOAT_EQ(paint.shader.values[0].floatValue, 3.5f);
}

TEST(SceneEvaluatorShaderPaintTest, MissingShaderSkipsStyle) {
    ShaderFillScene scene;
    ASSERT_NE(scene.fill, nullptr);
    scene.fill->shaderId = EntityId{999999};

    Expected<SceneState, std::string> result =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    // Rect with no successful styles emits neither shapeItems nor shapeNetwork.
    EXPECT_TRUE(result->layers.empty());
}

TEST(SceneEvaluatorShaderPaintTest, PreviewFillsFrameContextFields) {
    ShaderFillScene scene;
    Expected<SceneState, std::string> result =
        SceneEvaluator::EvaluatePreview(scene.document, scene.composition->id, 15.0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_FLOAT_EQ(result->frameRate, 30.f);
    EXPECT_EQ(result->frameIndex, 15);
    EXPECT_FLOAT_EQ(result->timeSeconds, 0.5f);
}
