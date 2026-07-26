// Motion Studio C ABI bridge.
//
// Thin C boundary over the C++ core so platform UI shells (Swift/AppKit,
// SwiftUI, future WASM) can drive the document model without C++ interop.
// All handles are opaque; all strings returned by this API are malloc'd and
// must be released with ms_string_free. Functions are null-safe: passing a
// null handle yields a no-op or a zero/default return.

#ifndef MOTIONSTUDIO_BRIDGE_H
#define MOTIONSTUDIO_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MSDocument MSDocument;
typedef struct MSCanvas MSCanvas;

typedef struct MSCanvasFrameProfile {
    bool drewFrame;
    uint64_t layerCount;
    uint64_t drawCommandCount;
    double totalMs;
    double documentLockMs;
    double sceneEvaluateMs;
    double buildCommandsMs;
    double beginFrameMs;
    double playCommandsMs;
    double endFrameMs;
    double endFrameCanvasRestoreMs;
    double endFramePresentMs;
    double endFrameFlushSubmitMs;
    double endFrameDeviceUnlockMs;
} MSCanvasFrameProfile;

// Layer content type tag, mirrors motion::LayerType.
enum {
    MS_LAYER_SHAPE = 0,
    MS_LAYER_IMAGE = 1,
    MS_LAYER_TEXT = 2,
    MS_LAYER_GROUP = 3,
    MS_LAYER_PRECOMP = 4,
};

// Layer style type tag, mirrors motion::LayerStyleType.
enum {
    MS_STYLE_FILL = 0,
    MS_STYLE_STROKE = 1,
};

// Stroke position tag, mirrors motion::StrokePosition.
enum {
    MS_STROKE_POSITION_CENTER = 0,
    MS_STROKE_POSITION_INSIDE = 1,
    MS_STROKE_POSITION_OUTSIDE = 2,
};

// Blend mode tag, mirrors motion::BlendMode ordinals (Lottie "bm" value set).
enum {
    MS_BLEND_NORMAL = 0,
    MS_BLEND_MULTIPLY = 1,
    MS_BLEND_SCREEN = 2,
    MS_BLEND_OVERLAY = 3,
    MS_BLEND_DARKEN = 4,
    MS_BLEND_LIGHTEN = 5,
    MS_BLEND_COLOR_DODGE = 6,
    MS_BLEND_COLOR_BURN = 7,
    MS_BLEND_HARD_LIGHT = 8,
    MS_BLEND_SOFT_LIGHT = 9,
    MS_BLEND_DIFFERENCE = 10,
    MS_BLEND_EXCLUSION = 11,
    MS_BLEND_HUE = 12,
    MS_BLEND_SATURATION = 13,
    MS_BLEND_COLOR = 14,
    MS_BLEND_LUMINOSITY = 15,
    MS_BLEND_ADD = 16,
};

// Animatable value type tag, mirrors motion::AnimatableType.
enum {
    MS_VALUE_FLOAT = 0,
    MS_VALUE_VEC2 = 1,
    MS_VALUE_COLOR = 2,
    MS_VALUE_BEZIER_PATH = 3,
    MS_VALUE_STRING = 4,
};

// Easing type tag, mirrors motion::EasingType.
enum {
    MS_EASING_LINEAR = 0,
    MS_EASING_HOLD = 1,
    MS_EASING_EASE = 2,
    MS_EASING_EASE_IN = 3,
    MS_EASING_EASE_OUT = 4,
    MS_EASING_EASE_IN_OUT = 5,
    MS_EASING_CUBIC_BEZIER = 6,
};

enum {
    MS_PREVIWER_BACKDROP_BLACK = 0,
    MS_PREVIWER_BACKDROP_TRANSPARENT = 1,
};

/* ============================ lifecycle ============================ */

// Creates a new document containing one default composition
// (1920x1080, 30 fps, 150 frames).
MSDocument *ms_document_create(void);

// Loads a document from JSON text.
// jsonText: JSON payload (need not be null-terminated).
// length: byte length of jsonText.
// errorOut: optional; on failure receives a malloc'd error message.
// Returns NULL on failure.
MSDocument *ms_document_load(const char *jsonText, size_t length, char **errorOut);

void ms_document_destroy(MSDocument *document);

// Serializes the document to indented JSON.
// Returns a malloc'd null-terminated string; release with ms_string_free.
char *ms_document_save(MSDocument *document);

