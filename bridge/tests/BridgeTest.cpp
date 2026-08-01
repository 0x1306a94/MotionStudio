#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "FrameCommandCache.h"
#include "motionstudio_bridge.h"

namespace {

// RAII wrapper for malloc'd strings returned by the bridge.
struct BridgeString {
    char *value = nullptr;
    ~BridgeString() {
        ms_string_free(value);
    }
    std::string str() const {
        return value != nullptr ? value : "";
    }
};

TEST(BridgeDocumentTest, CreateHasDefaultComposition) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);

    EXPECT_EQ(ms_document_composition_count(document), 1);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    EXPECT_NE(compositionId, 0u);
    EXPECT_EQ(ms_composition_width(document, compositionId), 1920);
    EXPECT_EQ(ms_composition_height(document, compositionId), 1080);
    EXPECT_EQ(ms_composition_duration(document, compositionId), 150);
    EXPECT_EQ(ms_composition_frame_rate_num(document, compositionId), 30);
    EXPECT_EQ(ms_composition_frame_rate_den(document, compositionId), 1);
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 0);
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 0.0f;
    ms_composition_background_color(document, compositionId, &r, &g, &b, &a);
    EXPECT_FLOAT_EQ(r, 0.0f);
    EXPECT_FLOAT_EQ(g, 0.0f);
    EXPECT_FLOAT_EQ(b, 0.0f);
    EXPECT_FLOAT_EQ(a, 1.0f);
    EXPECT_FLOAT_EQ(ms_composition_corner_radius(document, compositionId), 0.0f);

    ms_document_destroy(document);
}

TEST(BridgeDocumentTest, SaveLoadRoundTrip) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);

    BridgeString saved{ms_document_save(document)};
    ASSERT_NE(saved.value, nullptr);
    ms_document_destroy(document);

    MSDocument *loaded = ms_document_load_json(saved.value, std::strlen(saved.value), nullptr);
    ASSERT_NE(loaded, nullptr);
    const uint64_t loadedCompositionId = ms_document_composition_id_at(loaded, 0);
    EXPECT_EQ(ms_composition_layer_count(loaded, loadedCompositionId), 1);
    const uint64_t loadedLayerId = ms_layer_id_at(loaded, loadedCompositionId, 0);
    EXPECT_EQ(loadedLayerId, layerId);
    BridgeString name{ms_layer_name(loaded, loadedLayerId)};
    EXPECT_EQ(name.str(), "Rectangle 1");
    ms_document_destroy(loaded);
}

TEST(BridgeDocumentTest, LoadInvalidJsonReportsError) {
    char *error = nullptr;
    MSDocument *document = ms_document_load_json("not json", 8, &error);
    EXPECT_EQ(document, nullptr);
    ASSERT_NE(error, nullptr);
    EXPECT_FALSE(std::string(error).empty());
    ms_string_free(error);
}

