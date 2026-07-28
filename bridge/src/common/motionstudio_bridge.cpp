#include "motionstudio_bridge.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/HitTest.h"
#include "MotionStudio/render/MaskPathBake.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/render/SelectionHandles.h"
#include "MotionStudio/serialization/Serializer.h"
#include "MotionStudio/undo/AddKeyframeCommand.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/AddLayerStyleCommand.h"
#include "MotionStudio/undo/AddMaskCommand.h"
#include "MotionStudio/undo/ConvertGeometryToPathCommand.h"
#include "MotionStudio/undo/MoveKeyframeCommand.h"
#include "MotionStudio/undo/MoveLayerCommand.h"
#include "MotionStudio/undo/MoveMaskCommand.h"
#include "MotionStudio/undo/RemoveKeyframeCommand.h"
#include "MotionStudio/undo/RemoveLayerCommand.h"
#include "MotionStudio/undo/RemoveMaskCommand.h"
#include "MotionStudio/undo/RemoveStyleCommand.h"
#include "MotionStudio/undo/SetCompositionBackgroundColorCommand.h"
#include "MotionStudio/undo/SetCompositionCornerRadiusCommand.h"
#include "MotionStudio/undo/SetCompositionSettingsCommand.h"
#include "MotionStudio/undo/SetEasingCommand.h"
#include "MotionStudio/undo/SetLayerLockedCommand.h"
#include "MotionStudio/undo/SetLayerVisibleCommand.h"
#include "MotionStudio/undo/SetMaskInvertedCommand.h"
#include "MotionStudio/undo/SetMaskModeCommand.h"
#include "MotionStudio/undo/SetStaticValueCommand.h"
#include "MotionStudio/undo/SetStrokePositionCommand.h"
#include "MotionStudio/undo/SetStyleBlendModeCommand.h"
#include "MotionStudio/undo/SetTrackMatteCommand.h"
#include "MotionStudio/undo/UndoManager.h"

#include "DocumentLock.h"
#include "MSDocument.h"

using motion::Animatable;
using motion::AnimatableBase;
using motion::AnimatableType;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FillStyle;
using motion::FrameTime;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::PropertyPath;
using motion::ResolveAnimatable;
using motion::SceneEvaluator;
using motion::Serializer;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapePath;
using motion::ShapeRect;
using motion::StrokeStyle;
using motion::UndoManager;
using motion::Vec2;

namespace {

Document *Doc(MSDocument *handle) {
    return handle != nullptr ? handle->document.get() : nullptr;
}

Composition *FindComposition(MSDocument *handle, uint64_t compositionId) {
    Document *document = Doc(handle);
    if (document == nullptr) {
        return nullptr;
    }
    for (auto &composition : document->compositions) {
        if (composition->id.value == compositionId) {
            return composition.get();
        }
    }
    return nullptr;
}

Layer *FindLayer(MSDocument *handle, uint64_t layerId) {
    Document *document = Doc(handle);
    if (document == nullptr) {
        return nullptr;
    }
    return document->entityIndex().findLayer(EntityId{layerId});
}

AnimatableBase *FindProperty(MSDocument *handle, uint64_t entityId, const char *path) {
    Document *document = Doc(handle);
    if (document == nullptr || path == nullptr) {
        return nullptr;
    }
    return ResolveAnimatable(*document, PropertyPath{EntityId{entityId}, path});
}

// Downcasts via valueType() (dynamic_cast is banned by the coding rules).
const Animatable<float> *AsFloat(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Float) {
        return nullptr;
    }
    return static_cast<const Animatable<float> *>(base);
}

const Animatable<Vec2> *AsVec2(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Vec2) {
        return nullptr;
    }
    return static_cast<const Animatable<Vec2> *>(base);
}

const Animatable<Color> *AsColor(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Color) {
        return nullptr;
    }
    return static_cast<const Animatable<Color> *>(base);
}

const Animatable<motion::BezierPath> *AsBezierPath(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::BezierPath) {
        return nullptr;
    }
    return static_cast<const Animatable<motion::BezierPath> *>(base);
}

MSBezierPath *AllocateMSBezierPath(const motion::BezierPath &path) {
    auto *result = static_cast<MSBezierPath *>(std::malloc(sizeof(MSBezierPath)));
    if (result == nullptr) {
        return nullptr;
    }
    result->count = path.vertices.size();
    result->closed = path.closed;
    result->vertices = nullptr;
    if (result->count > 0) {
        result->vertices =
            static_cast<MSBezierVertex *>(std::calloc(result->count, sizeof(MSBezierVertex)));
        if (result->vertices == nullptr) {
            std::free(result);
            return nullptr;
        }
        for (size_t index = 0; index < result->count; ++index) {
            const motion::BezierPath::Vertex &vertex = path.vertices[index];
            result->vertices[index].pointX = vertex.point.x;
            result->vertices[index].pointY = vertex.point.y;
            result->vertices[index].inTangentX = vertex.inTangent.x;
            result->vertices[index].inTangentY = vertex.inTangent.y;
            result->vertices[index].outTangentX = vertex.outTangent.x;
            result->vertices[index].outTangentY = vertex.outTangent.y;
        }
    }
    return result;
}

