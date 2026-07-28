#include "motionstudio_bridge.h"

#include <chrono>
#include <utility>
#include <vector>

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSCanvas.h"
#include "MSDocument.h"

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/PathEditHandles.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::EntityId;

namespace {
using ProfileClock = std::chrono::steady_clock;

double Milliseconds(ProfileClock::time_point start, ProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

motion::Mat3 Mat3FromOverlay(const MSPathOverlayItem &item) {
    motion::Mat3 matrix = motion::Mat3::Identity();
    matrix.values[0] = item.m00;
    matrix.values[1] = item.m01;
    matrix.values[2] = item.m02;
    matrix.values[3] = item.m10;
    matrix.values[4] = item.m11;
    matrix.values[5] = item.m12;
    return matrix;
}

MS_PATH_HANDLE ToMSPathHandle(motion::PathHandleKind kind) {
    switch (kind) {
        case motion::PathHandleKind::None: {
            return MS_PATH_HANDLE_NONE;
        }
        case motion::PathHandleKind::Vertex: {
            return MS_PATH_HANDLE_VERTEX;
        }
        case motion::PathHandleKind::InTangent: {
            return MS_PATH_HANDLE_IN_TANGENT;
        }
        case motion::PathHandleKind::OutTangent: {
            return MS_PATH_HANDLE_OUT_TANGENT;
        }
        case motion::PathHandleKind::Segment: {
            return MS_PATH_HANDLE_SEGMENT;
        }
        case motion::PathHandleKind::CloseRing: {
            return MS_PATH_HANDLE_CLOSE_RING;
        }
    }
    return MS_PATH_HANDLE_NONE;
}

}  // namespace

void ms_canvas_destroy(MSCanvas *canvas) {
    delete canvas;
}

void ms_canvas_set_preview_backdrop(MSCanvas *canvas, MS_PREVIEWER_BACKDROP backdrop) {
    if (canvas == nullptr || canvas->adapter == nullptr) {
        return;
    }
    const auto mode = backdrop == MS_PREVIEWER_BACKDROP_TRANSPARENT
        ? motion::PreviewBackdrop::Transparent
        : motion::PreviewBackdrop::Black;
    canvas->adapter->setPreviewBackdrop(mode);
}

void ms_canvas_set_view_transform(MSCanvas *canvas, float zoom, float panX, float panY) {
    if (canvas == nullptr || canvas->adapter == nullptr) {
        return;
    }
    canvas->adapter->setViewTransform(zoom, panX, panY);
}

void ms_canvas_set_selected_layers(MSCanvas *canvas, const uint64_t *layerIds, size_t count) {
    if (canvas == nullptr) {
        return;
    }
    canvas->selectedLayerIds.clear();
    if (layerIds == nullptr || count == 0) {
        return;
    }
    canvas->selectedLayerIds.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        EntityId id{layerIds[index]};
        if (id.isValid()) {
            canvas->selectedLayerIds.push_back(id);
        }
    }
}

void ms_canvas_set_path_edit_target(MSCanvas *canvas, MS_PATH_EDIT kind, uint64_t layerId,
                                    int maskIndex, int selectedVertex) {
    if (canvas == nullptr) {
        return;
    }
    if (kind == MS_PATH_EDIT_NONE || layerId == 0) {
        canvas->hasPathEditTarget = false;
        canvas->pathEditTarget = {};
        canvas->pathEditSelectedVertex = -1;
        return;
    }
    canvas->hasPathEditTarget = true;
    canvas->pathEditTarget.kind =
        kind == MS_PATH_EDIT_MASK ? motion::PathEditKind::Mask : motion::PathEditKind::Shape;
    canvas->pathEditTarget.layerId = EntityId{layerId};
    canvas->pathEditTarget.maskIndex = maskIndex;
    canvas->pathEditSelectedVertex = selectedVertex;
}