TEST(BridgeCommandTest, AddShapeLayerIsUndoable) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    EXPECT_FALSE(ms_document_can_undo(document));
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 1);
    EXPECT_EQ(ms_layer_type(document, layerId), MS_LAYER_SHAPE);
    EXPECT_TRUE(ms_layer_visible(document, layerId));
    EXPECT_EQ(ms_layer_in_point(document, layerId), 0);
    EXPECT_EQ(ms_layer_out_point(document, layerId), 150);

    BridgeString description{ms_document_undo_description(document)};
    EXPECT_FALSE(description.str().empty());

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 0);
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 1);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, EllipseAndRectLayersCycleNames) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    ms_command_add_rect_layer(document, compositionId);
    const uint64_t ellipseId = ms_command_add_ellipse_layer(document, compositionId);
    BridgeString name{ms_layer_name(document, ellipseId)};
    EXPECT_EQ(name.str(), "Ellipse 2");

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, MaskAndTrackMatteLifecycle) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    const uint64_t matteId = ms_command_add_ellipse_layer(document, compositionId);

    EXPECT_EQ(ms_layer_mask_count(document, layerId), 0);
    ms_command_add_mask(document, layerId, 0);
    ASSERT_EQ(ms_layer_mask_count(document, layerId), 1);
    EXPECT_EQ(ms_layer_mask_mode_at(document, layerId, 0), MS_MASK_ADD);
    EXPECT_FALSE(ms_layer_mask_inverted_at(document, layerId, 0));

    ms_command_set_mask_mode(document, layerId, 0, MS_MASK_SUBTRACT);
    EXPECT_EQ(ms_layer_mask_mode_at(document, layerId, 0), MS_MASK_SUBTRACT);
    ms_command_set_mask_inverted(document, layerId, 0, true);
    EXPECT_TRUE(ms_layer_mask_inverted_at(document, layerId, 0));

    ms_command_add_mask(document, layerId, 0);
    ms_command_move_mask(document, layerId, 0, 1);
    EXPECT_EQ(ms_layer_mask_mode_at(document, layerId, 1), MS_MASK_SUBTRACT);

    ms_command_set_track_matte(document, layerId, matteId, MS_TRACK_MATTE_ALPHA);
    EXPECT_EQ(ms_layer_track_matte_type(document, layerId), MS_TRACK_MATTE_ALPHA);
    EXPECT_EQ(ms_layer_track_matte_layer_id(document, layerId), matteId);

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_layer_track_matte_type(document, layerId), MS_TRACK_MATTE_NONE);

    ms_command_remove_mask(document, layerId, 0);
    EXPECT_EQ(ms_layer_mask_count(document, layerId), 1);
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_layer_mask_count(document, layerId), 2);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, FollowPathLifecycle) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t pathLayerId = ms_command_add_ellipse_layer(document, compositionId);
    const uint64_t followerId = ms_command_add_rect_layer(document, compositionId);

    EXPECT_FALSE(ms_layer_follow_path_enabled(document, followerId));
    EXPECT_EQ(ms_layer_follow_path_layer_id(document, followerId), 0u);
    EXPECT_TRUE(ms_layer_follow_path_orient(document, followerId));

    ms_command_set_follow_path(document, followerId, true, pathLayerId, true);
    EXPECT_TRUE(ms_layer_follow_path_enabled(document, followerId));
    EXPECT_EQ(ms_layer_follow_path_layer_id(document, followerId), pathLayerId);
    EXPECT_TRUE(ms_layer_follow_path_orient(document, followerId));

    ms_command_set_static_float(document, followerId, "followPath.pathOffset", 0.5f);
    EXPECT_FLOAT_EQ(ms_property_evaluate_float(document, followerId, "followPath.pathOffset", 0),
                    0.5f);

    ms_command_add_keyframe_float(document, followerId, "followPath.pathOffset", 0, 0.0f);
    ms_command_add_keyframe_float(document, followerId, "followPath.pathOffset", 20, 1.0f);
    EXPECT_FLOAT_EQ(ms_property_evaluate_float(document, followerId, "followPath.pathOffset", 10),
                    0.5f);

    ms_document_end_merge_group(document);
    ms_command_set_follow_path(document, followerId, false, 0, false);
    EXPECT_FALSE(ms_layer_follow_path_enabled(document, followerId));
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_TRUE(ms_layer_follow_path_enabled(document, followerId));
    EXPECT_EQ(ms_layer_follow_path_layer_id(document, followerId), pathLayerId);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, LayerBlendModeSetAndUndo) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_image_layer(document, compositionId);

    EXPECT_EQ(ms_layer_blend_mode(document, layerId), MS_BLEND_NORMAL);
    ms_command_set_layer_blend_mode(document, layerId, MS_BLEND_MULTIPLY);
    EXPECT_EQ(ms_layer_blend_mode(document, layerId), MS_BLEND_MULTIPLY);
    ms_command_set_layer_blend_mode(document, layerId, MS_BLEND_SCREEN);
    EXPECT_EQ(ms_layer_blend_mode(document, layerId), MS_BLEND_SCREEN);
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_layer_blend_mode(document, layerId), MS_BLEND_NORMAL);
    EXPECT_EQ(ms_layer_blend_mode(document, 0), MS_BLEND_INVALID);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, FillStyleLifecycle) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    // Rect layers start with one default fill.
    ASSERT_EQ(ms_layer_style_count(document, layerId), 1);
    EXPECT_EQ(ms_layer_style_type_at(document, layerId, 0), MS_STYLE_FILL);
    EXPECT_EQ(ms_layer_style_blend_mode_at(document, layerId, 0), MS_BLEND_NORMAL);
    EXPECT_EQ(ms_layer_style_type_at(document, layerId, 5), MS_STYLE_INVALID);

    ms_command_add_fill_style(document, layerId);
    EXPECT_EQ(ms_layer_style_count(document, layerId), 2);

    ms_command_set_style_blend_mode(document, layerId, 1, MS_BLEND_SCREEN);
    EXPECT_EQ(ms_layer_style_blend_mode_at(document, layerId, 1), MS_BLEND_SCREEN);

    ms_command_set_style_blend_mode(document, layerId, 1, MS_BLEND_OVERLAY);
    EXPECT_EQ(ms_layer_style_blend_mode_at(document, layerId, 1), MS_BLEND_OVERLAY);

    // Out-of-range blend tags fall back to Normal.
    ms_command_set_style_blend_mode(document, layerId, 1, static_cast<MS_BLEND>(99));
    EXPECT_EQ(ms_layer_style_blend_mode_at(document, layerId, 1), MS_BLEND_NORMAL);

    ms_command_set_style_blend_mode(document, layerId, 1, MS_BLEND_SCREEN);
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_layer_style_blend_mode_at(document, layerId, 1), MS_BLEND_NORMAL);

    ms_command_remove_style(document, layerId, 1);
    EXPECT_EQ(ms_layer_style_count(document, layerId), 1);
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_layer_style_count(document, layerId), 2);
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_EQ(ms_layer_style_count(document, layerId), 1);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, ColorKeyframeLifecycle) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    EXPECT_FALSE(ms_property_is_animated(document, layerId, "styles[0].color"));
    ms_command_add_keyframe_color(document, layerId, "styles[0].color", 10, 1.0f, 0, 0, 1.0f);
    ms_command_add_keyframe_color(document, layerId, "styles[0].color", 20, 0, 0, 1.0f, 1.0f);
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "styles[0].color"));
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "styles[0].color"), 2);

    // Midpoint between the two linear keyframes blends the colors.
    float r = 0, g = 0, b = 0, a = 0;
    ms_property_evaluate_color(document, layerId, "styles[0].color", 15, &r, &g, &b, &a);
    EXPECT_FLOAT_EQ(r, 0.5f);
    EXPECT_FLOAT_EQ(g, 0.0f);
    EXPECT_FLOAT_EQ(b, 0.5f);
    EXPECT_FLOAT_EQ(a, 1.0f);

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_FALSE(ms_property_is_animated(document, layerId, "styles[0].color"));
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "styles[0].color"));

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, SetStaticValueUndoRedo) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ms_document_end_merge_group(document);

    ms_command_set_static_float(document, layerId, "transform.rotation", 45.0f);
    EXPECT_FLOAT_EQ(ms_property_static_float(document, layerId, "transform.rotation"), 45.0f);
    EXPECT_EQ(ms_property_type(document, layerId, "transform.rotation"), MS_VALUE_FLOAT);

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_FLOAT_EQ(ms_property_static_float(document, layerId, "transform.rotation"), 0.0f);
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_FLOAT_EQ(ms_property_static_float(document, layerId, "transform.rotation"), 45.0f);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, CompositionAppearanceCommandsUndoRedo) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    ms_command_set_composition_background_color(document, compositionId, 0.1f, 0.2f, 0.3f, 0.4f);
    ms_command_set_composition_corner_radius(document, compositionId, 24.0f);
    ms_document_end_merge_group(document);

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
    ms_composition_background_color(document, compositionId, &r, &g, &b, &a);
    EXPECT_FLOAT_EQ(r, 0.1f);
    EXPECT_FLOAT_EQ(g, 0.2f);
    EXPECT_FLOAT_EQ(b, 0.3f);
    EXPECT_FLOAT_EQ(a, 0.4f);
    EXPECT_FLOAT_EQ(ms_composition_corner_radius(document, compositionId), 24.0f);

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_FLOAT_EQ(ms_composition_corner_radius(document, compositionId), 0.0f);
    EXPECT_TRUE(ms_document_undo(document));
    ms_composition_background_color(document, compositionId, &r, &g, &b, &a);
    EXPECT_FLOAT_EQ(r, 0.0f);
    EXPECT_FLOAT_EQ(g, 0.0f);
    EXPECT_FLOAT_EQ(b, 0.0f);
    EXPECT_FLOAT_EQ(a, 1.0f);

    EXPECT_TRUE(ms_document_redo(document));
    ms_composition_background_color(document, compositionId, &r, &g, &b, &a);
    EXPECT_FLOAT_EQ(r, 0.1f);
    EXPECT_FLOAT_EQ(g, 0.2f);
    EXPECT_FLOAT_EQ(b, 0.3f);
    EXPECT_FLOAT_EQ(a, 0.4f);
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_FLOAT_EQ(ms_composition_corner_radius(document, compositionId), 24.0f);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, CompositionSettingsCommandsUndoRedo) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    ms_command_set_composition_size(document, compositionId, 1280, 720);
    ms_document_end_merge_group(document);
    ms_command_set_composition_duration(document, compositionId, 240);
    ms_document_end_merge_group(document);
    ms_command_set_composition_frame_rate(document, compositionId, 30000, 1001);
    ms_document_end_merge_group(document);

    EXPECT_EQ(ms_composition_width(document, compositionId), 1280);
    EXPECT_EQ(ms_composition_height(document, compositionId), 720);
    EXPECT_EQ(ms_composition_duration(document, compositionId), 240);
    EXPECT_EQ(ms_composition_frame_rate_num(document, compositionId), 30000);
    EXPECT_EQ(ms_composition_frame_rate_den(document, compositionId), 1001);

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_composition_frame_rate_num(document, compositionId), 30);
    EXPECT_EQ(ms_composition_frame_rate_den(document, compositionId), 1);
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_composition_duration(document, compositionId), 150);
    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_composition_width(document, compositionId), 1920);
    EXPECT_EQ(ms_composition_height(document, compositionId), 1080);

    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_EQ(ms_composition_width(document, compositionId), 1280);
    EXPECT_EQ(ms_composition_height(document, compositionId), 720);
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_EQ(ms_composition_duration(document, compositionId), 240);
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_EQ(ms_composition_frame_rate_num(document, compositionId), 30000);
    EXPECT_EQ(ms_composition_frame_rate_den(document, compositionId), 1001);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, LayerVisibleAndLockedUndo) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ms_document_end_merge_group(document);

    EXPECT_TRUE(ms_layer_visible(document, layerId));
    EXPECT_FALSE(ms_layer_locked(document, layerId));

    ms_command_set_layer_visible(document, layerId, false);
    ms_command_set_layer_locked(document, layerId, true);
    EXPECT_FALSE(ms_layer_visible(document, layerId));
    EXPECT_TRUE(ms_layer_locked(document, layerId));

    EXPECT_TRUE(ms_document_undo(document));  // undo lock
    EXPECT_FALSE(ms_layer_locked(document, layerId));
    EXPECT_TRUE(ms_document_undo(document));  // undo hide
    EXPECT_TRUE(ms_layer_visible(document, layerId));
    EXPECT_TRUE(ms_document_redo(document));
    EXPECT_FALSE(ms_layer_visible(document, layerId));

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, Vec2AndColorProperties) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    ms_command_set_static_vec2(document, layerId, "transform.position", 120.0f, 340.0f);
    float x = 0;
    float y = 0;
    ms_property_static_vec2(document, layerId, "transform.position", &x, &y);
    EXPECT_FLOAT_EQ(x, 120.0f);
    EXPECT_FLOAT_EQ(y, 340.0f);

    ms_command_set_static_color(document, layerId, "styles[0].color", 0.1f, 0.2f, 0.3f, 1.0f);
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;
    ms_property_static_color(document, layerId, "styles[0].color", &r, &g, &b, &a);
    EXPECT_FLOAT_EQ(r, 0.1f);
    EXPECT_FLOAT_EQ(g, 0.2f);
    EXPECT_FLOAT_EQ(b, 0.3f);
    EXPECT_FLOAT_EQ(a, 1.0f);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, KeyframeLifecycle) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ms_document_end_merge_group(document);

    ms_command_add_keyframe_float(document, layerId, "transform.rotation", 0, 0.0f);
    ms_command_add_keyframe_float(document, layerId, "transform.rotation", 60, 90.0f);
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "transform.rotation"));
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "transform.rotation"), 2);
    EXPECT_EQ(ms_property_keyframe_time_at(document, layerId, "transform.rotation", 0), 0);
    EXPECT_EQ(ms_property_keyframe_time_at(document, layerId, "transform.rotation", 1), 60);
    EXPECT_FLOAT_EQ(ms_property_keyframe_float_at(document, layerId, "transform.rotation", 1),
                    90.0f);

    // Evaluation at the midpoint (linear easing by default).
    EXPECT_FLOAT_EQ(ms_property_evaluate_float(document, layerId, "transform.rotation", 30),
                    45.0f);

    // Easing round trip.
    ms_command_set_easing(document, layerId, "transform.rotation", 0, MS_EASING_CUBIC_BEZIER,
                          0.42f, 0.0f, 1.0f, 1.0f);
    float inX = 0;
    float inY = 0;
    float outX = 0;
    float outY = 0;
    const MS_EASING easingType = ms_property_keyframe_easing_at(
        document, layerId, "transform.rotation", 0, &inX, &inY, &outX, &outY);
    EXPECT_EQ(easingType, MS_EASING_CUBIC_BEZIER);
    EXPECT_FLOAT_EQ(inX, 0.42f);
    EXPECT_FLOAT_EQ(outY, 1.0f);

    // Move then remove.
    ms_command_move_keyframe(document, layerId, "transform.rotation", 60, 90);
    EXPECT_EQ(ms_property_keyframe_time_at(document, layerId, "transform.rotation", 1), 90);
    ms_command_remove_keyframe(document, layerId, "transform.rotation", 90);
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "transform.rotation"), 1);

    // Undo the whole chain back to two keyframes.
    EXPECT_TRUE(ms_document_undo(document));  // remove
    EXPECT_TRUE(ms_document_undo(document));  // move
    EXPECT_TRUE(ms_document_undo(document));  // easing
    EXPECT_TRUE(ms_document_undo(document));  // second add
    EXPECT_TRUE(ms_document_undo(document));  // first add
    EXPECT_FALSE(ms_property_is_animated(document, layerId, "transform.rotation"));

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, SpatialTangentsAndMotionPath) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ms_document_end_merge_group(document);

    ms_command_add_keyframe_vec2(document, layerId, "transform.position", 0, 0.0f, 0.0f);
    ms_command_add_keyframe_vec2(document, layerId, "transform.position", 10, 100.0f, 0.0f);

    bool hasIn = true;
    bool hasOut = true;
    float inX = 1;
    float inY = 1;
    float outX = 1;
    float outY = 1;
    ASSERT_TRUE(ms_property_keyframe_spatial_at(document, layerId, "transform.position", 0, &hasIn,
                                                &inX, &inY, &hasOut, &outX, &outY));
    EXPECT_FALSE(hasIn);
    EXPECT_FALSE(hasOut);

    ms_command_set_spatial_tangents(document, layerId, "transform.position", 0, false, 0, 0, true,
                                    20.0f, 30.0f);
    ms_command_set_spatial_tangents(document, layerId, "transform.position", 10, true, -20.0f,
                                    30.0f, false, 0, 0);

    ASSERT_TRUE(ms_property_keyframe_spatial_at(document, layerId, "transform.position", 0, &hasIn,
                                                &inX, &inY, &hasOut, &outX, &outY));
    EXPECT_FALSE(hasIn);
    EXPECT_TRUE(hasOut);
    EXPECT_FLOAT_EQ(outX, 20.0f);
    EXPECT_FLOAT_EQ(outY, 30.0f);

    ASSERT_TRUE(ms_property_keyframe_spatial_at(document, layerId, "transform.position", 1, &hasIn,
                                                &inX, &inY, &hasOut, &outX, &outY));
    EXPECT_TRUE(hasIn);
    EXPECT_FALSE(hasOut);
    EXPECT_FLOAT_EQ(inX, -20.0f);
    EXPECT_FLOAT_EQ(inY, 30.0f);

    MSBezierPath *motionPath = ms_property_build_motion_path(document, layerId, "transform.position");
    ASSERT_NE(motionPath, nullptr);
    ASSERT_EQ(motionPath->count, 2u);
    EXPECT_FALSE(motionPath->closed);
    EXPECT_FLOAT_EQ(motionPath->vertices[0].pointX, 0.0f);
    EXPECT_FLOAT_EQ(motionPath->vertices[0].outTangentX, 20.0f);
    EXPECT_FLOAT_EQ(motionPath->vertices[0].outTangentY, 30.0f);
    EXPECT_FLOAT_EQ(motionPath->vertices[1].pointX, 100.0f);
    EXPECT_FLOAT_EQ(motionPath->vertices[1].inTangentX, -20.0f);
    EXPECT_FLOAT_EQ(motionPath->vertices[1].inTangentY, 30.0f);
    ms_bezier_path_free(motionPath);

    EXPECT_TRUE(ms_document_undo(document));  // clear frame 10 in
    ASSERT_TRUE(ms_property_keyframe_spatial_at(document, layerId, "transform.position", 1, &hasIn,
                                                &inX, &inY, &hasOut, &outX, &outY));
    EXPECT_FALSE(hasIn);

    EXPECT_EQ(ms_property_build_motion_path(document, layerId, "transform.rotation"), nullptr);
    EXPECT_FALSE(ms_property_keyframe_spatial_at(document, layerId, "no.such.property", 0, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr));

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, MissingPropertyIsSafe) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    EXPECT_EQ(ms_property_type(document, layerId, "no.such.property"), MS_VALUE_INVALID);
    EXPECT_FLOAT_EQ(ms_property_static_float(document, layerId, "no.such.property"), 0.0f);
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "no.such.property"), 0);
    EXPECT_FALSE(ms_property_is_animated(document, layerId, "no.such.property"));

    ms_document_destroy(document);
}