motion::BezierPath FromMSBezierPath(const MSBezierPath *path) {
    motion::BezierPath result;
    if (path == nullptr) {
        return result;
    }
    result.closed = path->closed;
    if (path->vertices == nullptr || path->count == 0) {
        return result;
    }
    result.vertices.reserve(path->count);
    for (size_t index = 0; index < path->count; ++index) {
        const MSBezierVertex &vertex = path->vertices[index];
        result.vertices.push_back({{vertex.pointX, vertex.pointY},
                                   {vertex.inTangentX, vertex.inTangentY},
                                   {vertex.outTangentX, vertex.outTangentY}});
    }
    return result;
}

// Builds a fully-initialized keyframe (avoids partial aggregate init warnings).
template <typename T>
Keyframe<T> MakeKeyframe(FrameTime time, T value) {
    Keyframe<T> keyframe;
    keyframe.time = time;
    keyframe.value = std::move(value);
    return keyframe;
}

void Execute(MSDocument *handle, std::unique_ptr<motion::Command> command) {
    if (handle == nullptr) {
        return;
    }
    handle->undoManager->execute(*handle->document, std::move(command));
}

PropertyPath MakePath(uint64_t entityId, const char *path) {
    return PropertyPath{EntityId{entityId}, path != nullptr ? path : ""};
}

motion::BlendMode MakeBlendMode(MS_BLEND blendMode) {
    // Bridge tags mirror the motion::BlendMode ordinals.
    if (blendMode < MS_BLEND_NORMAL || blendMode > MS_BLEND_ADD) {
        return motion::BlendMode::Normal;
    }
    return static_cast<motion::BlendMode>(blendMode);
}

motion::StrokePosition MakeStrokePosition(MS_STROKE_POSITION position) {
    // Bridge tags mirror the motion::StrokePosition ordinals.
    if (position < MS_STROKE_POSITION_CENTER || position > MS_STROKE_POSITION_OUTSIDE) {
        return motion::StrokePosition::Center;
    }
    return static_cast<motion::StrokePosition>(position);
}

motion::MaskMode MakeMaskMode(MS_MASK mode) {
    if (mode < MS_MASK_ADD || mode > MS_MASK_INTERSECT) {
        return motion::MaskMode::Add;
    }
    return static_cast<motion::MaskMode>(mode);
}

motion::TrackMatteType MakeTrackMatteType(MS_TRACK_MATTE type) {
    if (type < MS_TRACK_MATTE_NONE || type > MS_TRACK_MATTE_LUMA_INVERTED) {
        return motion::TrackMatteType::None;
    }
    return static_cast<motion::TrackMatteType>(type);
}

motion::Mask MakeMaskFromLayer(const Layer &layer, int64_t frame) {
    motion::Mask mask;
    mask.path.setStaticValue(motion::BakeMaskPathFromLayer(layer, frame));
    return mask;
}

Easing MakeEasing(int easingType, float inX, float inY, float outX, float outY) {
    switch (easingType) {
        case MS_EASING_HOLD: {
            return Easing::Hold();
        }
        case MS_EASING_EASE: {
            return Easing::Ease();
        }
        case MS_EASING_EASE_IN: {
            return Easing::EaseIn();
        }
        case MS_EASING_EASE_OUT: {
            return Easing::EaseOut();
        }
        case MS_EASING_EASE_IN_OUT: {
            return Easing::EaseInOut();
        }
        case MS_EASING_CUBIC_BEZIER: {
            return Easing::Bezier(inX, inY, outX, outY);
        }
        default: {
            return Easing::Linear();
        }
    }
}

const Color SHAPE_PALETTE[6] = {
    {0.29f, 0.56f, 0.89f, 1.0f},
    {0.91f, 0.52f, 0.29f, 1.0f},
    {0.40f, 0.76f, 0.45f, 1.0f},
    {0.69f, 0.42f, 0.87f, 1.0f},
    {0.96f, 0.71f, 0.25f, 1.0f},
    {0.90f, 0.38f, 0.45f, 1.0f},
};

