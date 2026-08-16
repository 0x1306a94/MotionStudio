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

#if defined(__APPLE__)

#include <CoreFoundation/CFAvailability.h>

#else

// Match Apple's CF_CLOSED_ENUM fallback when fixed enums are unavailable:
//   typedef CF_CLOSED_ENUM(int, Name) { ... };
// → typedef int Name; enum { ... };
#define CF_CLOSED_ENUM(_type, _name) \
    _type _name;                     \
    enum
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MSDocument MSDocument;
typedef struct MSCanvas MSCanvas;

typedef struct MSCanvasFrameProfile {
    bool drewFrame;
    bool usedFrameCache;
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
typedef CF_CLOSED_ENUM(int, MS_LAYER) {
    MS_LAYER_INVALID = -1,
    MS_LAYER_SHAPE = 0,
    MS_LAYER_IMAGE = 1,
    MS_LAYER_TEXT = 2,
    MS_LAYER_GROUP = 3,
    MS_LAYER_PRECOMP = 4,
};

// Layer style type tag, mirrors motion::LayerStyleType.
typedef CF_CLOSED_ENUM(int, MS_STYLE) {
    MS_STYLE_INVALID = -1,
    MS_STYLE_FILL = 0,
    MS_STYLE_STROKE = 1,
};

// Layer effect type tag, mirrors motion::LayerEffectType.
typedef CF_CLOSED_ENUM(int, MS_EFFECT) {
    MS_EFFECT_INVALID = -1,
    MS_EFFECT_BRIGHTNESS_CONTRAST = 0,
    MS_EFFECT_GAUSSIAN_BLUR = 1,
};

// Layer style (fx) type tag, mirrors motion::LayerFxType. Not MS_STYLE.
typedef CF_CLOSED_ENUM(int, MS_LAYER_FX) {
    MS_LAYER_FX_INVALID = -1,
    MS_LAYER_FX_DROP_SHADOW = 0,
    MS_LAYER_FX_OUTER_GLOW = 1,
    MS_LAYER_FX_STROKE = 2,
};

// Path mask mode tag, mirrors motion::MaskMode.
typedef CF_CLOSED_ENUM(int, MS_MASK) {
    MS_MASK_INVALID = -1,
    MS_MASK_ADD = 0,
    MS_MASK_SUBTRACT = 1,
    MS_MASK_INTERSECT = 2,
};

// Track matte type tag, mirrors motion::TrackMatteType.
typedef CF_CLOSED_ENUM(int, MS_TRACK_MATTE) {
    MS_TRACK_MATTE_NONE = 0,
    MS_TRACK_MATTE_ALPHA = 1,
    MS_TRACK_MATTE_ALPHA_INVERTED = 2,
    MS_TRACK_MATTE_LUMA = 3,
    MS_TRACK_MATTE_LUMA_INVERTED = 4,
};

// Stroke position tag, mirrors motion::StrokePosition.
typedef CF_CLOSED_ENUM(int, MS_STROKE_POSITION) {
    MS_STROKE_POSITION_INVALID = -1,
    MS_STROKE_POSITION_CENTER = 0,
    MS_STROKE_POSITION_INSIDE = 1,
    MS_STROKE_POSITION_OUTSIDE = 2,
};

// How image pixels map into the layer container (aligns with ImageScaleMode / PAGScaleMode).
typedef CF_CLOSED_ENUM(int, MS_IMAGE_SCALE) {
    MS_IMAGE_SCALE_NONE = 0,
    MS_IMAGE_SCALE_STRETCH = 1,
    MS_IMAGE_SCALE_LETTER_BOX = 2,
    MS_IMAGE_SCALE_ZOOM = 3,
};

// Horizontal text alignment, mirrors motion::TextAlign.
typedef CF_CLOSED_ENUM(int, MS_TEXT_ALIGN) {
    MS_TEXT_ALIGN_LEFT = 0,
    MS_TEXT_ALIGN_CENTER = 1,
    MS_TEXT_ALIGN_RIGHT = 2,
};

// Blend mode tag, mirrors motion::BlendMode ordinals.
typedef CF_CLOSED_ENUM(int, MS_BLEND) {
    MS_BLEND_INVALID = -1,
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
typedef CF_CLOSED_ENUM(int, MS_VALUE) {
    MS_VALUE_INVALID = -1,
    MS_VALUE_FLOAT = 0,
    MS_VALUE_VEC2 = 1,
    MS_VALUE_COLOR = 2,
    MS_VALUE_BEZIER_PATH = 3,
    MS_VALUE_STRING = 4,
    MS_VALUE_VEC3 = 5,
    MS_VALUE_VEC4 = 6,
};

// Easing type tag, mirrors motion::EasingType.
typedef CF_CLOSED_ENUM(int, MS_EASING) {
    MS_EASING_INVALID = -1,
    MS_EASING_LINEAR = 0,
    MS_EASING_HOLD = 1,
    MS_EASING_EASE = 2,
    MS_EASING_EASE_IN = 3,
    MS_EASING_EASE_OUT = 4,
    MS_EASING_EASE_IN_OUT = 5,
    MS_EASING_CUBIC_BEZIER = 6,
};

typedef CF_CLOSED_ENUM(int, MS_PREVIEWER_BACKDROP) {
    MS_PREVIEWER_BACKDROP_BLACK = 0,
    MS_PREVIEWER_BACKDROP_TRANSPARENT = 1,
};

// One BezierPath vertex; tangents are offsets from the point.
typedef struct MSBezierVertex {
    float pointX;
    float pointY;
    float inTangentX;
    float inTangentY;
    float outTangentX;
    float outTangentY;
} MSBezierVertex;

// Heap-allocated Bezier path. Release with ms_bezier_path_free.
typedef struct MSBezierPath {
    MSBezierVertex *vertices;
    size_t count;
    bool closed;
} MSBezierPath;

// Frees a path returned by this API (vertices + the struct itself).
void ms_bezier_path_free(MSBezierPath *path);

// One VectorNetwork vertex (authoring graph).
typedef CF_CLOSED_ENUM(int, MS_VERTEX_MIRROR) {
    MS_VERTEX_MIRROR_NONE = 0,
    MS_VERTEX_MIRROR_ANGLE = 1,
    MS_VERTEX_MIRROR_ANGLE_LENGTH = 2,
};

typedef struct MSVectorNetworkVertex {
    uint32_t id;
    float x;
    float y;
    MS_VERTEX_MIRROR mirrorMode;
} MSVectorNetworkVertex;

// One directed cubic edge; tangents are offsets from the endpoints.
typedef struct MSVectorNetworkEdge {
    uint32_t id;
    uint32_t start;
    uint32_t end;
    float startTangentX;
    float startTangentY;
    float endTangentX;
    float endTangentY;
} MSVectorNetworkEdge;

// Heap-allocated VectorNetwork. Release with ms_vector_network_free.
typedef struct MSVectorNetwork {
    MSVectorNetworkVertex *vertices;
    size_t vertexCount;
    MSVectorNetworkEdge *edges;
    size_t edgeCount;
} MSVectorNetwork;

void ms_vector_network_free(MSVectorNetwork *network);

// Pure local-space PathGeometryEdit wrappers. Return a new heap path (free with
// ms_bezier_path_free). NULL path / invalid index yields NULL or a copy.
MSBezierPath *ms_bezier_move_vertex(const MSBezierPath *path, size_t index, float x, float y,
                                    bool linkedHandles);
MSBezierPath *ms_bezier_move_in_tangent(const MSBezierPath *path, size_t index, float x, float y,
                                        bool mirrorOut);
MSBezierPath *ms_bezier_move_out_tangent(const MSBezierPath *path, size_t index, float x, float y,
                                         bool mirrorIn);
MSBezierPath *ms_bezier_insert_vertex_on_segment(const MSBezierPath *path, size_t segmentIndex,
                                                 float t);
MSBezierPath *ms_bezier_remove_vertex(const MSBezierPath *path, size_t index);
MSBezierPath *ms_bezier_close_path(const MSBezierPath *path);
MSBezierPath *ms_bezier_append_vertex(const MSBezierPath *path, float x, float y);
MSBezierPath *ms_bezier_toggle_vertex_smooth(const MSBezierPath *path, size_t index);

// Path-edit target kind, mirrors motion::PathEditKind (+ none to clear).
typedef CF_CLOSED_ENUM(int, MS_PATH_EDIT) {
    MS_PATH_EDIT_NONE = 0,
    MS_PATH_EDIT_SHAPE = 1,
    MS_PATH_EDIT_MASK = 2,
};

/* ============================ lifecycle ============================ */

// Creates a new document containing one default composition
// (1920x1080, 30 fps, 150 frames).
MSDocument *ms_document_create(void);

// Loads a document from JSON text (in-memory). Does not set projectRoot.
// jsonText: JSON payload (need not be null-terminated).
// length: byte length of jsonText.
// errorOut: optional; on failure receives a malloc'd error message.
// Returns NULL on failure.
MSDocument *ms_document_load_json(const char *jsonText, size_t length, char **errorOut);

// Loads a document package directory: reads {packagePath}/document.json and
// sets projectRoot to packagePath.
// packagePath: absolute path to the package directory.
// errorOut: optional; on failure receives a malloc'd error message.
MSDocument *ms_document_load(const char *packagePath, char **errorOut);

// Sets / gets the non-persistent project package root used to resolve Asset.path.
void ms_document_set_project_root(MSDocument *document, const char *absolutePath);
char *ms_document_project_root(MSDocument *document);

void ms_document_destroy(MSDocument *document);

// Content generation counter from the app (Swift revision). Used to invalidate
// PreviewSceneCache and FrameCommandCache when the document model changes.
void ms_document_set_content_revision(MSDocument *document, uint64_t revision);
uint64_t ms_document_get_content_revision(const MSDocument *document);

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
// Topmost layer hit at scene-space point (including locked), or 0 when none hit.
uint64_t ms_composition_hit_test_layer(MSDocument *document, uint64_t compositionId, double frameTime, float x, float y, float tolerance);
bool ms_composition_layer_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                                 float *minX, float *minY, float *maxX, float *maxY);

// Maps a composition-space translation into the layer's parent space using the
// inverse of the parent world linear transform. Identity when the layer has no
// parent. Returns false when the layer is missing or the parent transform is
// not invertible.
bool ms_layer_map_composition_delta(MSDocument *document, uint64_t compositionId, uint64_t layerId,
                                    double frameTime, float dx, float dy, float *outParentDx,
                                    float *outParentDy);

// Layer-local AABB (same source as selection handles localMin/localMax).
// Runs EvaluatePreview → ResolvePointTextContainerSizes → BoundsOfLayerLocal.
// Returns false when the layer is missing or has no local bounds.
bool ms_layer_local_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                           float *minX, float *minY, float *maxX, float *maxY);

