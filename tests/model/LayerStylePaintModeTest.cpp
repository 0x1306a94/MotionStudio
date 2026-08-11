#include <gtest/gtest.h>

#include <memory>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StylePaintMode.h"

using motion::BindShaderPaint;
using motion::ClearShaderPaint;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EnsureDefaultGradient;
using motion::EntityId;
using motion::FillStyle;
using motion::GradientPaint;
using motion::GradientStopsAreValid;
using motion::GradientType;
using motion::Layer;
using motion::LayerType;
using motion::ShaderDefinition;
using motion::ShaderIsReferenced;
using motion::ShaderUniformDecl;
using motion::ShaderUniformValueKind;
using motion::StrokeStyle;
using motion::StylePaintMode;
using motion::UniformFormat;
using motion::Vec2;

namespace {

ShaderDefinition MakeRippleShader() {
    ShaderDefinition shader;
    shader.name = "Ripple";
    shader.uniforms.push_back(ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1});
    shader.uniforms.push_back(ShaderUniformDecl{"tint", UniformFormat::Color, 1});
    return shader;
}

}  // namespace

TEST(LayerStylePaintModeTest, FillDefaultsToColorMode) {
    FillStyle fill;
    EXPECT_EQ(fill.paintMode, StylePaintMode::Color);
    EXPECT_FALSE(fill.shaderId.isValid());
    EXPECT_TRUE(fill.uniformValues.entries.empty());
}

TEST(LayerStylePaintModeTest, BindAndClearFillShaderPaint) {
    FillStyle fill;
    ShaderDefinition shader = MakeRippleShader();

    auto bound = BindShaderPaint(fill, shader);
    ASSERT_TRUE(bound.hasValue());
    EXPECT_EQ(fill.paintMode, StylePaintMode::Shader);
    EXPECT_EQ(fill.shaderId, shader.id);
    ASSERT_EQ(fill.uniformValues.entries.size(), 2u);
    EXPECT_EQ(fill.uniformValues.entries[0].kind, ShaderUniformValueKind::AnimFloat);
    EXPECT_EQ(fill.uniformValues.entries[1].kind, ShaderUniformValueKind::AnimColor);

    ClearShaderPaint(fill);
    EXPECT_EQ(fill.paintMode, StylePaintMode::Color);
    EXPECT_FALSE(fill.shaderId.isValid());
    EXPECT_TRUE(fill.uniformValues.entries.empty());
}

TEST(LayerStylePaintModeTest, BindAndClearStrokeShaderPaint) {
    StrokeStyle stroke;
    ShaderDefinition shader = MakeRippleShader();

    auto bound = BindShaderPaint(stroke, shader);
    ASSERT_TRUE(bound.hasValue());
    EXPECT_EQ(stroke.paintMode, StylePaintMode::Shader);
    EXPECT_EQ(stroke.shaderId, shader.id);
    ASSERT_EQ(stroke.uniformValues.entries.size(), 2u);

    ClearShaderPaint(stroke);
    EXPECT_EQ(stroke.paintMode, StylePaintMode::Color);
    EXPECT_FALSE(stroke.shaderId.isValid());
    EXPECT_TRUE(stroke.uniformValues.entries.empty());
}

TEST(LayerStylePaintModeTest, BindRejectsInvalidShaderId) {
    FillStyle fill;
    ShaderDefinition shader = MakeRippleShader();
    shader.id = EntityId{};

    auto bound = BindShaderPaint(fill, shader);
    EXPECT_FALSE(bound.hasValue());
    EXPECT_EQ(fill.paintMode, StylePaintMode::Color);
}

TEST(LayerStylePaintModeTest, SwitchKindPreservesShaderAndGradient) {
    FillStyle fill;
    ShaderDefinition shader = MakeRippleShader();
    ASSERT_TRUE(BindShaderPaint(fill, shader).hasValue());
    fill.gradient.stops.resize(2);
    fill.gradient.stops[0].position.setStaticValue(0.f);
    fill.gradient.stops[1].position.setStaticValue(1.f);
    fill.gradient.stops[1].color.setStaticValue(Color{1, 1, 1, 1});
    fill.paintMode = StylePaintMode::Gradient;
    const EntityId keptShader = fill.shaderId;
    fill.paintMode = StylePaintMode::Color;
    EXPECT_EQ(fill.shaderId, keptShader);
    EXPECT_EQ(fill.gradient.stops.size(), 2u);
    EXPECT_FALSE(fill.uniformValues.entries.empty());
}

TEST(LayerStylePaintModeTest, EnsureDefaultGradientOnlyWhenStopsMissing) {
    GradientPaint gradient;
    EnsureDefaultGradient(gradient, Vec2{10, 20}, Vec2{110, 20});
    ASSERT_TRUE(GradientStopsAreValid(gradient));
    EXPECT_EQ(gradient.type, GradientType::Linear);
    EXPECT_EQ(gradient.start.staticValue(), (Vec2{10, 20}));
    EXPECT_EQ(gradient.end.staticValue(), (Vec2{110, 20}));

    gradient.stops[0].color.setStaticValue(Color{1, 0, 0, 1});
    EnsureDefaultGradient(gradient, Vec2{0, 0}, Vec2{1, 0});
    EXPECT_EQ(gradient.stops[0].color.staticValue(), (Color{1, 0, 0, 1}));
}

TEST(LayerStylePaintModeTest, ShaderIsReferencedTracksFillAndStroke) {
    Document document;
    ShaderDefinition shader = MakeRippleShader();
    const EntityId shaderId = shader.id;
    document.shaders.push_back(shader);

    auto composition = std::make_unique<Composition>();
    auto *compositionPtr = document.addComposition(std::move(composition));
    ASSERT_NE(compositionPtr, nullptr);

    auto layer = std::make_unique<Layer>(LayerType::Shape);
    auto *layerPtr = document.addLayer(compositionPtr->id, std::move(layer));
    ASSERT_NE(layerPtr, nullptr);

    EXPECT_FALSE(ShaderIsReferenced(document, shaderId));

    auto fill = std::make_unique<FillStyle>();
    ASSERT_TRUE(BindShaderPaint(*fill, document.shaders[0]).hasValue());
    layerPtr->styles.push_back(std::move(fill));
    EXPECT_TRUE(ShaderIsReferenced(document, shaderId));

    ClearShaderPaint(*static_cast<FillStyle *>(layerPtr->styles[0].get()));
    EXPECT_FALSE(ShaderIsReferenced(document, shaderId));

    auto stroke = std::make_unique<StrokeStyle>();
    ASSERT_TRUE(BindShaderPaint(*stroke, document.shaders[0]).hasValue());
    layerPtr->styles.push_back(std::move(stroke));
    EXPECT_TRUE(ShaderIsReferenced(document, shaderId));
}