uint64_t AddShapeLayer(MSDocument *handle, uint64_t compositionId, bool ellipse) {
    Composition *composition = FindComposition(handle, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = (ellipse ? "Ellipse " : "Rectangle ") + std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.position.setStaticValue(Vec2{composition->width * 0.5f, composition->height * 0.5f});

    auto *content = static_cast<ShapeContent *>(layer->content.get());
    if (ellipse) {
        auto shape = std::make_unique<ShapeEllipse>();
        shape->size.setStaticValue(Vec2{200.0f, 200.0f});
        content->geometry = std::move(shape);
    } else {
        auto shape = std::make_unique<ShapeRect>();
        shape->size.setStaticValue(Vec2{200.0f, 200.0f});
        content->geometry = std::move(shape);
    }
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(SHAPE_PALETTE[composition->layers.size() % 6]);
    layer->styles.push_back(std::move(fill));

    const uint64_t layerId = layer->id.value;
    Execute(handle, std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

uint64_t AddPathLayer(MSDocument *handle, uint64_t compositionId) {
    Composition *composition = FindComposition(handle, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "Path " + std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.position.setStaticValue(Vec2{composition->width * 0.5f, composition->height * 0.5f});

    auto *content = static_cast<ShapeContent *>(layer->content.get());
    content->geometry = std::make_unique<ShapePath>();

    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(SHAPE_PALETTE[composition->layers.size() % 6]);
    layer->styles.push_back(std::move(fill));

    const uint64_t layerId = layer->id.value;
    Execute(handle, std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

}  // namespace

/* ============================ lifecycle ============================ */

MSDocument *ms_document_create(void) {
    auto *handle = new MSDocument();
    handle->document = std::make_unique<Document>();
    handle->undoManager = std::make_unique<UndoManager>();

    auto composition = std::make_unique<Composition>();
    composition->name = "Composition 1";
    composition->duration = 150;  // 5 seconds at the default 30 fps
    handle->document->addComposition(std::move(composition));
    return handle;
}

MSDocument *ms_document_load(const char *jsonText, size_t length, char **errorOut) {
    if (jsonText == nullptr) {
        return nullptr;
    }
    auto result = Serializer::deserialize(std::string(jsonText, length));
    if (!result.hasValue()) {
        if (errorOut != nullptr) {
            *errorOut = strdup(result.error().c_str());
        }
        return nullptr;
    }
    auto *handle = new MSDocument();
    handle->document = std::move(result).value();
    handle->undoManager = std::make_unique<UndoManager>();
    return handle;
}

void ms_document_destroy(MSDocument *document) {
    // No lock: destroying implies unique ownership of the handle.
    delete document;
}

char *ms_document_save(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr) {
        return nullptr;
    }
    return strdup(Serializer::serialize(*document->document).c_str());
}

void ms_string_free(char *string) {
    free(string);
}

void ms_bezier_path_free(MSBezierPath *path) {
    if (path == nullptr) {
        return;
    }
    free(path->vertices);
    free(path);
}

/* ============================ undo / redo ============================ */

bool ms_document_undo(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canUndo()) {
        return false;
    }
    document->undoManager->undo(*document->document);
    return true;
}

bool ms_document_redo(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canRedo()) {
        return false;
    }
    document->undoManager->redo(*document->document);
    return true;
}

bool ms_document_can_undo(MSDocument *document) {
    DocumentLock guard(document);
    return document != nullptr && document->undoManager->canUndo();
}

bool ms_document_can_redo(MSDocument *document) {
    DocumentLock guard(document);
    return document != nullptr && document->undoManager->canRedo();
}

char *ms_document_undo_description(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canUndo()) {
        return nullptr;
    }
    return strdup(document->undoManager->undoDescription().c_str());
}

char *ms_document_redo_description(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canRedo()) {
        return nullptr;
    }
    return strdup(document->undoManager->redoDescription().c_str());
}

void ms_document_begin_merge_group(MSDocument *document) {
    DocumentLock guard(document);
    if (document != nullptr) {
        document->undoManager->beginMergeGroup();
    }
}

void ms_document_end_merge_group(MSDocument *document) {
    DocumentLock guard(document);
    if (document != nullptr) {
        document->undoManager->endMergeGroup();
    }
}

/* ============================ composition queries ============================ */

int ms_document_composition_count(MSDocument *document) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    return doc != nullptr ? static_cast<int>(doc->compositions.size()) : 0;
}

uint64_t ms_document_composition_id_at(MSDocument *document, int index) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    if (doc == nullptr || index < 0 || static_cast<size_t>(index) >= doc->compositions.size()) {
        return 0;
    }
    return doc->compositions[static_cast<size_t>(index)]->id.value;
}

int64_t ms_composition_duration(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->duration : 0;
}

int ms_composition_width(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->width : 0;
}

int ms_composition_height(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->height : 0;
}

int ms_composition_frame_rate_num(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? static_cast<int>(composition->frameRate.num) : 0;
}