// Maps a layer-local point to composition scene space using the evaluated
// layer worldTransform (same source as path-edit ScenePointToLocal).
// Returns false when the layer is missing or has no evaluated world transform.
bool ms_layer_transform_local_point(MSDocument *document, uint64_t layerId, int64_t frame,
                                    float localX, float localY, float *sceneX, float *sceneY);

// Inverse of ms_layer_transform_local_point. Returns false when the layer is
// missing or the world transform is not invertible.
bool ms_layer_transform_scene_point(MSDocument *document, uint64_t layerId, int64_t frame,
                                    float sceneX, float sceneY, float *localX, float *localY);

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
typedef CF_CLOSED_ENUM(int, MS_SELECTION_HANDLE) {
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
MS_SELECTION_HANDLE ms_selection_handles_hit_test(const MSSelectionHandles *handles, float x, float y,
                                                  float handleHitRadius, float rotateInner, float rotateOuter);

// Composition name (malloc'd).
char *ms_composition_name(MSDocument *document, uint64_t compositionId);

/* ============================ layer queries ============================ */

// ID of the layer at index within the composition (0 = bottommost).
// Returns 0 on out-of-range.
uint64_t ms_layer_id_at(MSDocument *document, uint64_t compositionId, int index);

// Layer name (malloc'd).
char *ms_layer_name(MSDocument *document, uint64_t layerId);
// Layer type tag (MS_LAYER_*), MS_LAYER_INVALID when the layer does not exist.
MS_LAYER ms_layer_type(MSDocument *document, uint64_t layerId);
int64_t ms_layer_in_point(MSDocument *document, uint64_t layerId);
int64_t ms_layer_out_point(MSDocument *document, uint64_t layerId);
// Parent layer ID, 0 when the layer has no parent or does not exist.
uint64_t ms_layer_parent_id(MSDocument *document, uint64_t layerId);
bool ms_layer_visible(MSDocument *document, uint64_t layerId);
bool ms_layer_locked(MSDocument *document, uint64_t layerId);
// Layer-level blend mode (MS_BLEND_*); MS_BLEND_INVALID when the layer is missing.
MS_BLEND ms_layer_blend_mode(MSDocument *document, uint64_t layerId);

/* ============================ layer style queries ============================ */

// Number of styles (fills/strokes) on the layer; 0 when the layer does not exist.
int ms_layer_style_count(MSDocument *document, uint64_t layerId);
// Style type tag (MS_STYLE_*) at index, MS_STYLE_INVALID when out of range.
MS_STYLE ms_layer_style_type_at(MSDocument *document, uint64_t layerId, int index);
// Blend mode tag (MS_BLEND_*) of the style at index; MS_BLEND_INVALID when
// out of range.
MS_BLEND ms_layer_style_blend_mode_at(MSDocument *document, uint64_t layerId, int index);

// Number of post-process effects on the layer; 0 when the layer does not exist.
int ms_layer_effect_count(MSDocument *document, uint64_t layerId);
// Effect type tag (MS_EFFECT_*) at index, MS_EFFECT_INVALID when out of range.
MS_EFFECT ms_layer_effect_type_at(MSDocument *document, uint64_t layerId, int index);
// Whether the effect at index is enabled; false when out of range.
bool ms_layer_effect_enabled_at(MSDocument *document, uint64_t layerId, int index);
// Gaussian Blur repeatEdgePixels at index; false when out of range or not a blur.
bool ms_layer_effect_repeat_edge_at(MSDocument *document, uint64_t layerId, int index);

// Number of layer styles (drop shadow / glow / stroke); 0 when the layer does not exist.
int ms_layer_fx_count(MSDocument *document, uint64_t layerId);
// Style type tag (MS_LAYER_FX_*) at index, MS_LAYER_FX_INVALID when out of range.
MS_LAYER_FX ms_layer_fx_type_at(MSDocument *document, uint64_t layerId, int index);
// Whether the style at index is enabled; false when out of range.
bool ms_layer_fx_enabled_at(MSDocument *document, uint64_t layerId, int index);
// Blend mode of the style at index; MS_BLEND_NORMAL when out of range.
MS_BLEND ms_layer_fx_blend_mode_at(MSDocument *document, uint64_t layerId, int index);
// Stroke position at index; MS_STROKE_POSITION_INVALID when out of range or not a stroke.
MS_STROKE_POSITION ms_layer_fx_stroke_position_at(MSDocument *document, uint64_t layerId, int index);

// Path masks on a layer.
int ms_layer_mask_count(MSDocument *document, uint64_t layerId);
// Mask mode tag (MS_MASK_*) at index, MS_MASK_INVALID when out of range.
MS_MASK ms_layer_mask_mode_at(MSDocument *document, uint64_t layerId, int index);
bool ms_layer_mask_inverted_at(MSDocument *document, uint64_t layerId, int index);

// Track matte on a layer. type is MS_TRACK_MATTE_*; source id is 0 when none.
MS_TRACK_MATTE ms_layer_track_matte_type(MSDocument *document, uint64_t layerId);
uint64_t ms_layer_track_matte_layer_id(MSDocument *document, uint64_t layerId);

// Follow Path constraint on a layer. pathLayerId is 0 when unbound.
bool ms_layer_follow_path_enabled(MSDocument *document, uint64_t layerId);
uint64_t ms_layer_follow_path_layer_id(MSDocument *document, uint64_t layerId);
bool ms_layer_follow_path_orient(MSDocument *document, uint64_t layerId);

// Stroke position of the style at index; MS_STROKE_POSITION_INVALID when out of
// range or the style is not a stroke.
MS_STROKE_POSITION ms_layer_style_stroke_position_at(MSDocument *document, uint64_t layerId, int index);

/* ============================ property queries ============================ */
// entityId: ID of the owning Layer or ShapeElement.
// path: dot-separated property path. Layer examples: "transform.position", "size", "styles[0].color".
// ShapeElement examples: "path", "size", "cornerRadius".

// Value type tag (MS_VALUE_*), MS_VALUE_INVALID when the property does not exist.
MS_VALUE ms_property_type(MSDocument *document, uint64_t entityId, const char *path);
bool ms_property_is_animated(MSDocument *document, uint64_t entityId, const char *path);

// Static value accessors. Return zero/fill nothing when the property does not
// exist or the type does not match.
float ms_property_static_float(MSDocument *document, uint64_t entityId, const char *path);
void ms_property_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float *x, float *y);
void ms_property_static_vec3(MSDocument *document, uint64_t entityId, const char *path, float *x, float *y,
                             float *z);