// Frees any string returned by this API.
void ms_string_free(char *string);

/* ============================ undo / redo ============================ */

// Executes one undo step. Returns false when there is nothing to undo.
bool ms_document_undo(MSDocument *document);
// Executes one redo step. Returns false when there is nothing to redo.
bool ms_document_redo(MSDocument *document);
bool ms_document_can_undo(MSDocument *document);
bool ms_document_can_redo(MSDocument *document);

// Description of the next undo/redo step ("Move Keyframe").
// Returns a malloc'd string, or NULL when the stack is empty.
char *ms_document_undo_description(MSDocument *document);
char *ms_document_redo_description(MSDocument *document);

// Closes the merge window so the next command starts a new undo unit.
// Call on drag end.
// Opens a drag transaction: mixed property edits pack into one undo unit
// until ms_document_end_merge_group.
void ms_document_begin_merge_group(MSDocument *document);
void ms_document_end_merge_group(MSDocument *document);

/* ============================ composition queries ============================ */

int ms_document_composition_count(MSDocument *document);
uint64_t ms_document_composition_id_at(MSDocument *document, int index);
int64_t ms_composition_duration(MSDocument *document, uint64_t compositionId);
int ms_composition_width(MSDocument *document, uint64_t compositionId);
int ms_composition_height(MSDocument *document, uint64_t compositionId);
int ms_composition_frame_rate_num(MSDocument *document, uint64_t compositionId);
int ms_composition_frame_rate_den(MSDocument *document, uint64_t compositionId);
int ms_composition_layer_count(MSDocument *document, uint64_t compositionId);
void ms_composition_background_color(MSDocument *document, uint64_t compositionId, float *r, float *g, float *b, float *a);
float ms_composition_corner_radius(MSDocument *document, uint64_t compositionId);
// Topmost unlocked layer hit at scene-space point, or 0 when none hit.
uint64_t ms_composition_hit_test_layer(MSDocument *document, uint64_t compositionId, double frameTime, float x, float y, float tolerance);
bool ms_composition_layer_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                                 float *minX, float *minY, float *maxX, float *maxY);

// Selection chrome geometry in scene space for free-transform hit-testing.
// corners / edgeMids are TL,TR,BR,BL and top,right,bottom,left respectively.
// primaryLayerId selects the anchor owner (AE primary). Returns false when
// nothing selected has bounds; out may be null.
typedef struct MSSelectionHandles {
    int valid;
    int isOriented;
    float cornersX[4];
    float cornersY[4];
    float edgeMidsX[4];
    float edgeMidsY[4];
    float centerX;
    float centerY;
    float anchorX;
    float anchorY;
    uint64_t primaryLayerId;
    float boxRotationDegrees;
    float localMinX;
    float localMinY;
    float localMaxX;
    float localMaxY;
} MSSelectionHandles;

bool ms_composition_selection_handles(MSDocument *document, uint64_t compositionId, double frameTime,
                                      const uint64_t *layerIds, size_t count, uint64_t primaryLayerId,
                                      MSSelectionHandles *out);

// Hit-tests selection chrome. Returns MS_SELECTION_HANDLE_* (0 = none).
// Radii are scene units.
enum {
    MS_SELECTION_HANDLE_NONE = 0,
    MS_SELECTION_HANDLE_ANCHOR = 1,
    MS_SELECTION_HANDLE_SCALE_CORNER0 = 2,
    MS_SELECTION_HANDLE_SCALE_CORNER1 = 3,
    MS_SELECTION_HANDLE_SCALE_CORNER2 = 4,
    MS_SELECTION_HANDLE_SCALE_CORNER3 = 5,
    MS_SELECTION_HANDLE_SCALE_EDGE0 = 6,
    MS_SELECTION_HANDLE_SCALE_EDGE1 = 7,
    MS_SELECTION_HANDLE_SCALE_EDGE2 = 8,
    MS_SELECTION_HANDLE_SCALE_EDGE3 = 9,
    MS_SELECTION_HANDLE_ROTATE0 = 10,
    MS_SELECTION_HANDLE_ROTATE1 = 11,
    MS_SELECTION_HANDLE_ROTATE2 = 12,
    MS_SELECTION_HANDLE_ROTATE3 = 13,
};
int ms_selection_handles_hit_test(const MSSelectionHandles *handles, float x, float y,
                                  float handleHitRadius, float rotateInner, float rotateOuter);