int ms_composition_frame_rate_den(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? static_cast<int>(composition->frameRate.den) : 0;
}

int ms_composition_layer_count(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? static_cast<int>(composition->layers.size()) : 0;
}

void ms_composition_background_color(MSDocument *document, uint64_t compositionId, float *r, float *g, float *b, float *a) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    if (r != nullptr) {
        *r = composition->backgroundColor.r;
    }
    if (g != nullptr) {
        *g = composition->backgroundColor.g;
    }
    if (b != nullptr) {
        *b = composition->backgroundColor.b;
    }
    if (a != nullptr) {
        *a = composition->backgroundColor.a;
    }
}

float ms_composition_corner_radius(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->cornerRadius : 0.0f;
}

uint64_t ms_composition_hit_test_layer(MSDocument *document, uint64_t compositionId, double frameTime, float x, float y, float tolerance) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    if (doc == nullptr) {
        return 0;
    }
    auto result = SceneEvaluator::EvaluatePreview(*doc, EntityId{compositionId}, motion::PreviewTime(frameTime));
    if (!result.hasValue()) {
        return 0;
    }
    const motion::SceneState &state = result.value();
    for (auto it = state.layers.rbegin(); it != state.layers.rend(); ++it) {
        const Layer *layer = doc->entityIndex().findLayer(it->id);
        if (layer != nullptr && !layer->locked && motion::HitTestLayer(*it, Vec2{x, y}, tolerance)) {
            return it->id.value;
        }
    }
    return 0;
}

bool ms_composition_layer_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                                 float *minX, float *minY, float *maxX, float *maxY) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    if (doc == nullptr) {
        return false;
    }
    auto result = SceneEvaluator::EvaluatePreview(*doc, EntityId{compositionId}, motion::PreviewTime(frameTime));
    if (!result.hasValue()) {
        return false;
    }
    for (const motion::EvaluatedLayer &layer : result.value().layers) {
        if (layer.id.value != layerId) {
            continue;
        }
        Vec2 minPoint;
        Vec2 maxPoint;
        if (!motion::BoundsOfLayer(layer, minPoint, maxPoint)) {
            return false;
        }
        if (minX != nullptr) {
            *minX = minPoint.x;
        }
        if (minY != nullptr) {
            *minY = minPoint.y;
        }
        if (maxX != nullptr) {
            *maxX = maxPoint.x;
        }
        if (maxY != nullptr) {
            *maxY = maxPoint.y;
        }
        return true;
    }
    return false;
}

bool ms_composition_selection_handles(MSDocument *document, uint64_t compositionId, double frameTime,
                                      const uint64_t *layerIds, size_t count, uint64_t primaryLayerId,
                                      MSSelectionHandles *out) {
    if (out != nullptr) {
        *out = {};
    }
    DocumentLock guard(document);
    Document *doc = Doc(document);
    if (doc == nullptr || (count > 0 && layerIds == nullptr)) {
        return false;
    }
    auto result =
        SceneEvaluator::EvaluatePreview(*doc, EntityId{compositionId}, motion::PreviewTime(frameTime));
    if (!result.hasValue()) {
        return false;
    }
    std::vector<EntityId> selected;
    selected.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        selected.push_back(EntityId{layerIds[index]});
    }
    motion::SelectionHandles handles;
    if (!motion::BuildSelectionHandles(result.value(), selected, EntityId{primaryLayerId}, handles)) {
        return false;
    }
    if (out == nullptr) {
        return true;
    }
    out->valid = handles.valid ? 1 : 0;
    out->isOriented = handles.isOriented ? 1 : 0;
    for (int index = 0; index < 4; ++index) {
        out->cornersX[index] = handles.corners[index].x;
        out->cornersY[index] = handles.corners[index].y;
        out->edgeMidsX[index] = handles.edgeMids[index].x;
        out->edgeMidsY[index] = handles.edgeMids[index].y;
    }
    out->centerX = handles.center.x;
    out->centerY = handles.center.y;
    out->anchorX = handles.anchor.x;
    out->anchorY = handles.anchor.y;
    out->primaryLayerId = handles.primaryLayerId.value;
    out->boxRotationDegrees = handles.boxRotationDegrees;
    out->localMinX = handles.localMin.x;
    out->localMinY = handles.localMin.y;
    out->localMaxX = handles.localMax.x;
    out->localMaxY = handles.localMax.y;
    return true;
}