void ms_property_static_color(MSDocument *document, uint64_t entityId, const char *path, float *r, float *g, float *b, float *a);
// Returns a heap-allocated path copy. Release with ms_bezier_path_free.
// Returns NULL when the property is missing or not a BezierPath.
MSBezierPath *ms_property_static_bezier_path(MSDocument *document, uint64_t entityId, const char *path);
// Authoring VectorNetwork. Release with ms_vector_network_free. NULL on miss.
MSVectorNetwork *ms_property_static_vector_network(MSDocument *document, uint64_t entityId,
                                                   const char *path);
// Returns a malloc'd UTF-8 string. Release with ms_string_free.
// Returns NULL when the property is missing or not a String.
char *ms_property_static_string(MSDocument *document, uint64_t entityId, const char *path);

// Keyframe accessors (index into the ascending-time keyframe list).
int ms_property_keyframe_count(MSDocument *document, uint64_t entityId, const char *path);
int64_t ms_property_keyframe_time_at(MSDocument *document, uint64_t entityId, const char *path, int index);
float ms_property_keyframe_float_at(MSDocument *document, uint64_t entityId, const char *path, int index);
void ms_property_keyframe_vec2_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *x, float *y);

// Easing of the keyframe at index. Returns the easing type tag (MS_EASING_*);
// bezier control points are written to the out parameters for bezier-backed
// easings. Returns MS_EASING_INVALID when the keyframe does not exist.
MS_EASING ms_property_keyframe_easing_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *inX, float *inY, float *outX, float *outY);