// Composition name (malloc'd).
char *ms_composition_name(MSDocument *document, uint64_t compositionId);

/* ============================ layer queries ============================ */

// ID of the layer at index within the composition (0 = bottommost).
// Returns 0 on out-of-range.
uint64_t ms_layer_id_at(MSDocument *document, uint64_t compositionId, int index);

// Layer name (malloc'd).
char *ms_layer_name(MSDocument *document, uint64_t layerId);
// Layer type tag (MS_LAYER_*), -1 when the layer does not exist.
int ms_layer_type(MSDocument *document, uint64_t layerId);
int64_t ms_layer_in_point(MSDocument *document, uint64_t layerId);
int64_t ms_layer_out_point(MSDocument *document, uint64_t layerId);
// Parent layer ID, 0 when the layer has no parent or does not exist.
uint64_t ms_layer_parent_id(MSDocument *document, uint64_t layerId);
bool ms_layer_visible(MSDocument *document, uint64_t layerId);
bool ms_layer_locked(MSDocument *document, uint64_t layerId);

/* ============================ layer style queries ============================ */

// Number of styles (fills/strokes) on the layer; 0 when the layer does not exist.
int ms_layer_style_count(MSDocument *document, uint64_t layerId);
// Style type tag (MS_STYLE_*) at index, -1 when out of range.
int ms_layer_style_type_at(MSDocument *document, uint64_t layerId, int index);
// Blend mode tag (MS_BLEND_*) of the fill at index, -1 when out of range or
// the style is not a fill.
int ms_layer_style_blend_mode_at(MSDocument *document, uint64_t layerId, int index);
// Stroke position of the style at index, or -1 when not a stroke.
int ms_layer_style_stroke_position_at(MSDocument *document, uint64_t layerId, int index);

/* ============================ property queries ============================ */
// entityId: ID of the owning Layer or ShapeElement.
// path: dot-separated property path. Layer examples: "transform.position", "size", "styles[0].color".
// ShapeElement examples: "path", "size", "cornerRadius".

// Value type tag (MS_VALUE_*), -1 when the property does not exist.
int ms_property_type(MSDocument *document, uint64_t entityId, const char *path);
bool ms_property_is_animated(MSDocument *document, uint64_t entityId, const char *path);

// Static value accessors. Return zero/fill nothing when the property does not
// exist or the type does not match.
float ms_property_static_float(MSDocument *document, uint64_t entityId, const char *path);
void ms_property_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float *x, float *y);
void ms_property_static_color(MSDocument *document, uint64_t entityId, const char *path, float *r, float *g, float *b, float *a);

// Keyframe accessors (index into the ascending-time keyframe list).
int ms_property_keyframe_count(MSDocument *document, uint64_t entityId, const char *path);
int64_t ms_property_keyframe_time_at(MSDocument *document, uint64_t entityId, const char *path, int index);
float ms_property_keyframe_float_at(MSDocument *document, uint64_t entityId, const char *path, int index);
void ms_property_keyframe_vec2_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *x, float *y);

// Easing of the keyframe at index. Returns the easing type tag (MS_EASING_*);
// bezier control points are written to the out parameters for bezier-backed
// easings. Returns -1 when the keyframe does not exist.
int ms_property_keyframe_easing_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *inX, float *inY, float *outX, float *outY);

// Value of the property evaluated at the given frame.
float ms_property_evaluate_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame);
void ms_property_evaluate_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *x, float *y);
void ms_property_evaluate_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *r, float *g, float *b, float *a);

/* ============================ commands ============================ */
// Every function below executes a command through the document's undo
// manager, so each call is undoable.

void ms_command_set_static_float(MSDocument *document, uint64_t entityId, const char *path, float value);
void ms_command_set_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float x, float y);
void ms_command_set_static_color(MSDocument *document, uint64_t entityId, const char *path, float r, float g, float b, float a);
void ms_command_set_composition_background_color(MSDocument *document, uint64_t compositionId, float r, float g, float b, float a);
void ms_command_set_composition_corner_radius(MSDocument *document, uint64_t compositionId, float cornerRadius);
void ms_command_set_composition_size(MSDocument *document, uint64_t compositionId, int width, int height);
void ms_command_set_composition_duration(MSDocument *document, uint64_t compositionId, int64_t duration);
void ms_command_set_composition_frame_rate(MSDocument *document, uint64_t compositionId, int frameRateNum, int frameRateDen);