MS_SELECTION_HANDLE ms_selection_handles_hit_test(const MSSelectionHandles *handles, float x, float y,
                                                  float handleHitRadius, float rotateInner, float rotateOuter) {
    if (handles == nullptr || handles->valid == 0) {
        return MS_SELECTION_HANDLE_NONE;
    }
    motion::SelectionHandles coreHandles;
    coreHandles.valid = true;
    coreHandles.isOriented = handles->isOriented != 0;
    for (int index = 0; index < 4; ++index) {
        coreHandles.corners[index] = {handles->cornersX[index], handles->cornersY[index]};
        coreHandles.edgeMids[index] = {handles->edgeMidsX[index], handles->edgeMidsY[index]};
    }
    coreHandles.center = {handles->centerX, handles->centerY};
    coreHandles.anchor = {handles->anchorX, handles->anchorY};
    coreHandles.primaryLayerId = EntityId{handles->primaryLayerId};
    coreHandles.boxRotationDegrees = handles->boxRotationDegrees;
    coreHandles.localMin = {handles->localMinX, handles->localMinY};
    coreHandles.localMax = {handles->localMaxX, handles->localMaxY};
    switch (motion::HitTestSelectionHandle(coreHandles, Vec2{x, y}, handleHitRadius, rotateInner, rotateOuter)) {
        case motion::SelectionHandleKind::None:
            return MS_SELECTION_HANDLE_NONE;
        case motion::SelectionHandleKind::Anchor:
            return MS_SELECTION_HANDLE_ANCHOR;
        case motion::SelectionHandleKind::ScaleCorner0:
            return MS_SELECTION_HANDLE_SCALE_CORNER0;
        case motion::SelectionHandleKind::ScaleCorner1:
            return MS_SELECTION_HANDLE_SCALE_CORNER1;
        case motion::SelectionHandleKind::ScaleCorner2:
            return MS_SELECTION_HANDLE_SCALE_CORNER2;
        case motion::SelectionHandleKind::ScaleCorner3:
            return MS_SELECTION_HANDLE_SCALE_CORNER3;
        case motion::SelectionHandleKind::ScaleEdge0:
            return MS_SELECTION_HANDLE_SCALE_EDGE0;
        case motion::SelectionHandleKind::ScaleEdge1:
            return MS_SELECTION_HANDLE_SCALE_EDGE1;
        case motion::SelectionHandleKind::ScaleEdge2:
            return MS_SELECTION_HANDLE_SCALE_EDGE2;
        case motion::SelectionHandleKind::ScaleEdge3:
            return MS_SELECTION_HANDLE_SCALE_EDGE3;
        case motion::SelectionHandleKind::Rotate0:
            return MS_SELECTION_HANDLE_ROTATE0;
        case motion::SelectionHandleKind::Rotate1:
            return MS_SELECTION_HANDLE_ROTATE1;
        case motion::SelectionHandleKind::Rotate2:
            return MS_SELECTION_HANDLE_ROTATE2;
        case motion::SelectionHandleKind::Rotate3:
            return MS_SELECTION_HANDLE_ROTATE3;
    }
    return MS_SELECTION_HANDLE_NONE;
}

char *ms_composition_name(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? strdup(composition->name.c_str()) : nullptr;
}

/* ============================ layer queries ============================ */

uint64_t ms_layer_id_at(MSDocument *document, uint64_t compositionId, int index) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr || index < 0 || static_cast<size_t>(index) >= composition->layers.size()) {
        return 0;
    }
    return composition->layers[static_cast<size_t>(index)]->id.value;
}

char *ms_layer_name(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? strdup(layer->name.c_str()) : nullptr;
}

MS_LAYER ms_layer_type(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return MS_LAYER_INVALID;
    }
    return static_cast<MS_LAYER>(layer->type());
}

int64_t ms_layer_in_point(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? layer->inPoint : 0;
}

int64_t ms_layer_out_point(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? layer->outPoint : 0;
}

uint64_t ms_layer_parent_id(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? layer->parentId.value : 0;
}

bool ms_layer_visible(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr && layer->visible;
}

bool ms_layer_locked(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr && layer->locked;
}

/* ============================ layer style queries ============================ */

int ms_layer_style_count(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? static_cast<int>(layer->styles.size()) : 0;
}

MS_STYLE ms_layer_style_type_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_STYLE_INVALID;
    }
    switch (layer->styles[static_cast<size_t>(index)]->type()) {
        case motion::LayerStyleType::Fill: {
            return MS_STYLE_FILL;
        }
        case motion::LayerStyleType::Stroke: {
            return MS_STYLE_STROKE;
        }
    }
    return MS_STYLE_INVALID;
}

MS_BLEND ms_layer_style_blend_mode_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_BLEND_INVALID;
    }
    motion::LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    switch (style->type()) {
        case motion::LayerStyleType::Fill: {
            return static_cast<MS_BLEND>(static_cast<FillStyle *>(style)->blendMode);
        }
        case motion::LayerStyleType::Stroke: {
            return static_cast<MS_BLEND>(static_cast<StrokeStyle *>(style)->blendMode);
        }
    }
    return MS_BLEND_INVALID;
}