// Spatial tangents of a Vec2 keyframe (motion path handles). Returns false when
// the property is missing, not Vec2, or index is out of range. hasIn/hasOut and
// coordinate outs may be NULL.
bool ms_property_keyframe_spatial_at(MSDocument *document, uint64_t entityId, const char *path,
                                     int index, bool *hasIn, float *inX, float *inY, bool *hasOut,
                                     float *outX, float *outY);

// Builds the spatial motion path for a Vec2 animatable (typically
// transform.position). Release with ms_bezier_path_free. NULL when missing /
// not Vec2 / fewer than two keyframes.
MSBezierPath *ms_property_build_motion_path(MSDocument *document, uint64_t entityId,
                                            const char *path);

// Value of the property evaluated at the given frame.
float ms_property_evaluate_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame);
void ms_property_evaluate_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *x, float *y);
void ms_property_evaluate_vec3(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *x,
                               float *y, float *z);
void ms_property_evaluate_vec4(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *x,
                               float *y, float *z, float *w);
void ms_property_evaluate_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *r, float *g, float *b, float *a);
// Evaluated BezierPath at frame. Release with ms_bezier_path_free. NULL on miss.
MSBezierPath *ms_property_evaluate_bezier_path(MSDocument *document, uint64_t entityId, const char *path, int64_t frame);
// Evaluated VectorNetwork at frame. Release with ms_vector_network_free.
MSVectorNetwork *ms_property_evaluate_vector_network(MSDocument *document, uint64_t entityId,
                                                     const char *path, int64_t frame);

/* ============================ commands ============================ */
// Every function below executes a command through the document's undo
// manager, so each call is undoable.

void ms_command_set_static_float(MSDocument *document, uint64_t entityId, const char *path, float value);
void ms_command_set_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float x, float y);
void ms_command_set_static_vec3(MSDocument *document, uint64_t entityId, const char *path, float x, float y, float z);
void ms_command_set_static_vec4(MSDocument *document, uint64_t entityId, const char *path, float x, float y, float z,
                                float w);
void ms_command_set_static_color(MSDocument *document, uint64_t entityId, const char *path, float r, float g, float b, float a);
// value may be NULL (treated as empty open path).
void ms_command_set_static_bezier_path(MSDocument *document, uint64_t entityId, const char *path, const MSBezierPath *value);
void ms_command_set_static_vector_network(MSDocument *document, uint64_t entityId, const char *path,
                                          const MSVectorNetwork *value);
// value may be NULL (treated as empty string).
void ms_command_set_static_string(MSDocument *document, uint64_t entityId, const char *path, const char *value);
void ms_command_set_composition_background_color(MSDocument *document, uint64_t compositionId, float r, float g, float b, float a);
void ms_command_set_composition_corner_radius(MSDocument *document, uint64_t compositionId, float cornerRadius);
void ms_command_set_composition_size(MSDocument *document, uint64_t compositionId, int width, int height);
void ms_command_set_composition_duration(MSDocument *document, uint64_t compositionId, int64_t duration);
void ms_command_set_composition_frame_rate(MSDocument *document, uint64_t compositionId, int frameRateNum, int frameRateDen);

// Adds a keyframe holding the property's given value at frame.
void ms_command_add_keyframe_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float value);
void ms_command_add_keyframe_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x, float y);
void ms_command_add_keyframe_vec3(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x,
                                  float y, float z);
void ms_command_add_keyframe_vec4(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x,
                                  float y, float z, float w);
void ms_command_add_keyframe_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float r, float g, float b, float a);
void ms_command_add_keyframe_bezier_path(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, const MSBezierPath *value);
void ms_command_add_keyframe_vector_network(MSDocument *document, uint64_t entityId, const char *path,
                                            int64_t frame, const MSVectorNetwork *value);
// Writes value at playhead: static SetStaticValue when not animated, otherwise
// AddKeyframe upsert at frame.
void ms_command_write_bezier_path_at_playhead(MSDocument *document, uint64_t entityId,
                                              const char *path, int64_t frame,
                                              const MSBezierPath *value);
void ms_command_write_vector_network_at_playhead(MSDocument *document, uint64_t entityId,
                                                 const char *path, int64_t frame,
                                                 const MSVectorNetwork *value);
// Scene-space path edits on a Shape or Mask target. Writes via playhead policy.
void ms_command_path_edit_move_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                      int maskIndex, int64_t frame, size_t index, float sceneX,
                                      float sceneY, bool linkedHandles);
void ms_command_path_edit_move_in_tangent(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                          int maskIndex, int64_t frame, size_t index, float sceneX,
                                          float sceneY, bool mirrorOut);
void ms_command_path_edit_move_out_tangent(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                           int maskIndex, int64_t frame, size_t index, float sceneX,
                                           float sceneY, bool mirrorIn);
void ms_command_path_edit_insert_on_segment(MSDocument *document, uint64_t layerId,
                                            MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                            size_t segmentIndex, float t);
void ms_command_path_edit_remove_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                        int maskIndex, int64_t frame, size_t index);
void ms_command_path_edit_close(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                int maskIndex, int64_t frame);
void ms_command_path_edit_append_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                        int maskIndex, int64_t frame, float sceneX, float sceneY);
// Sets vertex mirroring mode (Figma-style). Degree ≠ 2 stores mode only.
void ms_command_path_edit_set_mirror_mode(MSDocument *document, uint64_t layerId,
                                          MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                          uint32_t vertexId, MS_VERTEX_MIRROR mode);
// Shape path only: rebase path so bounds center is local origin and bump
// transform.position so the world silhouette stays put. No-op for masks /
// already-centered paths.
void ms_command_path_edit_recenter_shape(MSDocument *document, uint64_t layerId, int64_t frame);

// VectorNetwork topology edits (scene-space where a point is required).
// outVertexId / outEdgeId may be NULL; written 0 on failure / no-op.
void ms_command_network_edit_add_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                        int maskIndex, int64_t frame, float sceneX, float sceneY,
                                        uint32_t *outVertexId);
void ms_command_network_edit_add_edge(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                      int maskIndex, int64_t frame, uint32_t startId, uint32_t endId,
                                      uint32_t *outEdgeId);
void ms_command_network_edit_move_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                         int maskIndex, int64_t frame, uint32_t vertexId,
                                         float sceneX, float sceneY);
void ms_command_network_edit_move_edge_tangent(MSDocument *document, uint64_t layerId,
                                               MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                               uint32_t edgeId, bool atStart, float sceneX,
                                               float sceneY, bool mirror);