// Mirrors the app's threading: edits on the main actor while
// ReferenceFileDocument serializes on a background actor.
void SaveLoop(MSDocument *document, const std::atomic<bool> *stop) {
    while (!stop->load()) {
        char *json = ms_document_save(document);
        ms_string_free(json);
    }
}

void EditLoop(MSDocument *document, uint64_t layerId, const std::atomic<bool> *stop) {
    float value = 0;
    while (!stop->load()) {
        ms_command_set_static_float(document, layerId, "transform.rotation", value);
        value += 1;
    }
}

TEST(BridgeConcurrencyTest, SerializeWhileEditingIsSerialized) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    std::atomic<bool> stop{false};
    std::thread saver(SaveLoop, document, &stop);
    std::thread editor(EditLoop, document, layerId, &stop);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop.store(true);
    saver.join();
    editor.join();

    // The document remains consistent after concurrent access.
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 1);
    ms_document_destroy(document);
}

TEST(BridgeTest, NullHandlesAreSafe) {
    ms_document_destroy(nullptr);
    ms_canvas_destroy(nullptr);
    EXPECT_EQ(ms_document_undo(nullptr), false);
    EXPECT_EQ(ms_document_can_undo(nullptr), false);
    EXPECT_EQ(ms_document_save(nullptr), nullptr);
    EXPECT_EQ(ms_document_composition_count(nullptr), 0);
    EXPECT_EQ(ms_layer_name(nullptr, 1), nullptr);
    EXPECT_EQ(ms_document_load_json(nullptr, 0, nullptr), nullptr);
    ms_canvas_draw_frame(nullptr, nullptr, 0, 0);
    ms_canvas_set_draw_mode(nullptr, MS_CANVAS_DRAW_MODE_PLAYBACK);
    EXPECT_EQ(ms_canvas_get_draw_mode(nullptr), MS_CANVAS_DRAW_MODE_EDIT);
    ms_canvas_set_content_revision(nullptr, 7);
    EXPECT_EQ(ms_canvas_get_content_revision(nullptr), 0u);
    ms_command_add_keyframe_float(nullptr, 0, "transform.position", 0, 0.0f);
    ms_command_add_stroke_style(nullptr, 0);
    ms_command_set_stroke_position(nullptr, 0, 0, MS_STROKE_POSITION_INSIDE);
    EXPECT_EQ(ms_layer_style_stroke_position_at(nullptr, 0, 0), MS_STROKE_POSITION_INVALID);
}