MS_STROKE_POSITION ms_layer_style_stroke_position_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_STROKE_POSITION_INVALID;
    }
    motion::LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    if (style->type() != motion::LayerStyleType::Stroke) {
        return MS_STROKE_POSITION_INVALID;
    }
    return static_cast<MS_STROKE_POSITION>(static_cast<StrokeStyle *>(style)->position);
}

/* ============================ mask / track matte queries ============================ */

int ms_layer_mask_count(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? static_cast<int>(layer->masks.size()) : 0;
}

MS_MASK ms_layer_mask_mode_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->masks.size()) {
        return MS_MASK_INVALID;
    }
    return static_cast<MS_MASK>(layer->masks[static_cast<size_t>(index)].mode);
}

bool ms_layer_mask_inverted_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->masks.size()) {
        return false;
    }
    return layer->masks[static_cast<size_t>(index)].inverted;
}

MS_TRACK_MATTE ms_layer_track_matte_type(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return MS_TRACK_MATTE_NONE;
    }
    return static_cast<MS_TRACK_MATTE>(layer->trackMatteType);
}

uint64_t ms_layer_track_matte_layer_id(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return 0;
    }
    return layer->trackMatteLayerId.value;
}

/* ============================ property queries ============================ */

MS_VALUE ms_property_type(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    return property != nullptr ? static_cast<MS_VALUE>(property->valueType()) : MS_VALUE_INVALID;
}

bool ms_property_is_animated(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr) {
        return false;
    }
    switch (property->valueType()) {
        case AnimatableType::Float: {
            return AsFloat(property)->isAnimated();
        }
        case AnimatableType::Vec2: {
            return AsVec2(property)->isAnimated();
        }
        case AnimatableType::Color: {
            return AsColor(property)->isAnimated();
        }
        case AnimatableType::BezierPath: {
            return static_cast<const Animatable<motion::BezierPath> *>(property)->isAnimated();
        }
        case AnimatableType::String: {
            return static_cast<const Animatable<std::string> *>(property)->isAnimated();
        }
    }
    return false;
}

float ms_property_static_float(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    const Animatable<float> *property = AsFloat(FindProperty(document, entityId, path));
    return property != nullptr ? property->staticValue() : 0.0f;
}

void ms_property_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float *x, float *y) {
    DocumentLock guard(document);
    const Animatable<Vec2> *property = AsVec2(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Vec2 value = property->staticValue();
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
}

void ms_property_static_color(MSDocument *document, uint64_t entityId, const char *path, float *r, float *g, float *b, float *a) {
    DocumentLock guard(document);
    const Animatable<Color> *property = AsColor(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Color value = property->staticValue();
    if (r != nullptr) {
        *r = value.r;
    }
    if (g != nullptr) {
        *g = value.g;
    }
    if (b != nullptr) {
        *b = value.b;
    }
    if (a != nullptr) {
        *a = value.a;
    }
}

MSBezierPath *ms_property_static_bezier_path(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    const Animatable<motion::BezierPath> *property = AsBezierPath(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(property->staticValue());
}

int ms_property_keyframe_count(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr) {
        return 0;
    }
    switch (property->valueType()) {
        case AnimatableType::Float: {
            return static_cast<int>(AsFloat(property)->keyframes().size());
        }
        case AnimatableType::Vec2: {
            return static_cast<int>(AsVec2(property)->keyframes().size());
        }
        case AnimatableType::Color: {
            return static_cast<int>(AsColor(property)->keyframes().size());
        }
        case AnimatableType::BezierPath: {
            return static_cast<int>(static_cast<const Animatable<motion::BezierPath> *>(property)->keyframes().size());
        }
        case AnimatableType::String: {
            return static_cast<int>(static_cast<const Animatable<std::string> *>(property)->keyframes().size());
        }
    }
    return 0;
}

// Looks up the keyframe at index for the expected value type.
template <typename T>
const Keyframe<T> *KeyframeAt(const Animatable<T> *property, int index) {
    if (property == nullptr || index < 0 || static_cast<size_t>(index) >= property->keyframes().size()) {
        return nullptr;
    }
    return &property->keyframes()[static_cast<size_t>(index)];
}

int64_t ms_property_keyframe_time_at(MSDocument *document, uint64_t entityId, const char *path, int index) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    const Keyframe<float> *floatKey = KeyframeAt(AsFloat(property), index);
    if (floatKey != nullptr) {
        return floatKey->time;
    }
    const Keyframe<Vec2> *vec2Key = KeyframeAt(AsVec2(property), index);
    if (vec2Key != nullptr) {
        return vec2Key->time;
    }
    const Keyframe<Color> *colorKey = KeyframeAt(AsColor(property), index);
    if (colorKey != nullptr) {
        return colorKey->time;
    }
    return 0;
}

float ms_property_keyframe_float_at(MSDocument *document, uint64_t entityId, const char *path, int index) {
    DocumentLock guard(document);
    const Keyframe<float> *keyframe = KeyframeAt(AsFloat(FindProperty(document, entityId, path)), index);
    return keyframe != nullptr ? keyframe->value : 0.0f;
}

void ms_property_keyframe_vec2_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *x, float *y) {
    DocumentLock guard(document);
    const Keyframe<Vec2> *keyframe = KeyframeAt(AsVec2(FindProperty(document, entityId, path)), index);
    if (keyframe == nullptr) {
        return;
    }
    if (x != nullptr) {
        *x = keyframe->value.x;
    }
    if (y != nullptr) {
        *y = keyframe->value.y;
    }
}