void ms_canvas_set_path_overlays(MSCanvas *canvas, const MSPathOverlayItem *items, size_t count) {
    if (canvas == nullptr) {
        return;
    }
    canvas->customPathOverlays.clear();
    if (items == nullptr || count == 0) {
        return;
    }
    canvas->customPathOverlays.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const MSPathOverlayItem &item = items[index];
        motion::PathOverlayItem overlay;
            overlay.path = bridge::FromMSBezierPath(item.path);
        overlay.worldTransform = Mat3FromOverlay(item);
        overlay.color = motion::Color{item.r, item.g, item.b, item.a};
        canvas->customPathOverlays.push_back(std::move(overlay));
    }
}

MSPathEditHit ms_canvas_hit_path_edit(MSCanvas *canvas, MSDocument *document,
                                      uint64_t compositionId, double frameTime, float sceneX,
                                      float sceneY) {
    MSPathEditHit hit{};
    hit.kind = MS_PATH_HANDLE_NONE;
    if (canvas == nullptr || canvas->adapter == nullptr || document == nullptr ||
        !canvas->hasPathEditTarget) {
        return hit;
    }
    DocumentLock guard(document);
    auto result = motion::SceneEvaluator::EvaluatePreview(
        *document->document, EntityId{compositionId}, motion::PreviewTime(frameTime));
    if (!result.hasValue()) {
        return hit;
    }
    const motion::SceneState &state = result.value();
    motion::PathEditHandles handles;
    if (!motion::BuildPathEditHandles(state, canvas->pathEditTarget, canvas->pathEditSelectedVertex,
                                      handles)) {
        return hit;
    }
    const float viewUnit =
        canvas->adapter->sceneUnitsPerViewPoint(state.viewportWidth, state.viewportHeight);
    // Vertex chrome is ~7pt; keep hit close so mid-edge insert still works.
    const float handleRadius = 8.0f * viewUnit;
    const float segmentRadius = 6.0f * viewUnit;
    const motion::PathEditHit coreHit =
        motion::HitTestPathEdit(handles, {sceneX, sceneY}, handleRadius, segmentRadius);
    hit.kind = ToMSPathHandle(coreHit.kind);
    hit.index = coreHit.index;
    hit.segmentT = coreHit.segmentT;
    return hit;
}

void ms_canvas_draw_frame(MSCanvas *canvas, MSDocument *document, uint64_t compositionId,
                          int64_t frame) {
    ms_canvas_draw_frame_profiled(canvas, document, compositionId, frame, nullptr);
}

void ms_canvas_draw_frame_profiled(MSCanvas *canvas, MSDocument *document, uint64_t compositionId,
                                   int64_t frame, MSCanvasFrameProfile *profileOut) {
    ms_canvas_draw_frame_at_time_profiled(canvas, document, compositionId,
                                          static_cast<double>(frame), profileOut);
}