TEST(BridgeCanvasTest, DrawModeApiNullSafe) {
    // Full canvas create needs Metal; getters on null document the defaults.
    EXPECT_EQ(ms_canvas_get_draw_mode(nullptr), MS_CANVAS_DRAW_MODE_EDIT);
    EXPECT_EQ(ms_canvas_get_content_revision(nullptr), 0u);
    ms_canvas_set_draw_mode(nullptr, MS_CANVAS_DRAW_MODE_PLAYBACK);
    ms_canvas_set_content_revision(nullptr, 99);
    EXPECT_EQ(ms_canvas_get_draw_mode(nullptr), MS_CANVAS_DRAW_MODE_EDIT);
    EXPECT_EQ(ms_canvas_get_content_revision(nullptr), 0u);
}

TEST(BridgeCanvasTest, FrameCommandCacheHitAndRevisionInvalidation) {
    motionstudio::FrameCommandCache cache;
    cache.invalidateIfStale(10, 1);
    motionstudio::FrameCommandCache::Entry entry;
    entry.viewportWidth = 100;
    entry.viewportHeight = 50;
    entry.layerCount = 2;
    cache.put(3, entry);

    const motionstudio::FrameCommandCache::Entry *found = cache.find(3);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->viewportWidth, 100);
    EXPECT_EQ(found->viewportHeight, 50);
    EXPECT_EQ(found->layerCount, 2u);
    EXPECT_EQ(cache.find(4), nullptr);

    cache.invalidateIfStale(10, 2);
    EXPECT_EQ(cache.find(3), nullptr);
    EXPECT_EQ(cache.size(), 0u);
}