MS_EASING ms_property_keyframe_easing_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *inX, float *inY, float *outX, float *outY) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    const Easing *easing = nullptr;
    const Keyframe<float> *floatKey = KeyframeAt(AsFloat(property), index);
    if (floatKey != nullptr) {
        easing = &floatKey->easing;
    }
    const Keyframe<Vec2> *vec2Key = KeyframeAt(AsVec2(property), index);
    if (vec2Key != nullptr) {
        easing = &vec2Key->easing;
    }
    const Keyframe<Color> *colorKey = KeyframeAt(AsColor(property), index);
    if (colorKey != nullptr) {
        easing = &colorKey->easing;
    }
    if (easing == nullptr) {
        return MS_EASING_INVALID;
    }
    if (inX != nullptr) {
        *inX = easing->inX;
    }
    if (inY != nullptr) {
        *inY = easing->inY;
    }
    if (outX != nullptr) {
        *outX = easing->outX;
    }
    if (outY != nullptr) {
        *outY = easing->outY;
    }
    return static_cast<MS_EASING>(easing->type);
}

float ms_property_evaluate_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame) {
    DocumentLock guard(document);
    const Animatable<float> *property = AsFloat(FindProperty(document, entityId, path));
    return property != nullptr ? property->evaluate(static_cast<FrameTime>(frame)) : 0.0f;
}

void ms_property_evaluate_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *x, float *y) {
    DocumentLock guard(document);
    const Animatable<Vec2> *property = AsVec2(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Vec2 value = property->evaluate(static_cast<FrameTime>(frame));
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
}

void ms_property_evaluate_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *r, float *g, float *b, float *a) {
    DocumentLock guard(document);
    const Animatable<Color> *property = AsColor(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Color value = property->evaluate(static_cast<FrameTime>(frame));
    if (r != nullptr) {
        *r = value.r;
    }
    if (g != nullptr) {
        *g = value.g;
    }
    if (b != nullptr) {
        *b = value.b;
    }
    if (a != nullptr) {
        *a = value.a;
    }
}

MSBezierPath *ms_property_evaluate_bezier_path(MSDocument *document, uint64_t entityId, const char *path,
                                               int64_t frame) {
    DocumentLock guard(document);
    const Animatable<motion::BezierPath> *property = AsBezierPath(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(property->evaluate(static_cast<FrameTime>(frame)));
}

/* ============================ commands ============================ */

void ms_command_set_static_float(MSDocument *document, uint64_t entityId, const char *path, float value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(value)));
}

void ms_command_set_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float x, float y) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Vec2{x, y})));
}

void ms_command_set_static_color(MSDocument *document, uint64_t entityId, const char *path, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Color{r, g, b, a})));
}

void ms_command_set_static_bezier_path(MSDocument *document, uint64_t entityId, const char *path,
                                       const MSBezierPath *value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(
                          MakePath(entityId, path), motion::PropertyValue(FromMSBezierPath(value))));
}

void ms_command_set_composition_background_color(MSDocument *document, uint64_t compositionId, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetCompositionBackgroundColorCommand>(EntityId{compositionId}, Color{r, g, b, a}));
}

void ms_command_set_composition_corner_radius(MSDocument *document, uint64_t compositionId, float cornerRadius) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetCompositionCornerRadiusCommand>(EntityId{compositionId}, cornerRadius));
}

void ms_command_set_composition_size(MSDocument *document, uint64_t compositionId, int width, int height) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = width;
    settings.height = height;
    settings.duration = composition->duration;
    settings.frameRate = composition->frameRate;
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_set_composition_duration(MSDocument *document, uint64_t compositionId, int64_t duration) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = composition->width;
    settings.height = composition->height;
    settings.duration = static_cast<FrameTime>(duration);
    settings.frameRate = composition->frameRate;
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_set_composition_frame_rate(MSDocument *document, uint64_t compositionId, int frameRateNum, int frameRateDen) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr || frameRateNum <= 0 || frameRateDen <= 0) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = composition->width;
    settings.height = composition->height;
    settings.duration = composition->duration;
    settings.frameRate = {static_cast<uint32_t>(frameRateNum),
                          static_cast<uint32_t>(frameRateDen)};
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_add_keyframe_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), value))));
}