void ms_canvas_draw_frame_at_time_profiled(MSCanvas *canvas, MSDocument *document,
                                           uint64_t compositionId, double frameTime,
                                           MSCanvasFrameProfile *profileOut) {
    MSCanvasFrameProfile profile{};
    const auto totalStart = ProfileClock::now();
    const auto lockStart = ProfileClock::now();
    DocumentLock guard(document);
    const auto lockEnd = ProfileClock::now();
    profile.documentLockMs = Milliseconds(lockStart, lockEnd);

    if (canvas == nullptr || canvas->adapter == nullptr || document == nullptr) {
        profile.totalMs = Milliseconds(totalStart, ProfileClock::now());
        if (profileOut != nullptr) {
            *profileOut = profile;
        }
        return;
    }

    const auto evaluateStart = ProfileClock::now();
    auto result = motion::SceneEvaluator::EvaluatePreview(
        *document->document, EntityId{compositionId}, motion::PreviewTime(frameTime));
    const auto evaluateEnd = ProfileClock::now();
    profile.sceneEvaluateMs = Milliseconds(evaluateStart, evaluateEnd);
    if (!result.hasValue()) {
        profile.totalMs = Milliseconds(totalStart, ProfileClock::now());
        if (profileOut != nullptr) {
            *profileOut = profile;
        }
        return;
    }
    const motion::SceneState &state = result.value();
    profile.layerCount = state.layers.size();

    const auto buildStart = ProfileClock::now();
    motion::DrawCommandList commands = motion::BuildCommands(state);
    const float viewUnit =
        canvas->adapter->sceneUnitsPerViewPoint(state.viewportWidth, state.viewportHeight);
    const float outlineWidth = 1.5f * viewUnit;
    const float handleSize = 7.0f * viewUnit;
    constexpr motion::Color pathOverlayColor{1.0f, 0.85f, 0.2f, 1.0f};
    std::vector<motion::PathOverlayItem> pathOverlays =
        motion::CollectMaskPathOverlays(state, canvas->selectedLayerIds, pathOverlayColor);
    pathOverlays.insert(pathOverlays.end(), canvas->customPathOverlays.begin(),
                        canvas->customPathOverlays.end());
    motion::DrawCommandList pathOverlayCommands =
        motion::BuildPathOverlayCommands(pathOverlays, outlineWidth);

    motion::DrawCommandList pathEditCommands;
    if (canvas->hasPathEditTarget) {
        motion::PathEditHandles handles;
        if (motion::BuildPathEditHandles(state, canvas->pathEditTarget,
                                         canvas->pathEditSelectedVertex, handles)) {
            pathEditCommands = motion::BuildPathEditCommands(handles, outlineWidth, handleSize);
        }
    }

    motion::DrawCommandList selectionCommands;
    if (!canvas->hasPathEditTarget) {
        const motion::EntityId primaryLayerId =
            canvas->selectedLayerIds.empty() ? motion::EntityId{} : canvas->selectedLayerIds.back();
        selectionCommands = motion::BuildSelectionOutlineCommands(
            state, canvas->selectedLayerIds, primaryLayerId, outlineWidth, handleSize);
    }
    const auto buildEnd = ProfileClock::now();
    profile.buildCommandsMs = Milliseconds(buildStart, buildEnd);
    profile.drawCommandCount = commands.size() + pathOverlayCommands.size() +
        pathEditCommands.size() + selectionCommands.size();

    const auto beginFrameStart = ProfileClock::now();
    canvas->adapter->beginFrame(state.viewportWidth, state.viewportHeight, state.backgroundColor,
                                state.cornerRadius);
    const auto beginFrameEnd = ProfileClock::now();
    profile.beginFrameMs = Milliseconds(beginFrameStart, beginFrameEnd);

    const auto playCommandsStart = ProfileClock::now();
    motion::PlayCommands(commands, *canvas->adapter);
    if (!pathOverlayCommands.empty() || !pathEditCommands.empty() || !selectionCommands.empty()) {
        canvas->adapter->restoreCompositionClip();
        if (!pathOverlayCommands.empty()) {
            motion::PlayCommands(pathOverlayCommands, *canvas->adapter);
        }
        if (!pathEditCommands.empty()) {
            motion::PlayCommands(pathEditCommands, *canvas->adapter);
        }
        if (!selectionCommands.empty()) {
            motion::PlayCommands(selectionCommands, *canvas->adapter);
        }
    }
    const auto playCommandsEnd = ProfileClock::now();
    profile.playCommandsMs = Milliseconds(playCommandsStart, playCommandsEnd);

    const auto endFrameStart = ProfileClock::now();
    canvas->adapter->endFrame();
    const auto endFrameEnd = ProfileClock::now();
    profile.endFrameMs = Milliseconds(endFrameStart, endFrameEnd);
    const motion::EndFrameProfile &endFrameProfile = canvas->adapter->endFrameProfile();
    profile.endFrameCanvasRestoreMs = endFrameProfile.canvasRestoreMs;
    profile.endFramePresentMs = endFrameProfile.presentTargetMs;
    profile.endFrameFlushSubmitMs = endFrameProfile.flushSubmitMs;
    profile.endFrameDeviceUnlockMs = endFrameProfile.deviceUnlockMs;
    profile.drewFrame = true;
    profile.totalMs = Milliseconds(totalStart, endFrameEnd);

    if (profileOut != nullptr) {
        *profileOut = profile;
    }
}