TEST(BridgeCommandTest, StrokeStyleLifecycle) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    // Rect layers start with one default fill; append a stroke.
    ASSERT_EQ(ms_layer_style_count(document, layerId), 1);
    ms_command_add_stroke_style(document, layerId);
    ASSERT_EQ(ms_layer_style_count(document, layerId), 2);
    EXPECT_EQ(ms_layer_style_type_at(document, layerId, 1), MS_STYLE_STROKE);
    EXPECT_EQ(ms_layer_style_stroke_position_at(document, layerId, 1),
              MS_STROKE_POSITION_CENTER);
    // Position query on a fill reports invalid.
    EXPECT_EQ(ms_layer_style_stroke_position_at(document, layerId, 0), MS_STROKE_POSITION_INVALID);

    ms_command_set_stroke_position(document, layerId, 1, MS_STROKE_POSITION_INSIDE);
    EXPECT_EQ(ms_layer_style_stroke_position_at(document, layerId, 1),
              MS_STROKE_POSITION_INSIDE);
    // Out-of-range position tags fall back to Center.
    ms_command_set_stroke_position(document, layerId, 1, static_cast<MS_STROKE_POSITION>(99));
    EXPECT_EQ(ms_layer_style_stroke_position_at(document, layerId, 1),
              MS_STROKE_POSITION_CENTER);

    // Strokes carry their own blend mode.
    ms_command_set_style_blend_mode(document, layerId, 1, MS_BLEND_MULTIPLY);
    EXPECT_EQ(ms_layer_style_blend_mode_at(document, layerId, 1), MS_BLEND_MULTIPLY);

    // Width and trim properties animate through the generic float channel.
    ms_command_add_keyframe_float(document, layerId, "styles[1].width", 0, 2.0f);
    ms_command_add_keyframe_float(document, layerId, "styles[1].trimStart", 0, 0.25f);
    ms_command_add_keyframe_float(document, layerId, "styles[1].trimOffset", 10, 90.0f);
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "styles[1].width"));
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "styles[1].trimStart"));
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "styles[1].trimOffset"));
    EXPECT_FLOAT_EQ(ms_property_evaluate_float(document, layerId, "styles[1].trimStart", 5),
                    0.25f);

    // Undo walks back every stroke edit in reverse order; the two
    // consecutive position sets merge into a single undo step.
    EXPECT_TRUE(ms_document_undo(document));  // trimOffset keyframe
    EXPECT_TRUE(ms_document_undo(document));  // trimStart keyframe
    EXPECT_TRUE(ms_document_undo(document));  // width keyframe
    EXPECT_TRUE(ms_document_undo(document));  // blend mode
    EXPECT_TRUE(ms_document_undo(document));  // position (merged)
    EXPECT_TRUE(ms_document_undo(document));  // add stroke
    EXPECT_EQ(ms_layer_style_count(document, layerId), 1);
    EXPECT_FALSE(ms_property_is_animated(document, layerId, "styles[1].width"));

    ms_document_destroy(document);
}