void ms_command_network_edit_insert_on_edge(MSDocument *document, uint64_t layerId,
                                            MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                            uint32_t edgeId, float t, uint32_t *outVertexId);
void ms_command_network_edit_remove_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                           int maskIndex, int64_t frame, uint32_t vertexId);
void ms_command_network_edit_recenter_shape(MSDocument *document, uint64_t layerId, int64_t frame);
void ms_command_remove_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t frame);
void ms_command_move_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t oldFrame, int64_t newFrame);
// easingType: MS_EASING_* tag. Control points are only used for MS_EASING_CUBIC_BEZIER.
void ms_command_set_easing(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, int easingType, float inX, float inY, float outX, float outY);

// Sets spatial in/out tangents on a Vec2 keyframe at frame. hasIn/hasOut false
// clears that handle. No-op when property is not Vec2 or keyframe missing.
void ms_command_set_spatial_tangents(MSDocument *document, uint64_t entityId, const char *path,
                                     int64_t frame, bool hasIn, float inX, float inY, bool hasOut,
                                     float outX, float outY);

// Adds a rectangle/ellipse shape layer (200x200, centered on the composition,
// default fill, spanning the full composition duration).
// Returns the new layer ID, 0 on failure.
uint64_t ms_command_add_rect_layer(MSDocument *document, uint64_t compositionId);
uint64_t ms_command_add_ellipse_layer(MSDocument *document, uint64_t compositionId);
// Adds an empty open ShapePath layer with a default stroke (Figma-style, no
// fill), centered on the composition. Returns the new layer ID, 0 on failure.
uint64_t ms_command_add_path_layer(MSDocument *document, uint64_t compositionId);

// Copies sourceAbsolutePath into {projectRoot}/assets/ (requires projectRoot),
// registers a document Asset with the given intrinsic pixel size, and returns
// the new asset id (0 on failure). Undo removes the Asset entry only.
uint64_t ms_command_import_image_asset(MSDocument *document, const char *sourceAbsolutePath,
                                       const char *preferredFileName, int width, int height);
// Adds an unbound placeholder Image layer (200x200, centered). Returns layer id.
uint64_t ms_command_add_image_layer(MSDocument *document, uint64_t compositionId);
// Binds image layer to assetId; assetId 0 unbinds. Does not change container size.
bool ms_layer_set_image_asset(MSDocument *document, uint64_t layerId, uint64_t assetId);
uint64_t ms_layer_image_asset_id(MSDocument *document, uint64_t layerId);
void ms_layer_set_image_scale_mode(MSDocument *document, uint64_t layerId, MS_IMAGE_SCALE mode);
MS_IMAGE_SCALE ms_layer_image_scale_mode(MSDocument *document, uint64_t layerId);
int ms_document_asset_count(MSDocument *document);
uint64_t ms_document_asset_id_at(MSDocument *document, int index);
char *ms_asset_name(MSDocument *document, uint64_t assetId);
char *ms_asset_path(MSDocument *document, uint64_t assetId);
int ms_asset_width(MSDocument *document, uint64_t assetId);
int ms_asset_height(MSDocument *document, uint64_t assetId);
int ms_asset_type(MSDocument *document, uint64_t assetId);  // 0 = image

// ---- Document shader library (process color / Color Source) ----

// Paint kind for Fill/Stroke; mirrors motion::StylePaintMode.
typedef CF_CLOSED_ENUM(int, MS_PAINT_MODE) {
    MS_PAINT_MODE_INVALID = -1,
    MS_PAINT_MODE_COLOR = 0,
    MS_PAINT_MODE_SHADER = 1,
    MS_PAINT_MODE_GRADIENT = 2,
};

// Gradient type; mirrors motion::GradientType.
typedef CF_CLOSED_ENUM(int, MS_GRADIENT_TYPE) {
    MS_GRADIENT_TYPE_INVALID = -1,
    MS_GRADIENT_TYPE_LINEAR = 0,
    MS_GRADIENT_TYPE_RADIAL = 1,
    MS_GRADIENT_TYPE_CONIC = 2,
    MS_GRADIENT_TYPE_DIAMOND = 3,
};

// User-editable scheme formats (v1 UI subset). Map to motion::UniformFormat via
// switch — do not static_cast (Color is appended at the end of the C++ enum).
typedef CF_CLOSED_ENUM(int, MS_UNIFORM_FORMAT) {
    MS_UNIFORM_FORMAT_INVALID = -1,
    MS_UNIFORM_FORMAT_FLOAT = 0,
    MS_UNIFORM_FORMAT_FLOAT2 = 1,
    MS_UNIFORM_FORMAT_FLOAT3 = 2,
    MS_UNIFORM_FORMAT_FLOAT4 = 3,
    MS_UNIFORM_FORMAT_COLOR = 4,
};

int ms_document_shader_count(MSDocument *document);
uint64_t ms_document_shader_id_at(MSDocument *document, int index);
char *ms_document_shader_name(MSDocument *document, uint64_t shaderId);        // ms_string_free
char *ms_document_shader_main_image(MSDocument *document, uint64_t shaderId);  // ms_string_free
int ms_document_shader_uniform_count(MSDocument *document, uint64_t shaderId);
char *ms_document_shader_uniform_name_at(MSDocument *document, uint64_t shaderId, int index);
MS_UNIFORM_FORMAT ms_document_shader_uniform_format_at(MSDocument *document, uint64_t shaderId,
                                                       int index);
bool ms_document_shader_uniform_animatable_at(MSDocument *document, uint64_t shaderId, int index);
float ms_document_shader_uniform_default_float_at(MSDocument *document, uint64_t shaderId, int index);
void ms_document_shader_uniform_default_vec2_at(MSDocument *document, uint64_t shaderId, int index,
                                                float *x, float *y);
void ms_document_shader_uniform_default_vec3_at(MSDocument *document, uint64_t shaderId, int index,
                                                float *x, float *y, float *z);
void ms_document_shader_uniform_default_vec4_at(MSDocument *document, uint64_t shaderId, int index,
                                                float *x, float *y, float *z, float *w);
void ms_document_shader_uniform_default_color_at(MSDocument *document, uint64_t shaderId, int index,
                                                 float *r, float *g, float *b, float *a);

