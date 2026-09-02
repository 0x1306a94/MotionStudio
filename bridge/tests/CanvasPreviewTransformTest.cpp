#include <unordered_map>

#include <gtest/gtest.h>

#include "MSCanvas.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/render/EvaluatedLayer.h"
#include "MotionStudio/render/SceneState.h"
#include "PreviewTransformApply.h"
#include "motionstudio_bridge.h"

namespace {

TEST(CanvasPreviewTransformTest, ApplyMultipliesWorldOnRight) {
    motion::SceneState state;
    motion::EvaluatedLayer layer;
    layer.id = motion::EntityId{7};
    layer.worldTransform = motion::Mat3::Translate({10, 0});
    state.layers.push_back(layer);

    std::unordered_map<motion::EntityId, motion::Mat3> preview;
    preview[motion::EntityId{7}] = motion::Mat3::Scale({2, 1});
    bridge::ApplyPreviewTransformsToScene(state, preview);

    const motion::Vec2 point = state.layers[0].worldTransform.transformPoint({1, 0});
    EXPECT_FLOAT_EQ(point.x, 12.f);
    EXPECT_FLOAT_EQ(point.y, 0.f);
}

TEST(CanvasPreviewTransformTest, ApplySkipsLayersWithoutPreview) {
    motion::SceneState state;
    motion::EvaluatedLayer layer;
    layer.id = motion::EntityId{3};
    layer.worldTransform = motion::Mat3::Translate({4, 5});
    state.layers.push_back(layer);

    std::unordered_map<motion::EntityId, motion::Mat3> preview;
    preview[motion::EntityId{9}] = motion::Mat3::Scale({3, 3});
    bridge::ApplyPreviewTransformsToScene(state, preview);

    const motion::Vec2 point = state.layers[0].worldTransform.transformPoint({0, 0});
    EXPECT_FLOAT_EQ(point.x, 4.f);
    EXPECT_FLOAT_EQ(point.y, 5.f);
}

TEST(CanvasPreviewTransformTest, SetClearRoundTripOnCanvas) {
    MSCanvas canvas = {};
    const float matrix[9] = {2, 0, 0, 0, 3, 0, 0, 0, 1};
    ms_canvas_set_preview_transform(&canvas, 42, matrix);
    ASSERT_EQ(canvas.previewTransforms.size(), 1u);
    const auto found = canvas.previewTransforms.find(motion::EntityId{42});
    ASSERT_NE(found, canvas.previewTransforms.end());
    EXPECT_FLOAT_EQ(found->second.values[0], 2.f);
    EXPECT_FLOAT_EQ(found->second.values[4], 3.f);

    ms_canvas_clear_preview_transform(&canvas, 42);
    EXPECT_TRUE(canvas.previewTransforms.empty());

    ms_canvas_set_preview_transform(&canvas, 1, matrix);
    ms_canvas_set_preview_transform(&canvas, 2, matrix);
    ASSERT_EQ(canvas.previewTransforms.size(), 2u);
    ms_canvas_clear_all_preview_transforms(&canvas);
    EXPECT_TRUE(canvas.previewTransforms.empty());
}

TEST(CanvasPreviewTransformTest, NullCanvasAndInvalidArgsAreNoops) {
    ms_canvas_set_preview_transform(nullptr, 1, nullptr);
    ms_canvas_clear_preview_transform(nullptr, 1);
    ms_canvas_clear_all_preview_transforms(nullptr);

    MSCanvas canvas = {};
    const float matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    ms_canvas_set_preview_transform(&canvas, 0, matrix);
    ms_canvas_set_preview_transform(&canvas, 1, nullptr);
    EXPECT_TRUE(canvas.previewTransforms.empty());
}

}  // namespace