TEST(BridgeBezierPathTest, StaticRoundTripAndKeyframe) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_path_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);
    EXPECT_EQ(ms_property_type(document, layerId, "path"), MS_VALUE_BEZIER_PATH);

    MSBezierVertex vertices[2] = {
        {0, 0, 0, 0, 1, 0},
        {10, 0, -1, 0, 0, 0},
    };
    MSBezierPath input;
    input.vertices = vertices;
    input.count = 2;
    input.closed = false;

    ms_command_set_static_bezier_path(document, layerId, "path", &input);
    MSBezierPath *loaded = ms_property_static_bezier_path(document, layerId, "path");
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->count, 2u);
    EXPECT_FALSE(loaded->closed);
    EXPECT_FLOAT_EQ(loaded->vertices[0].pointX, 0);
    EXPECT_FLOAT_EQ(loaded->vertices[1].pointX, 10);
    EXPECT_FLOAT_EQ(loaded->vertices[0].outTangentX, 1);
    ms_bezier_path_free(loaded);

    MSBezierVertex keyedVertices[2] = {
        {0, 0, 0, 0, 0, 0},
        {20, 5, 0, 0, 0, 0},
    };
    MSBezierPath keyed;
    keyed.vertices = keyedVertices;
    keyed.count = 2;
    keyed.closed = true;
    ms_command_add_keyframe_bezier_path(document, layerId, "path", 10, &keyed);
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "path"));
    MSBezierPath *evaluated = ms_property_evaluate_bezier_path(document, layerId, "path", 10);
    ASSERT_NE(evaluated, nullptr);
    EXPECT_TRUE(evaluated->closed);
    EXPECT_FLOAT_EQ(evaluated->vertices[1].pointX, 20);
    ms_bezier_path_free(evaluated);

    ms_document_destroy(document);
}

TEST(BridgeBezierPathTest, ConvertGeometryAndAddPathLayer) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t rectId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(rectId, 0u);

    // Rect has no "path" until converted.
    EXPECT_EQ(ms_property_type(document, rectId, "path"), MS_VALUE_INVALID);
    ms_command_convert_geometry_to_path(document, rectId, 0);
    EXPECT_EQ(ms_property_type(document, rectId, "path"), MS_VALUE_BEZIER_PATH);
    MSBezierPath *path = ms_property_static_bezier_path(document, rectId, "path");
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(path->closed);
    EXPECT_GE(path->count, 4u);
    ms_bezier_path_free(path);

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_property_type(document, rectId, "path"), MS_VALUE_INVALID);

    ms_document_destroy(document);
}

TEST(BridgeBezierPathTest, ToggleSmoothAndRecenterShape) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_path_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);

    ms_command_path_edit_append_vertex(document, layerId, MS_PATH_EDIT_SHAPE, 0, 0, 100, 100);
    ms_command_path_edit_append_vertex(document, layerId, MS_PATH_EDIT_SHAPE, 0, 0, 200, 100);
    ms_command_path_edit_append_vertex(document, layerId, MS_PATH_EDIT_SHAPE, 0, 0, 200, 200);

    ms_command_path_edit_toggle_smooth(document, layerId, MS_PATH_EDIT_SHAPE, 0, 0, 1);
    MSBezierPath *smooth = ms_property_static_bezier_path(document, layerId, "path");
    ASSERT_NE(smooth, nullptr);
    ASSERT_EQ(smooth->count, 3u);
    EXPECT_NE(smooth->vertices[1].inTangentX, 0);
    EXPECT_NE(smooth->vertices[1].outTangentY, 0);
    ms_bezier_path_free(smooth);

    float posX = 0;
    float posY = 0;
    ms_property_static_vec2(document, layerId, "transform.position", &posX, &posY);
    ms_command_path_edit_close(document, layerId, MS_PATH_EDIT_SHAPE, 0, 0);
    MSBezierPath *closed = ms_property_static_bezier_path(document, layerId, "path");
    ASSERT_NE(closed, nullptr);
    EXPECT_TRUE(closed->closed);
    // Bounds center should sit near local origin after close+recenter.
    float minX = closed->vertices[0].pointX;
    float maxX = minX;
    float minY = closed->vertices[0].pointY;
    float maxY = minY;
    for (size_t i = 0; i < closed->count; ++i) {
        minX = std::min(minX, closed->vertices[i].pointX);
        maxX = std::max(maxX, closed->vertices[i].pointX);
        minY = std::min(minY, closed->vertices[i].pointY);
        maxY = std::max(maxY, closed->vertices[i].pointY);
    }
    EXPECT_NEAR((minX + maxX) * 0.5f, 0.0f, 1e-3f);
    EXPECT_NEAR((minY + maxY) * 0.5f, 0.0f, 1e-3f);
    ms_bezier_path_free(closed);

    float newX = 0;
    float newY = 0;
    ms_property_static_vec2(document, layerId, "transform.position", &newX, &newY);
    // Position absorbs the local center so the silhouette stays put.
    EXPECT_NE(newX, posX);
    EXPECT_NE(newY, posY);

    ms_document_destroy(document);
}

TEST(BridgeBezierPathTest, MorphEvaluateMidpointAndKeyframeTimes) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_path_layer(document, compositionId);

    MSBezierVertex a[2] = {
        {0, 0, 0, 0, 0, 0},
        {10, 0, 0, 0, 0, 0},
    };
    MSBezierPath from;
    from.vertices = a;
    from.count = 2;
    from.closed = false;

    MSBezierVertex b[2] = {
        {20, 0, 0, 0, 0, 0},
        {30, 0, 0, 0, 0, 0},
    };
    MSBezierPath to;
    to.vertices = b;
    to.count = 2;
    to.closed = false;

    ms_command_add_keyframe_bezier_path(document, layerId, "path", 0, &from);
    ms_command_add_keyframe_bezier_path(document, layerId, "path", 20, &to);
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "path"), 2);
    EXPECT_EQ(ms_property_keyframe_time_at(document, layerId, "path", 0), 0);
    EXPECT_EQ(ms_property_keyframe_time_at(document, layerId, "path", 1), 20);

    MSBezierPath *mid = ms_property_evaluate_bezier_path(document, layerId, "path", 10);
    ASSERT_NE(mid, nullptr);
    ASSERT_EQ(mid->count, 2u);
    EXPECT_FLOAT_EQ(mid->vertices[0].pointX, 10.0f);
    EXPECT_FLOAT_EQ(mid->vertices[1].pointX, 20.0f);
    ms_bezier_path_free(mid);
    ms_document_destroy(document);
}

