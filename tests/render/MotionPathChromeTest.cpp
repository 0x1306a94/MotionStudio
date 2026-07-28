#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/render/MotionPathChrome.h"

using motion::Animatable;
using motion::BuildMotionPathChrome;
using motion::BuildMotionPathCommands;
using motion::Composition;
using motion::Document;
using motion::DrawCommandType;
using motion::EntityId;
using motion::HitTestMotionPath;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::MotionPathChrome;
using motion::MotionPathHandleKind;
using motion::MotionPathTangentDragUpdates;
using motion::PreviewTime;
using motion::Vec2;

namespace {

Keyframe<Vec2> MakePositionKeyframe(motion::FrameTime time, Vec2 value) {
    Keyframe<Vec2> keyframe;
    keyframe.time = time;
    keyframe.value = value;
    return keyframe;
}

struct Scene {
    Document document;
    Composition *composition = nullptr;
    Layer *layer = nullptr;

    Scene() {
        composition = document.addComposition(std::make_unique<Composition>());
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    }
};

}  // namespace

TEST(MotionPathChromeTest, FewerThanTwoKeyframesIsInvalid) {
    Scene scene;
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(0, {0, 0}));
    MotionPathChrome chrome;
    EXPECT_FALSE(BuildMotionPathChrome(scene.document, scene.layer->id, PreviewTime{0}, -1, chrome));
    EXPECT_FALSE(chrome.valid);
}

TEST(MotionPathChromeTest, BuildsPathAndPreviewHandles) {
    Scene scene;
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(0, {0, 0}));
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(10, {90, 0}));

    MotionPathChrome chrome;
    ASSERT_TRUE(BuildMotionPathChrome(scene.document, scene.layer->id, PreviewTime{0}, 0, chrome));
    ASSERT_EQ(chrome.worldVertices.size(), 2u);
    EXPECT_EQ(chrome.worldVertices[0], (Vec2{0, 0}));
    EXPECT_EQ(chrome.worldVertices[1], (Vec2{90, 0}));
    // Preview out at selected KF0 = Δ/3 = (30, 0)
    EXPECT_EQ(chrome.displayOutTangents[0], (Vec2{30, 0}));
    EXPECT_EQ(chrome.worldOutHandles[0], (Vec2{30, 0}));
    EXPECT_FALSE(chrome.hasStoredOut[0]);
}

TEST(MotionPathChromeTest, ParentWorldTransformApplies) {
    Scene scene;
    auto parent = std::make_unique<Layer>(LayerType::Shape);
    parent->transform.position.setStaticValue({100, 50});
    Layer *parentLayer = scene.document.addLayer(scene.composition->id, std::move(parent));
    ASSERT_TRUE(scene.layer->setParent(parentLayer->id, scene.document));

    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(0, {10, 20}));
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(10, {40, 20}));

    MotionPathChrome chrome;
    ASSERT_TRUE(BuildMotionPathChrome(scene.document, scene.layer->id, PreviewTime{0}, -1, chrome));
    EXPECT_EQ(chrome.worldVertices[0], (Vec2{110, 70}));
    EXPECT_EQ(chrome.worldVertices[1], (Vec2{140, 70}));
}

TEST(MotionPathChromeTest, HitSelectsKeyframeMarkersOnly) {
    Scene scene;
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(0, {0, 0}));
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(10, {90, 0}));

    MotionPathChrome chrome;
    ASSERT_TRUE(BuildMotionPathChrome(scene.document, scene.layer->id, PreviewTime{0}, 0, chrome));

    auto keyHit = HitTestMotionPath(chrome, {0, 0}, 8.0f);
    EXPECT_EQ(keyHit.kind, MotionPathHandleKind::Keyframe);
    EXPECT_EQ(keyHit.index, 0u);

    // Preview out handle location is not pickable on canvas anymore.
    auto outHit = HitTestMotionPath(chrome, {30, 0}, 8.0f);
    EXPECT_EQ(outHit.kind, MotionPathHandleKind::None);
}

TEST(MotionPathChromeTest, TangentDragWritesNeighborDefault) {
    Scene scene;
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(0, {0, 0}));
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(10, {90, 0}));

    MotionPathChrome chrome;
    ASSERT_TRUE(BuildMotionPathChrome(scene.document, scene.layer->id, PreviewTime{0}, 0, chrome));

    auto updates = MotionPathTangentDragUpdates(scene.document, scene.layer->id, 0, true,
                                                {45, 30}, chrome.parentWorldTransform);
    ASSERT_EQ(updates.size(), 2u);
    EXPECT_EQ(updates[0].time, 0);
    ASSERT_TRUE(updates[0].spatialOut.has_value());
    EXPECT_EQ(*updates[0].spatialOut, (Vec2{45, 30}));
    EXPECT_EQ(updates[1].time, 10);
    ASSERT_TRUE(updates[1].spatialIn.has_value());
    EXPECT_EQ(*updates[1].spatialIn, (Vec2{-30, 0}));
}

TEST(MotionPathChromeTest, BuildCommandsEmitsStrokeAndHandles) {
    Scene scene;
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(0, {0, 0}));
    scene.layer->transform.position.addKeyframe(MakePositionKeyframe(10, {90, 0}));

    MotionPathChrome chrome;
    ASSERT_TRUE(BuildMotionPathChrome(scene.document, scene.layer->id, PreviewTime{0}, 0, chrome));
    auto commands = BuildMotionPathCommands(chrome, 1.5f, 7.0f);
    EXPECT_FALSE(commands.empty());
    bool sawStroke = false;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::StrokePath) {
            sawStroke = true;
            break;
        }
    }
    EXPECT_TRUE(sawStroke);
}
