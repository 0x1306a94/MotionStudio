#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeGroup.h"
#include "MotionStudio/model/ShapeStroke.h"
#include "MotionStudio/model/TextContent.h"

using motion::Animatable;
using motion::AnimatableBase;
using motion::Composition;
using motion::Document;
using motion::Layer;
using motion::LayerType;
using motion::parsePropertyPath;
using motion::PropertyPath;
using motion::resolveAnimatable;
using motion::ShapeContent;
using motion::ShapeFill;
using motion::ShapeGroup;
using motion::ShapeStroke;

TEST(ParsePropertyPathTest, SimpleDottedPath) {
    auto segments = parsePropertyPath("transform.position");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0], (motion::PathSegment{"transform", -1}));
    EXPECT_EQ(segments[1], (motion::PathSegment{"position", -1}));
}

TEST(ParsePropertyPathTest, ArrayIndex) {
    auto segments = parsePropertyPath("elements[12].color");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0], (motion::PathSegment{"elements", 12}));
    EXPECT_EQ(segments[1], (motion::PathSegment{"color", -1}));
}

TEST(ParsePropertyPathTest, RejectsMalformed) {
    EXPECT_TRUE(parsePropertyPath("").empty());
    EXPECT_TRUE(parsePropertyPath(".a").empty());
    EXPECT_TRUE(parsePropertyPath("a.").empty());
    EXPECT_TRUE(parsePropertyPath("a[x]").empty());
    EXPECT_TRUE(parsePropertyPath("a[1").empty());
    EXPECT_TRUE(parsePropertyPath("a[]").empty());
}

namespace {

struct ShapeScene {
    Document document;
    Composition* composition;
    Layer* layer;
    ShapeFill* fill = nullptr;
    ShapeGroup* group = nullptr;
    ShapeStroke* nestedStroke = nullptr;

    ShapeScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        auto* shapeContent = static_cast<ShapeContent*>(layer->content.get());

        auto fillElement = std::make_unique<ShapeFill>();
        fill = fillElement.get();
        shapeContent->elements.push_back(std::move(fillElement));

        auto groupElement = std::make_unique<ShapeGroup>();
        group = groupElement.get();
        auto strokeElement = std::make_unique<ShapeStroke>();
        nestedStroke = strokeElement.get();
        groupElement->elements.push_back(std::move(strokeElement));
        shapeContent->elements.push_back(std::move(groupElement));

        document.refreshEntityIndex();
    }
};

}  // namespace

TEST(ResolveAnimatableTest, ResolvesTransformProperty) {
    ShapeScene scene;
    AnimatableBase* resolved =
        resolveAnimatable(scene.document, {scene.layer->id, "transform.position"});
    EXPECT_EQ(resolved,
              static_cast<AnimatableBase*>(&scene.layer->transform.position));
}

TEST(ResolveAnimatableTest, ResolvesShapeById) {
    ShapeScene scene;
    AnimatableBase* resolved = resolveAnimatable(scene.document, {scene.fill->id, "color"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase*>(&scene.fill->color));
}

TEST(ResolveAnimatableTest, ResolvesLayerElementsPath) {
    ShapeScene scene;
    AnimatableBase* resolved =
        resolveAnimatable(scene.document, {scene.layer->id, "elements[0].color"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase*>(&scene.fill->color));
}

TEST(ResolveAnimatableTest, ResolvesNestedGroupPath) {
    ShapeScene scene;
    AnimatableBase* resolved = resolveAnimatable(
        scene.document, {scene.layer->id, "elements[1].elements[0].width"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase*>(&scene.nestedStroke->width));
}

TEST(ResolveAnimatableTest, ResolvesGroupTransform) {
    ShapeScene scene;
    AnimatableBase* resolved = resolveAnimatable(
        scene.document, {scene.layer->id, "elements[1].transform.opacity"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase*>(&scene.group->transform.opacity));
}

TEST(ResolveAnimatableTest, ResolvesTextContent) {
    Document document;
    Composition* composition = document.addComposition(std::make_unique<Composition>());
    Layer* textLayer =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    auto* textContent = static_cast<motion::TextContent*>(textLayer->content.get());

    AnimatableBase* resolved =
        resolveAnimatable(document, {textLayer->id, "content.fontSize"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase*>(&textContent->fontSize));
}

TEST(ResolveAnimatableTest, ReturnsNullForMissingOrInvalid) {
    ShapeScene scene;
    EXPECT_EQ(resolveAnimatable(scene.document, {motion::EntityId{999}, "color"}), nullptr);
    EXPECT_EQ(resolveAnimatable(scene.document, {scene.layer->id, "transform.bogus"}),
              nullptr);
    EXPECT_EQ(resolveAnimatable(scene.document, {scene.layer->id, "elements[5].color"}),
              nullptr);
    EXPECT_EQ(resolveAnimatable(scene.document, {scene.layer->id, "transform"}), nullptr);
    EXPECT_EQ(resolveAnimatable(scene.document, {scene.layer->id, "elements[0].width"}),
              nullptr);  // fill 没有 width
    EXPECT_EQ(resolveAnimatable(scene.document, {scene.layer->id, "elements[-1].color"}),
              nullptr);
}