TEST(BridgeBezierPathTest, WriteAtPlayheadStaticThenAnimated) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_path_layer(document, compositionId);

    MSBezierVertex v0[2] = {
        {0, 0, 0, 0, 0, 0},
        {5, 0, 0, 0, 0, 0},
    };
    MSBezierPath staticPath;
    staticPath.vertices = v0;
    staticPath.count = 2;
    staticPath.closed = false;
    ms_command_write_bezier_path_at_playhead(document, layerId, "path", 0, &staticPath);
    EXPECT_FALSE(ms_property_is_animated(document, layerId, "path"));

    ms_command_add_keyframe_bezier_path(document, layerId, "path", 0, &staticPath);
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "path"));

    MSBezierVertex v1[2] = {
        {0, 0, 0, 0, 0, 0},
        {15, 0, 0, 0, 0, 0},
    };
    MSBezierPath keyed;
    keyed.vertices = v1;
    keyed.count = 2;
    keyed.closed = false;
    ms_command_write_bezier_path_at_playhead(document, layerId, "path", 10, &keyed);
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "path"), 2);

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, ImageAssetImportAddLayerBindAndUndo) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    // Temp package root with assets/.
    const auto root = std::filesystem::temp_directory_path() /
        ("ms_image_bridge_" + std::to_string(reinterpret_cast<uintptr_t>(document)));
    std::filesystem::create_directories(root / "assets");
    ms_document_set_project_root(document, root.string().c_str());

    const auto source = root / "source.png";
    {
        // Minimal valid 1x1 PNG.
        static const unsigned char kPng[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
            0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
            0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8,
            0xCF, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xFE, 0x02, 0xFE, 0x00, 0x00,
            0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
        std::ofstream out(source, std::ios::binary);
        out.write(reinterpret_cast<const char *>(kPng), sizeof(kPng));
    }

    const uint64_t assetId =
        ms_command_import_image_asset(document, source.string().c_str(), "photo.png", 64, 32);
    ASSERT_NE(assetId, 0u);
    EXPECT_EQ(ms_document_asset_count(document), 1);
    EXPECT_EQ(ms_asset_width(document, assetId), 64);
    EXPECT_EQ(ms_asset_height(document, assetId), 32);
    EXPECT_TRUE(std::filesystem::exists(root / "assets" / "photo.png"));

    const uint64_t layerId = ms_command_add_image_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);
    EXPECT_EQ(ms_layer_image_asset_id(document, layerId), 0u);
    EXPECT_EQ(ms_layer_image_scale_mode(document, layerId), MS_IMAGE_SCALE_LETTER_BOX);

    ASSERT_TRUE(ms_layer_set_image_asset(document, layerId, assetId));
    EXPECT_EQ(ms_layer_image_asset_id(document, layerId), assetId);
    ms_layer_set_image_scale_mode(document, layerId, MS_IMAGE_SCALE_STRETCH);
    EXPECT_EQ(ms_layer_image_scale_mode(document, layerId), MS_IMAGE_SCALE_STRETCH);

    ASSERT_TRUE(ms_document_undo(document));  // scale mode
    EXPECT_EQ(ms_layer_image_scale_mode(document, layerId), MS_IMAGE_SCALE_LETTER_BOX);
    ASSERT_TRUE(ms_document_undo(document));  // bind
    EXPECT_EQ(ms_layer_image_asset_id(document, layerId), 0u);
    ASSERT_TRUE(ms_document_undo(document));  // add layer
    ASSERT_TRUE(ms_document_undo(document));  // import asset
    EXPECT_EQ(ms_document_asset_count(document), 0);
    EXPECT_TRUE(std::filesystem::exists(root / "assets" / "photo.png"));

    std::error_code error;
    std::filesystem::remove_all(root, error);
    ms_document_destroy(document);
}

