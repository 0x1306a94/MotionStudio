#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

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

    MSDocument *loaded = ms_document_load(saved.value, std::strlen(saved.value), nullptr);
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
    MSDocument *document = ms_document_load("not json", 8, &error);
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
    const int easingType = ms_property_keyframe_easing_at(
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

TEST(BridgeCommandTest, MissingPropertyIsSafe) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    EXPECT_EQ(ms_property_type(document, layerId, "no.such.property"), -1);
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
    EXPECT_EQ(ms_document_load(nullptr, 0, nullptr), nullptr);
    ms_canvas_draw_frame(nullptr, nullptr, 0, 0);
    ms_command_add_keyframe_float(nullptr, 0, "transform.position", 0, 0.0f);
}

}  // namespace