// Adds a shader with default mainImage template and empty uniforms. name may be NULL → "Shader".
uint64_t ms_document_add_shader(MSDocument *document, const char *name);
// Replaces name/mainImage/uniforms in one undo step. uniformsJson is a JSON array of
// {name,format,count,animatable?,default?} (same shape as shader.json uniforms); NULL or "[]" clears.
bool ms_document_update_shader(MSDocument *document, uint64_t shaderId, const char *name,
                               const char *mainImage, const char *uniformsJson);
bool ms_document_remove_shader(MSDocument *document, uint64_t shaderId);  // false if referenced
bool ms_document_shader_is_referenced(MSDocument *document, uint64_t shaderId);
bool ms_document_rename_shader(MSDocument *document, uint64_t shaderId, const char *name);

char *ms_document_serialize_shaders(MSDocument *document);  // ms_string_free
// Package open: deserialize document.json + shader.json into one Document (shaders assigned
// before the handle is returned, then ValidateShaderReferences). shadersJson may be NULL
// or shadersLength 0 for an empty library. On failure returns NULL and sets *errorOut.
MSDocument *ms_document_load_json_with_shaders(const char *documentJson, size_t documentLength,
                                               const char *shadersJson, size_t shadersLength,
                                               char **errorOut);

MS_PAINT_MODE ms_layer_style_paint_mode_at(MSDocument *document, uint64_t layerId, int index);
uint64_t ms_layer_style_shader_id_at(MSDocument *document, uint64_t layerId, int index);
// COLOR/GRADIENT ignore shaderId. SHADER uses shaderId when the style has none yet
// (or when rebinding to a different shader); pass 0 to keep/reuse the current binding.
bool ms_document_set_style_paint_mode(MSDocument *document, uint64_t layerId, int index,
                                      MS_PAINT_MODE mode, uint64_t shaderId);

MS_GRADIENT_TYPE ms_layer_style_gradient_type_at(MSDocument *document, uint64_t layerId, int index);
int ms_layer_style_gradient_stop_count(MSDocument *document, uint64_t layerId, int index);
bool ms_document_set_gradient_type(MSDocument *document, uint64_t layerId, int index,
                                   MS_GRADIENT_TYPE type);
bool ms_document_add_gradient_stop(MSDocument *document, uint64_t layerId, int index,
                                   int insertIndex, float r, float g, float b, float a,
                                   float position);
bool ms_document_remove_gradient_stop(MSDocument *document, uint64_t layerId, int index,
                                      int stopIndex);

// Adds a Text layer (400x120, boxTextMode off, PingFang SC, black fill, centered).
uint64_t ms_command_add_text_layer(MSDocument *document, uint64_t compositionId);
// family / style: system CT family and style names (style may be empty for default).
bool ms_command_set_text_font(MSDocument *document, uint64_t layerId, const char *family,
                              const char *style);
// frame: playhead used to evaluate text when enabling box mode (measure → size).
bool ms_command_set_text_box_text_mode(MSDocument *document, uint64_t layerId, bool boxTextMode,
                                       int64_t frame);
bool ms_command_set_text_font_size(MSDocument *document, uint64_t layerId, float fontSize);
bool ms_command_set_text_size(MSDocument *document, uint64_t layerId, float width, float height);
bool ms_command_set_text_align(MSDocument *document, uint64_t layerId, MS_TEXT_ALIGN align);
bool ms_layer_text_box_text_mode(MSDocument *document, uint64_t layerId);
float ms_layer_text_font_size(MSDocument *document, uint64_t layerId);
bool ms_layer_text_size(MSDocument *document, uint64_t layerId, float *width, float *height);
MS_TEXT_ALIGN ms_layer_text_align(MSDocument *document, uint64_t layerId);
char *ms_layer_text_font_family(MSDocument *document, uint64_t layerId);  // ms_string_free
char *ms_layer_text_font_style(MSDocument *document, uint64_t layerId);   // ms_string_free

// Text Path on a text layer. pathLayerId is 0 when unbound. Margins use
// ms_property_* with "content.textPath.firstMargin" / "lastMargin".
void ms_command_set_text_path(MSDocument *document, uint64_t layerId, bool enabled,
                              uint64_t pathLayerId, bool reversed, bool perpendicular,
                              bool forceAlignment);
bool ms_layer_text_path_enabled(MSDocument *document, uint64_t layerId);
uint64_t ms_layer_text_path_layer_id(MSDocument *document, uint64_t layerId);
bool ms_layer_text_path_reversed(MSDocument *document, uint64_t layerId);
bool ms_layer_text_path_perpendicular(MSDocument *document, uint64_t layerId);
bool ms_layer_text_path_force_alignment(MSDocument *document, uint64_t layerId);

// Bakes Rect/Ellipse geometry on the layer into a ShapePath at frame.
void ms_command_convert_geometry_to_path(MSDocument *document, uint64_t layerId, int64_t frame);

// Scales shape geometry + all mask paths in layer-local space about localPivot by
// (scaleX, scaleY) relative to the *current* geometry (one-shot). Does not open a
// merge group — wrap with begin/end merge for a single undo unit, or call inside
// an existing drag merge. Does not modify transform.scale / anchor / position.
// Returns false if the layer is missing.
bool ms_command_resize_layer_geometry(MSDocument *document, uint64_t layerId, double frameTime,
                                      float localPivotX, float localPivotY, float scaleX,
                                      float scaleY);
void ms_command_remove_layer(MSDocument *document, uint64_t compositionId, uint64_t layerId);
void ms_command_move_layer(MSDocument *document, uint64_t compositionId, int fromIndex, int toIndex);
void ms_command_set_layer_visible(MSDocument *document, uint64_t layerId, bool visible);
void ms_command_set_layer_locked(MSDocument *document, uint64_t layerId, bool locked);
// name: UTF-8 layer display name. Null is treated as empty.
void ms_command_set_layer_name(MSDocument *document, uint64_t layerId, const char *name);
// blendMode: MS_BLEND_* tag. Sets Layer::blendMode (not fill/stroke style).
void ms_command_set_layer_blend_mode(MSDocument *document, uint64_t layerId, MS_BLEND blendMode);

// Appends a default fill (black, normal blend) to the layer's style list.
void ms_command_add_fill_style(MSDocument *document, uint64_t layerId);
// Appends a default stroke (black, width 2, normal blend, center) to the
// layer's style list.
void ms_command_add_stroke_style(MSDocument *document, uint64_t layerId);
// Removes the style at index from the layer's style list.
void ms_command_remove_style(MSDocument *document, uint64_t layerId, int index);
// Moves a layer style within the same Fill or Stroke block. Cross-type is a no-op.
void ms_command_move_layer_style(MSDocument *document, uint64_t layerId, int fromIndex,
                                 int toIndex);
