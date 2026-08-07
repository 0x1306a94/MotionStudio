#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/model/TextContent.h"

using motion::Animatable;
using motion::AnimatableBase;
using motion::BindShaderPaint;
using motion::Composition;
using motion::Document;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::Mask;
using motion::ParsePropertyPath;
using motion::PropertyPath;
using motion::ResolveAnimatable;
using motion::ShaderDefinition;
using motion::ShaderUniformDecl;
using motion::ShaderUniformValue;
using motion::ShaderUniformValueKind;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::StrokeStyle;
using motion::StylePaintMode;
using motion::UniformFormat;

TEST(ParsePropertyPathTest, SimpleDottedPath) {
    auto segments = ParsePropertyPath("transform.position");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0], (motion::PathSegment{"transform", -1}));
    EXPECT_EQ(segments[1], (motion::PathSegment{"position", -1}));
}

TEST(ParsePropertyPathTest, ArrayIndex) {
    auto segments = ParsePropertyPath("styles[12].color");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0], (motion::PathSegment{"styles", 12}));
    EXPECT_EQ(segments[1], (motion::PathSegment{"color", -1}));
}

TEST(ParsePropertyPathTest, RejectsMalformed) {
    EXPECT_TRUE(ParsePropertyPath("").empty());
    EXPECT_TRUE(ParsePropertyPath(".a").empty());
    EXPECT_TRUE(ParsePropertyPath("a.").empty());
    EXPECT_TRUE(ParsePropertyPath("a[x]").empty());
    EXPECT_TRUE(ParsePropertyPath("a[1").empty());
    EXPECT_TRUE(ParsePropertyPath("a[]").empty());
}

namespace {

struct ShapeScene {
    Document document;
    Composition *composition;
    Layer *layer;
    ShapeRect *rect = nullptr;

    ShapeScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        auto *shapeContent = static_cast<ShapeContent *>(layer->content.get());

        auto rectElement = std::make_unique<ShapeRect>();
        rect = rectElement.get();
        shapeContent->geometry = std::move(rectElement);

        document.refreshEntityIndex();
    }
};

}  // namespace

TEST(ResolveAnimatableTest, ResolvesTransformProperty) {
    ShapeScene scene;
    AnimatableBase *resolved =
        ResolveAnimatable(scene.document, {scene.layer->id, "transform.position"});
    EXPECT_EQ(resolved,
              static_cast<AnimatableBase *>(&scene.layer->transform.position));
}

TEST(ResolveAnimatableTest, ResolvesFollowPathProperties) {
    ShapeScene scene;
    AnimatableBase *pathOffset =
        ResolveAnimatable(scene.document, {scene.layer->id, "followPath.pathOffset"});
    AnimatableBase *orientOffset =
        ResolveAnimatable(scene.document, {scene.layer->id, "followPath.orientOffset"});
    EXPECT_EQ(pathOffset, static_cast<AnimatableBase *>(&scene.layer->followPath.pathOffset));
    EXPECT_EQ(orientOffset,
              static_cast<AnimatableBase *>(&scene.layer->followPath.orientOffset));
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.layer->id, "followPath.enabled"}),
              nullptr);
}

TEST(ResolveAnimatableTest, ResolvesShapeById) {
    ShapeScene scene;
    AnimatableBase *resolved = ResolveAnimatable(scene.document, {scene.rect->id, "size"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase *>(&scene.rect->size));
}

TEST(ResolveAnimatableTest, ResolvesPrimaryShapePropertyFromLayer) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *shapeContent = static_cast<ShapeContent *>(layer->content.get());

    auto rectElement = std::make_unique<ShapeRect>();
    ShapeRect *rect = rectElement.get();
    shapeContent->geometry = std::move(rectElement);
    document.refreshEntityIndex();

    AnimatableBase *size = ResolveAnimatable(document, {layer->id, "size"});
    AnimatableBase *cornerRadius = ResolveAnimatable(document, {layer->id, "cornerRadius"});
    EXPECT_EQ(size, static_cast<AnimatableBase *>(&rect->size));
    EXPECT_EQ(cornerRadius, static_cast<AnimatableBase *>(&rect->cornerRadius));
}

TEST(ResolveAnimatableTest, ResolvesLayerStyleProperty) {
    ShapeScene scene;
    auto fill = std::make_unique<FillStyle>();
    FillStyle *fillStyle = fill.get();
    scene.layer->styles.push_back(std::move(fill));
    auto stroke = std::make_unique<StrokeStyle>();
    StrokeStyle *strokeStyle = stroke.get();
    scene.layer->styles.push_back(std::move(stroke));

    AnimatableBase *color =
        ResolveAnimatable(scene.document, {scene.layer->id, "styles[0].color"});
    AnimatableBase *width =
        ResolveAnimatable(scene.document, {scene.layer->id, "styles[1].width"});
    EXPECT_EQ(color, static_cast<AnimatableBase *>(&fillStyle->color));
    EXPECT_EQ(width, static_cast<AnimatableBase *>(&strokeStyle->width));
}

