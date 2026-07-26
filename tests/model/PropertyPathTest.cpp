#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/TextContent.h"

using motion::Animatable;
using motion::AnimatableBase;
using motion::Composition;
using motion::Document;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::Mask;
using motion::ParsePropertyPath;
using motion::PropertyPath;
using motion::ResolveAnimatable;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::StrokeStyle;

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

    AnimatableBase *resolved =
        ResolveAnimatable(document, {textLayer->id, "content.fontSize"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase *>(&textContent->fontSize));
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
