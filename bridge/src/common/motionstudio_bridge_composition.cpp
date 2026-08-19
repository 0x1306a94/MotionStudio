#include "motionstudio_bridge.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/render/HitTest.h"
#include "MotionStudio/render/SelectionHandles.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"
#include "PreviewEnsure.h"

using namespace bridge;

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::FrameTime;
using motion::Layer;
using motion::Mat3;
using motion::Vec2;

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
    if (Doc(document) == nullptr) {
        return 0;
    }
    auto ensured = EnsurePreviewScene(document, compositionId, motion::PreviewTime(frameTime));
    if (!ensured.hasValue()) {
        return 0;
    }
    const motion::SceneState &state = ensured.value()->state;
    // Locked layers remain hittable/selectable; transform edits still refuse locks in the app.
    for (auto it = state.layers.rbegin(); it != state.layers.rend(); ++it) {
        if (motion::HitTestLayer(*it, Vec2{x, y}, tolerance)) {
            return it->id.value;
        }
    }
    return 0;
}

bool ms_composition_layer_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                                 float *minX, float *minY, float *maxX, float *maxY) {
    DocumentLock guard(document);
    if (Doc(document) == nullptr) {
        return false;
    }
    auto ensured = EnsurePreviewScene(document, compositionId, motion::PreviewTime(frameTime));
    if (!ensured.hasValue()) {
        return false;
    }
    const motion::SceneState &state = ensured.value()->state;
    for (const motion::EvaluatedLayer &layer : state.layers) {
        if (layer.id.value != layerId) {
            continue;
        }
        Vec2 minPoint;
        Vec2 maxPoint;
        if (!motion::BoundsOfLayerIncludingDescendants(state, layer, minPoint, maxPoint)) {
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

bool ms_layer_map_composition_delta(MSDocument *document, uint64_t compositionId, uint64_t layerId,
                                    double frameTime, float dx, float dy, float *outParentDx,
                                    float *outParentDy) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    if (doc == nullptr || outParentDx == nullptr || outParentDy == nullptr) {
        return false;
    }
    if (FindComposition(document, compositionId) == nullptr) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return false;
    }
    Mat3 parentWorld = Mat3::Identity();
    if (layer->parentId.isValid()) {
        const Layer *parent = doc->entityIndex().findLayer(layer->parentId);
        if (parent == nullptr) {
            return false;
        }
        parentWorld = parent->worldTransform(static_cast<FrameTime>(std::llround(frameTime)), *doc);
    }
    Mat3 inverse;
    if (!parentWorld.tryInvert(inverse)) {
        return false;
    }
    const Vec2 mapped = inverse.transformVector(Vec2{dx, dy});
    *outParentDx = mapped.x;
    *outParentDy = mapped.y;
    return true;
}

bool ms_layer_local_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                           float *minX, float *minY, float *maxX, float *maxY) {
    DocumentLock guard(document);
    if (Doc(document) == nullptr) {
        return false;
    }
    auto ensured = EnsurePreviewScene(document, compositionId, motion::PreviewTime(frameTime));
    if (!ensured.hasValue()) {
        return false;
    }
    const motion::SceneState &state = ensured.value()->state;
    for (const motion::EvaluatedLayer &layer : state.layers) {
        if (layer.id.value != layerId) {
            continue;
        }
        Vec2 minPoint;
        Vec2 maxPoint;
        if (!motion::BoundsOfLayerLocal(layer, minPoint, maxPoint) &&
            !motion::BoundsOfDescendantUnionLocal(state, layer.id, minPoint, maxPoint)) {
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
    if (Doc(document) == nullptr || (count > 0 && layerIds == nullptr)) {
        return false;
    }
    auto ensured = EnsurePreviewScene(document, compositionId, motion::PreviewTime(frameTime));
    if (!ensured.hasValue()) {
        return false;
    }
    const motion::SceneState &state = ensured.value()->state;
    std::vector<EntityId> selected;
    selected.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        selected.push_back(EntityId{layerIds[index]});
    }
    motion::SelectionHandles handles;
    if (!motion::BuildSelectionHandles(state, selected, EntityId{primaryLayerId}, handles)) {
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