TEST(ResolveAnimatableTest, ResolvesMaskProperties) {
    ShapeScene scene;
    scene.layer->masks.emplace_back();
    Mask &mask = scene.layer->masks.front();

    AnimatableBase *path =
        ResolveAnimatable(scene.document, {scene.layer->id, "masks[0].path"});
    AnimatableBase *opacity =
        ResolveAnimatable(scene.document, {scene.layer->id, "masks[0].opacity"});
    AnimatableBase *feather =
        ResolveAnimatable(scene.document, {scene.layer->id, "masks[0].feather"});
    AnimatableBase *expansion =
        ResolveAnimatable(scene.document, {scene.layer->id, "masks[0].expansion"});
    EXPECT_EQ(path, static_cast<AnimatableBase *>(&mask.path));
    EXPECT_EQ(opacity, static_cast<AnimatableBase *>(&mask.opacity));
    EXPECT_EQ(feather, static_cast<AnimatableBase *>(&mask.feather));
    EXPECT_EQ(expansion, static_cast<AnimatableBase *>(&mask.expansion));
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.layer->id, "masks[1].opacity"}),
              nullptr);
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.layer->id, "masks[0].mode"}),
              nullptr);
}

TEST(ResolveAnimatableTest, ResolvesTextContent) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *textLayer =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    auto *textContent = static_cast<motion::TextContent *>(textLayer->content.get());

    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.fontSize"}), nullptr);
    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.size"}), nullptr);
    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.text"}),
              static_cast<AnimatableBase *>(&textContent->text));
}

TEST(ResolveAnimatableTest, ResolvesTextPathMargins) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *textLayer =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    auto *textContent = static_cast<motion::TextContent *>(textLayer->content.get());

    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.textPath.firstMargin"}),
              static_cast<AnimatableBase *>(&textContent->textPath.firstMargin));
    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.textPath.lastMargin"}),
              static_cast<AnimatableBase *>(&textContent->textPath.lastMargin));
    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.textPath.enabled"}), nullptr);
}

TEST(ResolveAnimatableTest, ResolvesImageSize) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *imageLayer =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Image));
    auto *imageContent = static_cast<motion::ImageContent *>(imageLayer->content.get());

    AnimatableBase *resolved = ResolveAnimatable(document, {imageLayer->id, "image.size"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase *>(&imageContent->size));
    EXPECT_EQ(ResolveAnimatable(document, {imageLayer->id, "image.bogus"}), nullptr);
    EXPECT_EQ(ResolveAnimatable(document, {imageLayer->id, "size"}), nullptr);
}

TEST(ResolveAnimatableTest, ReturnsNullForMissingOrInvalid) {
    ShapeScene scene;
    EXPECT_EQ(ResolveAnimatable(scene.document, {motion::EntityId{999}, "size"}), nullptr);
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.layer->id, "transform.bogus"}),
              nullptr);
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.layer->id, "styles[5].color"}),
              nullptr);
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.layer->id, "transform"}), nullptr);
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.rect->id, "bogus"}),
              nullptr);
    EXPECT_EQ(ResolveAnimatable(scene.document, {scene.layer->id, "styles[-1].color"}),
              nullptr);
}

TEST(PropertyPathTest, ResolvesShaderUniformFloat) {
    ShapeScene scene;
    ShaderDefinition shader;
    shader.name = "Ripple";
    shader.uniforms.push_back(ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1});
    shader.uniforms.push_back(ShaderUniformDecl{"tint", UniformFormat::Float4, 1});

    auto fill = std::make_unique<FillStyle>();
    FillStyle *fillStyle = fill.get();
    ASSERT_TRUE(BindShaderPaint(*fillStyle, shader).hasValue());
    fillStyle->uniformValues.entries[0].floatValue.setStaticValue(5.f);
    scene.layer->styles.push_back(std::move(fill));

    AnimatableBase *resolved =
        ResolveAnimatable(scene.document, {scene.layer->id, "styles[0].uniformValues.rippleCount"});
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved, static_cast<AnimatableBase *>(&fillStyle->uniformValues.entries[0].floatValue));

    auto *floatAnim = static_cast<Animatable<float> *>(resolved);
    EXPECT_FLOAT_EQ(floatAnim->staticValue(), 5.f);
    floatAnim->setStaticValue(12.f);
    EXPECT_FLOAT_EQ(fillStyle->uniformValues.entries[0].floatValue.staticValue(), 12.f);

    AnimatableBase *tint =
        ResolveAnimatable(scene.document, {scene.layer->id, "styles[0].uniformValues.tint"});
    ASSERT_NE(tint, nullptr);
    EXPECT_EQ(tint, static_cast<AnimatableBase *>(&fillStyle->uniformValues.entries[1].colorValue));
}

TEST(PropertyPathTest, ShaderUniformRequiresShaderPaintMode) {
    ShapeScene scene;
    auto fill = std::make_unique<FillStyle>();
    FillStyle *fillStyle = fill.get();
    fillStyle->paintMode = StylePaintMode::Color;
    ShaderUniformValue entry;
    entry.name = "rippleCount";
    entry.kind = ShaderUniformValueKind::AnimFloat;
    entry.floatValue.setStaticValue(5.f);
    fillStyle->uniformValues.entries.push_back(std::move(entry));
    scene.layer->styles.push_back(std::move(fill));

    EXPECT_EQ(ResolveAnimatable(scene.document,
                                {scene.layer->id, "styles[0].uniformValues.rippleCount"}),
              nullptr);
}

TEST(PropertyPathTest, MissingShaderUniformReturnsNull) {
    ShapeScene scene;
    ShaderDefinition shader;
    shader.uniforms.push_back(ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1});

    auto fill = std::make_unique<FillStyle>();
    ASSERT_TRUE(BindShaderPaint(*fill, shader).hasValue());
    scene.layer->styles.push_back(std::move(fill));

    EXPECT_EQ(ResolveAnimatable(scene.document,
                                {scene.layer->id, "styles[0].uniformValues.missing"}),
              nullptr);
}