// Adds a keyframe holding the property's given value at frame.
void ms_command_add_keyframe_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float value);
void ms_command_add_keyframe_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x, float y);
void ms_command_add_keyframe_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float r, float g, float b, float a);
void ms_command_remove_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t frame);
void ms_command_move_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t oldFrame, int64_t newFrame);
// easingType: MS_EASING_* tag. Control points are only used for MS_EASING_CUBIC_BEZIER.
void ms_command_set_easing(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, int easingType, float inX, float inY, float outX, float outY);

// Adds a rectangle/ellipse shape layer (200x200, centered on the composition,
// default fill, spanning the full composition duration).
// Returns the new layer ID, 0 on failure.
uint64_t ms_command_add_rect_layer(MSDocument *document, uint64_t compositionId);
uint64_t ms_command_add_ellipse_layer(MSDocument *document, uint64_t compositionId);
void ms_command_remove_layer(MSDocument *document, uint64_t compositionId, uint64_t layerId);
void ms_command_move_layer(MSDocument *document, uint64_t compositionId, int fromIndex, int toIndex);
void ms_command_set_layer_visible(MSDocument *document, uint64_t layerId, bool visible);
void ms_command_set_layer_locked(MSDocument *document, uint64_t layerId, bool locked);

// Appends a default fill (black, normal blend) to the layer's style list.
void ms_command_add_fill_style(MSDocument *document, uint64_t layerId);
// Appends a default stroke (black, width 2, normal blend, center) to the
// layer's style list.
void ms_command_add_stroke_style(MSDocument *document, uint64_t layerId);
// Removes the style at index from the layer's style list.
void ms_command_remove_style(MSDocument *document, uint64_t layerId, int index);
// blendMode: MS_BLEND_* tag. Applies to fill and stroke styles.
void ms_command_set_style_blend_mode(MSDocument *document, uint64_t layerId, int index, int blendMode);
// position: MS_STROKE_POSITION_* tag. Only applies to stroke styles.
void ms_command_set_stroke_position(MSDocument *document, uint64_t layerId, int index, int position);

#if defined(__APPLE__)

/* ============================ canvas (Apple platforms) ============================ */

// Creates a canvas that renders directly into an MTKView.
// mtkView: an MTKView instance (paused, drawable-driven). Ownership stays
// with the caller. Returns NULL when Metal is unavailable or mtkView is null.
MSCanvas *ms_canvas_create(void *mtkView);
void ms_canvas_destroy(MSCanvas *canvas);

// Evaluates the composition at frame and presents the result into the
// canvas's MTKView drawable.
void ms_canvas_draw_frame(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, int64_t frame);
// Same as ms_canvas_draw_frame, also writing per-stage CPU timings when
// profileOut is non-null. Durations are milliseconds measured from this call
// boundary through evaluate/build/adapter playback.
void ms_canvas_draw_frame_profiled(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, int64_t frame, MSCanvasFrameProfile *profileOut);
// Fractional-frame variant for high-refresh live preview. The project timeline
// and keyframes remain integer-framed; this samples between frames.
void ms_canvas_draw_frame_at_time_profiled(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, double frameTime, MSCanvasFrameProfile *profileOut);

// Preview chrome behind the composition: 0 = solid black (default), 1 =
// transparency checkerboard. Ignored when canvas is null.
void ms_canvas_set_preview_backdrop(MSCanvas *canvas, int backdrop);

// Sets preview-only layer selection chrome for subsequent draw calls.
// layerIds may be null only when count is 0. Supports multiple IDs so callers
// can pass future multi-selection without changing the canvas API.
void ms_canvas_set_selected_layers(MSCanvas *canvas, const uint64_t *layerIds, size_t count);

// Sets the user view transform applied on top of the fit-to-drawable
// transform, effective for every subsequent draw call.
// zoom: magnification relative to fit (1 = fit to drawable).
// panX: horizontal translation in view points.
// panY: vertical translation in view points.
// Ignored when canvas is null.
void ms_canvas_set_view_transform(MSCanvas *canvas, float zoom, float panX, float panY);
#endif

#ifdef __cplusplus
}
#endif

#endif  // MOTIONSTUDIO_BRIDGE_H