void ms_command_add_keyframe_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x, float y) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), Vec2{x, y}))));
}

void ms_command_add_keyframe_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), Color{r, g, b, a}))));
}

void ms_command_add_keyframe_bezier_path(MSDocument *document, uint64_t entityId, const char *path,
                                         int64_t frame, const MSBezierPath *value) {
    DocumentLock guard(document);
    Execute(document,
            std::make_unique<motion::AddKeyframeCommand>(
                MakePath(entityId, path),
                motion::KeyframeData(
                    MakeKeyframe(static_cast<FrameTime>(frame), FromMSBezierPath(value)))));
}

void ms_command_remove_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t frame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveKeyframeCommand>(MakePath(entityId, path), static_cast<FrameTime>(frame)));
}

void ms_command_move_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t oldFrame, int64_t newFrame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveKeyframeCommand>(MakePath(entityId, path), static_cast<FrameTime>(oldFrame), static_cast<FrameTime>(newFrame)));
}

void ms_command_set_easing(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, int easingType, float inX, float inY, float outX, float outY) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetEasingCommand>(MakePath(entityId, path), static_cast<FrameTime>(frame), MakeEasing(easingType, inX, inY, outX, outY)));
}

uint64_t ms_command_add_rect_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddShapeLayer(document, compositionId, false);
}

uint64_t ms_command_add_ellipse_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddShapeLayer(document, compositionId, true);
}

uint64_t ms_command_add_path_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddPathLayer(document, compositionId);
}

void ms_command_convert_geometry_to_path(MSDocument *document, uint64_t layerId, int64_t frame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::ConvertGeometryToPathCommand>(
                          EntityId{layerId}, static_cast<FrameTime>(frame)));
}

void ms_command_remove_layer(MSDocument *document, uint64_t compositionId, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveLayerCommand>(EntityId{compositionId}, EntityId{layerId}));
}

void ms_command_move_layer(MSDocument *document, uint64_t compositionId, int fromIndex, int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveLayerCommand>(EntityId{compositionId}, fromIndex, toIndex));
}

void ms_command_set_layer_visible(MSDocument *document, uint64_t layerId, bool visible) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerVisibleCommand>(EntityId{layerId}, visible));
}

void ms_command_set_layer_locked(MSDocument *document, uint64_t layerId, bool locked) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerLockedCommand>(EntityId{layerId}, locked));
}

void ms_command_add_fill_style(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerStyleCommand>(EntityId{layerId}, std::make_unique<motion::FillStyle>()));
}

void ms_command_add_stroke_style(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerStyleCommand>(EntityId{layerId}, std::make_unique<motion::StrokeStyle>()));
}

void ms_command_remove_style(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveStyleCommand>(EntityId{layerId}, index));
}

void ms_command_set_style_blend_mode(MSDocument *document, uint64_t layerId, int index, MS_BLEND blendMode) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStyleBlendModeCommand>(EntityId{layerId}, index, MakeBlendMode(blendMode)));
}

void ms_command_set_stroke_position(MSDocument *document, uint64_t layerId, int index, MS_STROKE_POSITION position) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStrokePositionCommand>(EntityId{layerId}, index, MakeStrokePosition(position)));
}

void ms_command_add_mask(MSDocument *document, uint64_t layerId, int64_t frame) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return;
    }
    Execute(document, std::make_unique<motion::AddMaskCommand>(EntityId{layerId}, MakeMaskFromLayer(*layer, frame)));
}

void ms_command_remove_mask(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveMaskCommand>(EntityId{layerId}, index));
}

void ms_command_move_mask(MSDocument *document, uint64_t layerId, int fromIndex, int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveMaskCommand>(EntityId{layerId}, fromIndex, toIndex));
}

void ms_command_set_mask_mode(MSDocument *document, uint64_t layerId, int index, MS_MASK mode) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetMaskModeCommand>(EntityId{layerId}, index, MakeMaskMode(mode)));
}

void ms_command_set_mask_inverted(MSDocument *document, uint64_t layerId, int index,
                                  bool inverted) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetMaskInvertedCommand>(EntityId{layerId}, index, inverted));
}

void ms_command_set_track_matte(MSDocument *document, uint64_t layerId, uint64_t matteLayerId,
                                MS_TRACK_MATTE type) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetTrackMatteCommand>(EntityId{layerId}, EntityId{matteLayerId}, MakeTrackMatteType(type)));
}
