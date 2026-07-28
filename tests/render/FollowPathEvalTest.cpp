#include <cmath>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/FollowPathEval.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::ApproxEqual;
using motion::BezierPath;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EvaluateFollowPath;
using motion::EvaluateLayerPath;
using motion::Expected;
using motion::FillStyle;
using motion::FollowSample;
using motion::Layer;
using motion::LayerType;
using motion::Mat3;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapePath;
using motion::ShapeRect;
using motion::Vec2;

namespace {

BezierPath MakeHorizontalPath() {
    BezierPath path;
    path.closed = false;
    path.vertices.push_back({{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    path.vertices.push_back({{100.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    return path;
}

struct FollowScene {
    Document document;
    Composition *composition = nullptr;
    Layer *pathLayer = nullptr;
    Layer *follower = nullptr;
    ShapePath *pathShape = nullptr;

    FollowScene() {
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

        follower = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        follower->name = "follower";
        follower->outPoint = 100;
        follower->transform.position.setStaticValue({0.0f, 50.0f});
        auto *followerContent = static_cast<ShapeContent *>(follower->content.get());
        auto rect = std::make_unique<ShapeRect>();
        rect->size.setStaticValue({10.0f, 10.0f});
        followerContent->geometry = std::move(rect);
        auto followerFill = std::make_unique<FillStyle>();
        followerFill->color.setStaticValue(Color{1, 0, 0, 1});
        follower->styles.push_back(std::move(followerFill));

        document.refreshEntityIndex();
    }

    void EnableFollow(float offset) {
        follower->followPath.enabled = true;
        follower->followPath.pathLayerId = pathLayer->id;
        follower->followPath.pathOffset.setStaticValue(offset);
        follower->followPath.orientAlongPath = true;
    }

    Expected<SceneState, std::string> Evaluate(motion::FrameTime time) {
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

TEST(FollowPathEvalTest, EvaluateLayerPathReadsShapePath) {
    FollowScene scene;
    const std::optional<BezierPath> path = EvaluateLayerPath(*scene.pathLayer, 0.0);
    ASSERT_TRUE(path.has_value());
    ASSERT_EQ(path->vertices.size(), 2u);
    EXPECT_TRUE(ApproxEqual(path->vertices[0].point, {0.0f, 0.0f}));
    EXPECT_TRUE(ApproxEqual(path->vertices[1].point, {100.0f, 0.0f}));
}

TEST(FollowPathEvalTest, OffsetDrivesParentSpacePosition) {
    FollowScene scene;
    scene.EnableFollow(0.5f);
    std::vector<motion::EntityId> followVisiting;
    const std::optional<FollowSample> sample =
        EvaluateFollowPath(scene.document, *scene.follower, 0.0, Mat3::Identity(),
                           Mat3::Identity(), followVisiting);
    ASSERT_TRUE(sample.has_value());
    EXPECT_TRUE(ApproxEqual(sample->parentSpacePosition, {50.0f, 0.0f}, 0.5f));
    EXPECT_TRUE(sample->overrideRotation);
    EXPECT_NEAR(sample->rotationDegrees, 0.0f, 1.0f);
}

TEST(FollowPathEvalTest, OrientOffsetAddsToTangentAngle) {
    FollowScene scene;
    scene.EnableFollow(0.0f);
    scene.follower->followPath.orientOffset.setStaticValue(90.0f);
    std::vector<motion::EntityId> followVisiting;
    const std::optional<FollowSample> sample =
        EvaluateFollowPath(scene.document, *scene.follower, 0.0, Mat3::Identity(),
                           Mat3::Identity(), followVisiting);
    ASSERT_TRUE(sample.has_value());
    EXPECT_NEAR(sample->rotationDegrees, 90.0f, 1.0f);
}

TEST(FollowPathEvalTest, InvalidPathLayerIsNoOp) {
    FollowScene scene;
    scene.follower->followPath.enabled = true;
    scene.follower->followPath.pathLayerId = motion::EntityId{999};
    std::vector<motion::EntityId> followVisiting;
    EXPECT_FALSE(EvaluateFollowPath(scene.document, *scene.follower, 0.0, Mat3::Identity(),
                                    Mat3::Identity(), followVisiting)
                     .has_value());
}

TEST(FollowPathEvalTest, SceneEvaluatorPlacesFollowerOnPath) {
    FollowScene scene;
    scene.EnableFollow(1.0f);
    Expected<SceneState, std::string> state = scene.Evaluate(0);
    ASSERT_TRUE(state.hasValue());
    const motion::EvaluatedLayer *evaluated = FindLayer(*state, scene.follower->id);
    ASSERT_NE(evaluated, nullptr);
    EXPECT_TRUE(ApproxEqual(evaluated->worldTransform.transformPoint({0.0f, 0.0f}),
                            {100.0f, 0.0f}, 0.5f));
}

TEST(FollowPathEvalTest, MutualFollowDoesNotHang) {
    FollowScene scene;
    scene.EnableFollow(0.5f);
    scene.pathLayer->followPath.enabled = true;
    scene.pathLayer->followPath.pathLayerId = scene.follower->id;
    scene.pathLayer->followPath.pathOffset.setStaticValue(0.5f);
    Expected<SceneState, std::string> state = scene.Evaluate(0);
    ASSERT_TRUE(state.hasValue());
}