TEST(BridgeCommandTest, TextLayerAddSetStringFontAndUndo) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    const uint64_t layerId = ms_command_add_text_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);
    EXPECT_EQ(ms_layer_type(document, layerId), MS_LAYER_TEXT);
    EXPECT_FALSE(ms_layer_text_box_text_mode(document, layerId));
    EXPECT_EQ(ms_layer_text_align(document, layerId), MS_TEXT_ALIGN_LEFT);
    {
        BridgeString family;
        family.value = ms_layer_text_font_family(document, layerId);
        EXPECT_EQ(family.str(), "PingFang SC");
    }
    {
        BridgeString text;
        text.value = ms_property_static_string(document, layerId, "content.text");
        EXPECT_EQ(text.str(), "Text");
    }
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    ms_property_static_vec2(document, layerId, "transform.anchorPoint", &anchorX, &anchorY);
    EXPECT_FLOAT_EQ(anchorX, 200.0f);
    EXPECT_FLOAT_EQ(anchorY, 60.0f);

    ms_command_set_static_string(document, layerId, "content.text", "Hello\nWorld");
    {
        BridgeString text;
        text.value = ms_property_static_string(document, layerId, "content.text");
        EXPECT_EQ(text.str(), "Hello\nWorld");
    }

    ASSERT_TRUE(ms_command_set_text_box_text_mode(document, layerId, true));
    EXPECT_TRUE(ms_layer_text_box_text_mode(document, layerId));
    ASSERT_TRUE(ms_command_set_text_align(document, layerId, MS_TEXT_ALIGN_CENTER));
    EXPECT_EQ(ms_layer_text_align(document, layerId), MS_TEXT_ALIGN_CENTER);
    ASSERT_TRUE(ms_command_set_text_font(document, layerId, "Helvetica", "Bold"));
    {
        BridgeString family;
        family.value = ms_layer_text_font_family(document, layerId);
        EXPECT_EQ(family.str(), "Helvetica");
        BridgeString style;
        style.value = ms_layer_text_font_style(document, layerId);
        EXPECT_EQ(style.str(), "Bold");
    }

    ASSERT_TRUE(ms_document_undo(document));  // font
    {
        BridgeString family;
        family.value = ms_layer_text_font_family(document, layerId);
        EXPECT_EQ(family.str(), "PingFang SC");
        BridgeString style;
        style.value = ms_layer_text_font_style(document, layerId);
        EXPECT_EQ(style.str(), "");
    }
    ASSERT_TRUE(ms_document_undo(document));  // align
    EXPECT_EQ(ms_layer_text_align(document, layerId), MS_TEXT_ALIGN_LEFT);
    ASSERT_TRUE(ms_document_undo(document));  // boxTextMode
    EXPECT_FALSE(ms_layer_text_box_text_mode(document, layerId));

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, ResizeLayerGeometryScalesPathMaskAndRect) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t pathId = ms_command_add_path_layer(document, compositionId);
    ASSERT_NE(pathId, 0u);

    MSBezierVertex vertices[2] = {
        {0, 0, 0, 0, 2, 0},
        {10, 0, -2, 0, 0, 0},
    };
    MSBezierPath input;
    input.vertices = vertices;
    input.count = 2;
    input.closed = false;
    ms_command_set_static_bezier_path(document, pathId, "path", &input);
    ms_command_add_mask(document, pathId, 0);
    ASSERT_EQ(ms_layer_mask_count(document, pathId), 1);

    MSBezierVertex maskVertices[2] = {
        {1, 1, 0, 0, 0, 0},
        {3, 3, 0, 0, 0, 0},
    };
    MSBezierPath maskPath;
    maskPath.vertices = maskVertices;
    maskPath.count = 2;
    maskPath.closed = false;
    ms_command_set_static_bezier_path(document, pathId, "masks[0].path", &maskPath);

    float scaleX = 0.0f;
    float scaleY = 0.0f;
    ms_property_static_vec2(document, pathId, "transform.scale", &scaleX, &scaleY);
    EXPECT_FLOAT_EQ(scaleX, 1.0f);
    EXPECT_FLOAT_EQ(scaleY, 1.0f);

    ASSERT_TRUE(ms_command_resize_layer_geometry(document, pathId, 0, 0.0f, 0.0f, 2.0f, 2.0f));

    MSBezierPath *scaledPath = ms_property_static_bezier_path(document, pathId, "path");
    ASSERT_NE(scaledPath, nullptr);
    ASSERT_EQ(scaledPath->count, 2u);
    EXPECT_FLOAT_EQ(scaledPath->vertices[0].pointX, 0);
    EXPECT_FLOAT_EQ(scaledPath->vertices[1].pointX, 20);
    EXPECT_FLOAT_EQ(scaledPath->vertices[0].outTangentX, 4);
    EXPECT_FLOAT_EQ(scaledPath->vertices[1].inTangentX, -4);
    ms_bezier_path_free(scaledPath);

    MSBezierPath *scaledMask = ms_property_static_bezier_path(document, pathId, "masks[0].path");
    ASSERT_NE(scaledMask, nullptr);
    ASSERT_EQ(scaledMask->count, 2u);
    EXPECT_FLOAT_EQ(scaledMask->vertices[0].pointX, 2);
    EXPECT_FLOAT_EQ(scaledMask->vertices[1].pointY, 6);
    ms_bezier_path_free(scaledMask);

    ms_property_static_vec2(document, pathId, "transform.scale", &scaleX, &scaleY);
    EXPECT_FLOAT_EQ(scaleX, 1.0f);
    EXPECT_FLOAT_EQ(scaleY, 1.0f);

    // Path + mask writes are separate undo steps unless wrapped in a merge group.
    ASSERT_TRUE(ms_document_undo(document));  // mask
    ASSERT_TRUE(ms_document_undo(document));  // path
    MSBezierPath *restored = ms_property_static_bezier_path(document, pathId, "path");
    ASSERT_NE(restored, nullptr);
    EXPECT_FLOAT_EQ(restored->vertices[1].pointX, 10);
    ms_bezier_path_free(restored);

    const uint64_t rectId = ms_command_add_rect_layer(document, compositionId);
    ms_document_begin_merge_group(document);
    ASSERT_TRUE(ms_command_resize_layer_geometry(document, rectId, 0, -100.0f, 0.0f, 2.0f, 1.0f));
    ms_document_end_merge_group(document);
    float posX = 0.0f;
    float posY = 0.0f;
    float sizeX = 0.0f;
    float sizeY = 0.0f;
    ms_property_static_vec2(document, rectId, "position", &posX, &posY);
    ms_property_static_vec2(document, rectId, "size", &sizeX, &sizeY);
    EXPECT_FLOAT_EQ(posX, 100.0f);
    EXPECT_FLOAT_EQ(posY, 0.0f);
    EXPECT_FLOAT_EQ(sizeX, 400.0f);
    EXPECT_FLOAT_EQ(sizeY, 200.0f);

    EXPECT_FALSE(ms_command_resize_layer_geometry(document, 0, 0, 0, 0, 2, 2));

    ms_document_destroy(document);
}

TEST(BridgeCommandTest, ResizeLayerGeometryKeepsOuterDragMergeOpen) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t pathId = ms_command_add_path_layer(document, compositionId);
    MSBezierVertex vertices[2] = {
        {0, 0, 0, 0, 0, 0},
        {10, 0, 0, 0, 0, 0},
    };
    MSBezierPath input;
    input.vertices = vertices;
    input.count = 2;
    input.closed = false;
    ms_command_set_static_bezier_path(document, pathId, "path", &input);

    ms_document_begin_merge_group(document);
    ASSERT_TRUE(ms_command_resize_layer_geometry(document, pathId, 0, 0.0f, 0.0f, 2.0f, 2.0f));
    ms_command_set_static_vec2(document, pathId, "transform.position", 50.0f, 60.0f);
    ms_document_end_merge_group(document);

    // One undo must revert both the geometry resize and the position write.
    ASSERT_TRUE(ms_document_undo(document));
    MSBezierPath *restored = ms_property_static_bezier_path(document, pathId, "path");
    ASSERT_NE(restored, nullptr);
    EXPECT_FLOAT_EQ(restored->vertices[1].pointX, 10);
    ms_bezier_path_free(restored);
    float posX = 0.0f;
    float posY = 0.0f;
    ms_property_static_vec2(document, pathId, "transform.position", &posX, &posY);
    EXPECT_NE(posX, 50.0f);

    ms_document_destroy(document);
}

}  // namespace
