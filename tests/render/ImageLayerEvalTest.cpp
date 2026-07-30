#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/render/HitTest.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::Asset;
using motion::AssetType;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Expected;
using motion::ImageContent;
using motion::ImageScaleMode;
using motion::Layer;
using motion::LayerType;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::Vec2;

namespace {

struct ImageScene {
    Document document;
    Composition *composition = nullptr;
    Layer *layer = nullptr;
    Asset asset;

    ImageScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        composition->width = 800;
        composition->height = 600;
        composition->duration = 90;

        asset.type = AssetType::Image;
        asset.name = "photo.png";
        asset.path = "assets/photo.png";
        asset.width = 400;
        asset.height = 300;
        document.assets.push_back(asset);

        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Image));
        layer->outPoint = composition->duration;
        auto *image = static_cast<ImageContent *>(layer->content.get());
        image->assetId = asset.id;
        image->size.setStaticValue(Vec2{200, 100});
        image->scaleMode = ImageScaleMode::Stretch;
        layer->transform.anchorPoint.setStaticValue(Vec2{100, 50});
        layer->transform.position.setStaticValue(Vec2{400, 300});
        document.projectRoot = "/tmp/project";
        document.refreshEntityIndex();
    }
};

}  // namespace

TEST(ImageLayerEvalTest, EvaluatesBoundImageItem) {
    ImageScene scene;
    Expected<SceneState, std::string> state =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(state.hasValue());
    ASSERT_EQ(state->layers.size(), 1u);
    ASSERT_TRUE(state->layers[0].imageItem.has_value());
    EXPECT_TRUE(state->layers[0].shapeItems.empty());
    EXPECT_EQ(state->layers[0].imageItem->absolutePath, "/tmp/project/assets/photo.png");
    EXPECT_FLOAT_EQ(state->layers[0].imageItem->containerSize.x, 200.f);
    EXPECT_FLOAT_EQ(state->layers[0].imageItem->containerSize.y, 100.f);
    EXPECT_FLOAT_EQ(state->layers[0].imageItem->intrinsicSize.x, 400.f);
    EXPECT_FLOAT_EQ(state->layers[0].imageItem->intrinsicSize.y, 300.f);
    EXPECT_EQ(state->layers[0].imageItem->scaleMode, ImageScaleMode::Stretch);
}

TEST(ImageLayerEvalTest, UnboundImageStillHasContainerBounds) {
    ImageScene scene;
    static_cast<ImageContent *>(scene.layer->content.get())->assetId = {};
    Expected<SceneState, std::string> state =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(state.hasValue());
    ASSERT_EQ(state->layers.size(), 1u);
    ASSERT_TRUE(state->layers[0].imageItem.has_value());
    EXPECT_TRUE(state->layers[0].imageItem->absolutePath.empty());

    Vec2 minPoint;
    Vec2 maxPoint;
    ASSERT_TRUE(motion::BoundsOfLayerLocal(state->layers[0], minPoint, maxPoint));
    EXPECT_FLOAT_EQ(minPoint.x, 0.f);
    EXPECT_FLOAT_EQ(minPoint.y, 0.f);
    EXPECT_FLOAT_EQ(maxPoint.x, 200.f);
    EXPECT_FLOAT_EQ(maxPoint.y, 100.f);
}

TEST(ImageLayerEvalTest, HitTestUsesContainer) {
    ImageScene scene;
    Expected<SceneState, std::string> state =
        SceneEvaluator::Evaluate(scene.document, scene.composition->id, 0);
    ASSERT_TRUE(state.hasValue());
    // Position at composition center with anchor at container center → container
    // covers scene [300,250]–[500,350].
    EXPECT_TRUE(motion::HitTestLayer(state->layers[0], Vec2{400, 300}, 0.0f));
    EXPECT_FALSE(motion::HitTestLayer(state->layers[0], Vec2{10, 10}, 0.0f));
}