// blendMode: MS_BLEND_* tag. Applies to fill and stroke styles.
void ms_command_set_style_blend_mode(MSDocument *document, uint64_t layerId, int index, MS_BLEND blendMode);
// position: MS_STROKE_POSITION_* tag. Only applies to stroke styles.
void ms_command_set_stroke_position(MSDocument *document, uint64_t layerId, int index, MS_STROKE_POSITION position);

// Appends a default BrightnessContrast effect (identity parameters, enabled).
void ms_command_add_brightness_contrast_effect(MSDocument *document, uint64_t layerId);
// Appends a default GaussianBlur effect (blurriness 0, repeatEdgePixels false).
void ms_command_add_gaussian_blur_effect(MSDocument *document, uint64_t layerId);
// Removes the effect at index from the layer's effect list.
void ms_command_remove_layer_effect(MSDocument *document, uint64_t layerId, int index);
// Moves an effect to another index. Out of range is a no-op.
void ms_command_move_layer_effect(MSDocument *document, uint64_t layerId, int fromIndex, int toIndex);
void ms_command_set_layer_effect_enabled(MSDocument *document, uint64_t layerId, int index, bool enabled);
// Only applies to GaussianBlur; other types are a no-op.
void ms_command_set_gaussian_blur_repeat_edge(MSDocument *document, uint64_t layerId, int index,
                                              bool repeatEdgePixels);

void ms_command_add_layer_fx_drop_shadow(MSDocument *document, uint64_t layerId);
void ms_command_add_layer_fx_outer_glow(MSDocument *document, uint64_t layerId);
void ms_command_add_layer_fx_stroke(MSDocument *document, uint64_t layerId);
void ms_command_remove_layer_fx(MSDocument *document, uint64_t layerId, int index);
void ms_command_move_layer_fx(MSDocument *document, uint64_t layerId, int fromIndex, int toIndex);
void ms_command_set_layer_fx_enabled(MSDocument *document, uint64_t layerId, int index, bool enabled);
void ms_command_set_layer_fx_blend_mode(MSDocument *document, uint64_t layerId, int index, MS_BLEND blendMode);
void ms_command_set_layer_fx_stroke_position(MSDocument *document, uint64_t layerId, int index,
                                             MS_STROKE_POSITION position);

// Appends a path mask (Add mode) baked from the layer's shape at `frame`.
// Non-shape layers fall back to a 200x200 centered rectangle.
void ms_command_add_mask(MSDocument *document, uint64_t layerId, int64_t frame);
void ms_command_remove_mask(MSDocument *document, uint64_t layerId, int index);
void ms_command_move_mask(MSDocument *document, uint64_t layerId, int fromIndex, int toIndex);
// mode: MS_MASK_* tag.
void ms_command_set_mask_mode(MSDocument *document, uint64_t layerId, int index, MS_MASK mode);
void ms_command_set_mask_inverted(MSDocument *document, uint64_t layerId, int index, bool inverted);
// type: MS_TRACK_MATTE_*. matteLayerId may be 0 when type is NONE.
void ms_command_set_track_matte(MSDocument *document, uint64_t layerId, uint64_t matteLayerId, MS_TRACK_MATTE type);
// Sets Follow Path binding. pathLayerId may be 0 when disabling.
void ms_command_set_follow_path(MSDocument *document, uint64_t layerId, bool enabled,
                                uint64_t pathLayerId, bool orientAlongPath);

#if defined(__APPLE__)

/* ============================ canvas (Apple platforms) ============================ */

// Creates a canvas that renders directly into an MTKView.
// mtkView: an MTKView instance (paused, drawable-driven). Ownership stays
// with the caller. Returns NULL when Metal is unavailable or mtkView is null.
MSCanvas *ms_canvas_create(void *mtkView);

/* ============================ video export (Apple platforms) ============================ */

typedef struct MSVideoExportOptions {
    const char *outputPath;
    int64_t startFrame;  // <0 → 0
    int64_t endFrame;    // <0 → composition.duration
    int width;           // 0 → composition
    int height;
    int frameRateNum;  // 0 → composition
    int frameRateDen;
    int bitrateBps;
    int keyframeInterval;
    int profile;  // 0 Baseline / 1 Main / 2 High
} MSVideoExportOptions;

// Exports composition to H.264 MP4. progress may be NULL. Returns false to cancel.
// cancelFlag non-null and non-zero also cancels. On failure *errorOut is malloc'd
// (ms_string_free); may be NULL when the caller does not need the message.
bool ms_video_export(MSDocument *document, uint64_t compositionId,
                     const MSVideoExportOptions *options,
                     bool (*progress)(void *ctx, int64_t completed, int64_t total), void *progressCtx,
                     const volatile int *cancelFlag, char **errorOut);

/* ============================ pag export (Apple platforms) ============================ */

typedef CF_CLOSED_ENUM(int, MS_PAG_BMP_SEQUENCE_TYPE) {
    MS_PAG_BMP_SEQUENCE_AUTO = 0,
    MS_PAG_BMP_SEQUENCE_VIDEO = 1,
    MS_PAG_BMP_SEQUENCE_BITMAP = 2,
};

typedef struct MSPagExportOptions {
    const char *outputPath;
    bool allowBitmapExport;
    float bitmapScale;  // <=0 → 1.0
    MS_PAG_BMP_SEQUENCE_TYPE bmpSequenceType;
} MSPagExportOptions;

// Exports composition to a binary .pag file via PagExporter.
// When allowBitmapExport and the export tree uses a _bmp name, injects
// TgfxBitmapFrameSource automatically. cancelFlag non-null and non-zero aborts between
// frames (error message "cancelled"). On failure *errorOut is malloc'd (ms_string_free).
bool ms_pag_export(MSDocument *document, uint64_t compositionId, const MSPagExportOptions *options,
                   const volatile int *cancelFlag, char **errorOut);
#endif

void ms_canvas_destroy(MSCanvas *canvas);

// Preview draw mode: EDIT builds selection/path chrome; PLAYBACK skips it.
typedef CF_CLOSED_ENUM(int, MS_CANVAS_DRAW_MODE) {
    MS_CANVAS_DRAW_MODE_EDIT = 0,
    MS_CANVAS_DRAW_MODE_PLAYBACK = 1,
};

void ms_canvas_set_draw_mode(MSCanvas *canvas, MS_CANVAS_DRAW_MODE mode);
MS_CANVAS_DRAW_MODE ms_canvas_get_draw_mode(const MSCanvas *canvas);

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
void ms_canvas_set_preview_backdrop(MSCanvas *canvas, MS_PREVIEWER_BACKDROP backdrop);

// Sets preview-only layer selection chrome for subsequent draw calls.
// layerIds may be null only when count is 0. Supports multiple IDs so callers
// can pass future multi-selection without changing the canvas API.
void ms_canvas_set_selected_layers(MSCanvas *canvas, const uint64_t *layerIds, size_t count);

// When showAnchor is false, subsequent draws omit the selection anchor crosshair
// (used for image container-resize mode). Default is true. Ignored when canvas is null.
void ms_canvas_set_selection_show_anchor(MSCanvas *canvas, bool showAnchor);
void ms_canvas_set_selection_show_scale_handles(MSCanvas *canvas, bool showScaleHandles);

// Sets the user view transform applied on top of the fit-to-drawable
// transform, effective for every subsequent draw call.
// zoom: magnification relative to fit (1 = fit to drawable).
// panX: horizontal translation in view points.
// panY: vertical translation in view points.
// Ignored when canvas is null.
void ms_canvas_set_view_transform(MSCanvas *canvas, float zoom, float panX, float panY);

// Path-edit hit kind, mirrors motion::PathHandleKind.
typedef CF_CLOSED_ENUM(int, MS_PATH_HANDLE) {
    MS_PATH_HANDLE_NONE = 0,
    MS_PATH_HANDLE_VERTEX = 1,
    MS_PATH_HANDLE_IN_TANGENT = 2,
    MS_PATH_HANDLE_OUT_TANGENT = 3,
    MS_PATH_HANDLE_SEGMENT = 4,
    MS_PATH_HANDLE_CLOSE_RING = 5,
    MS_PATH_HANDLE_EDGE_TANGENT = 6,
};

typedef struct MSPathEditHit {
    MS_PATH_HANDLE kind;
    size_t index;
    float segmentT;
    uint32_t vertexId;
    uint32_t edgeId;
    bool atStart;
} MSPathEditHit;

// One preview path overlay. transform is the affine 2x3 of Mat3
// (m00,m01,m02 / m10,m11,m12); path vertices are local to that transform.
typedef struct MSPathOverlayItem {
    const MSBezierPath *path;
    float m00;
    float m01;
    float m02;
    float m10;
    float m11;
    float m12;
    float r;
    float g;
    float b;
    float a;
} MSPathOverlayItem;

// Sets the active path-edit target for chrome + hit testing. Pass
// MS_PATH_EDIT_NONE (or layerId 0) to clear. selectedVertex < 0 hides tangents.
void ms_canvas_set_path_edit_target(MSCanvas *canvas, MS_PATH_EDIT kind, uint64_t layerId,
                                    int maskIndex, int selectedVertex);

// Extra path overlays merged after mask overlays (e.g. open draft stroke).
// items may be NULL when count is 0. Paths are copied; callers may free inputs.
void ms_canvas_set_path_overlays(MSCanvas *canvas, const MSPathOverlayItem *items, size_t count);

// Hits path-edit chrome at scene-space (sceneX, sceneY). Requires a current
// path-edit target. Returns kind NONE when nothing hits.
MSPathEditHit ms_canvas_hit_path_edit(MSCanvas *canvas, MSDocument *document,
                                      uint64_t compositionId, double frameTime, float sceneX,
                                      float sceneY);

// Motion-path chrome (Select tool). layerId 0 clears the selected keyframe.
// selectedKeyframe < 0 hides tangent handles but still draws the path for
// selected layers with ≥2 position keyframes.
void ms_canvas_set_motion_path_selection(MSCanvas *canvas, uint64_t layerId,
                                         int selectedKeyframe);

typedef CF_CLOSED_ENUM(int, MS_MOTION_PATH_HANDLE) {
    MS_MOTION_PATH_HANDLE_NONE = 0,
    MS_MOTION_PATH_HANDLE_KEYFRAME = 1,
    MS_MOTION_PATH_HANDLE_IN_TANGENT = 2,
    MS_MOTION_PATH_HANDLE_OUT_TANGENT = 3,
};

typedef struct MSMotionPathHit {
    MS_MOTION_PATH_HANDLE kind;
    uint64_t layerId;
    size_t index;
} MSMotionPathHit;

// Hits motion-path chrome for currently selected layers. Skips when a path-edit
// target is active. Returns NONE when nothing hits.
MSMotionPathHit ms_canvas_hit_motion_path(MSCanvas *canvas, MSDocument *document,
                                          uint64_t compositionId, double frameTime, float sceneX,
                                          float sceneY);

// Gradient edit chrome (Select tool). styleIndex < 0 or layerId 0 clears.
void ms_canvas_set_gradient_edit_target(MSCanvas *canvas, uint64_t layerId, int styleIndex);

typedef CF_CLOSED_ENUM(int, MS_GRADIENT_HANDLE) {
    MS_GRADIENT_HANDLE_NONE = 0,
    MS_GRADIENT_HANDLE_START = 1,
    MS_GRADIENT_HANDLE_END = 2,
    MS_GRADIENT_HANDLE_START_ANGLE = 3,
    MS_GRADIENT_HANDLE_END_ANGLE = 4,
};

// Hits gradient chrome for the current gradient-edit target. Skips when a
// path-edit target is active. Returns NONE when nothing hits.
MS_GRADIENT_HANDLE ms_canvas_hit_gradient_edit(MSCanvas *canvas, MSDocument *document,
                                               uint64_t compositionId, double frameTime,
                                               float sceneX, float sceneY);

// Writes spatial tangents for a motion-path handle drag (parent-space via scene
// point). Also seeds the adjacent keyframe's opposite handle when missing.
void ms_command_motion_path_drag_tangent(MSDocument *document, uint64_t layerId,
                                         int keyframeIndex, bool isOut, float sceneX,
                                         float sceneY, double frameTime);

#ifdef __cplusplus
}
#endif

#endif  // MOTIONSTUDIO_BRIDGE_H
