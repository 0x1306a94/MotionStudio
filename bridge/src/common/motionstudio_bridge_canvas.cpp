#include "motionstudio_bridge.h"

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "FrameCommandCache.h"
#include "MSCanvas.h"
#include "MSDocument.h"

#include <cmath>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/MotionPathChrome.h"
#include "MotionStudio/render/PathEditHandles.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/undo/SetSpatialTangentsCommand.h"

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

MS_MOTION_PATH_HANDLE ToMSMotionPathHandle(motion::MotionPathHandleKind kind) {
    switch (kind) {
        case motion::MotionPathHandleKind::None: {
            return MS_MOTION_PATH_HANDLE_NONE;
        }
        case motion::MotionPathHandleKind::Keyframe: {
            return MS_MOTION_PATH_HANDLE_KEYFRAME;
        }
        case motion::MotionPathHandleKind::InTangent: {
            return MS_MOTION_PATH_HANDLE_IN_TANGENT;
        }
        case motion::MotionPathHandleKind::OutTangent: {
            return MS_MOTION_PATH_HANDLE_OUT_TANGENT;
        }
    }
    return MS_MOTION_PATH_HANDLE_NONE;
}

}  // namespace

void ms_canvas_destroy(MSCanvas *canvas) {
    delete canvas;
}

void ms_canvas_set_draw_mode(MSCanvas *canvas, MS_CANVAS_DRAW_MODE mode) {
    if (canvas == nullptr) {
        return;
    }
    canvas->drawMode = static_cast<int>(mode);
}

MS_CANVAS_DRAW_MODE ms_canvas_get_draw_mode(const MSCanvas *canvas) {
    if (canvas == nullptr) {
        return MS_CANVAS_DRAW_MODE_EDIT;
    }
    return static_cast<MS_CANVAS_DRAW_MODE>(canvas->drawMode);
}

void ms_canvas_set_content_revision(MSCanvas *canvas, uint64_t revision) {
    if (canvas == nullptr) {
        return;
    }
    canvas->contentRevision = revision;
}

uint64_t ms_canvas_get_content_revision(const MSCanvas *canvas) {
    if (canvas == nullptr) {
        return 0;
    }
    return canvas->contentRevision;
}

void ms_canvas_set_preview_backdrop(MSCanvas *canvas, MS_PREVIEWER_BACKDROP backdrop) {
    if (canvas == nullptr || canvas->adapter == nullptr) {
        return;
    }
    const auto mode = backdrop == MS_PREVIEWER_BACKDROP_TRANSPARENT ? motion::PreviewBackdrop::Transparent : motion::PreviewBackdrop::Black;
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

void ms_canvas_set_selection_show_anchor(MSCanvas *canvas, bool showAnchor) {
    if (canvas == nullptr) {
        return;
    }
    canvas->showSelectionAnchor = showAnchor;
}

void ms_canvas_set_path_edit_target(MSCanvas *canvas, MS_PATH_EDIT kind, uint64_t layerId, int maskIndex, int selectedVertex) {
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
    canvas->pathEditTarget.kind = kind == MS_PATH_EDIT_MASK ? motion::PathEditKind::Mask : motion::PathEditKind::Shape;
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

MSPathEditHit ms_canvas_hit_path_edit(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, double frameTime, float sceneX, float sceneY) {
    MSPathEditHit hit{};
    hit.kind = MS_PATH_HANDLE_NONE;
    if (canvas == nullptr || canvas->adapter == nullptr || document == nullptr ||
        !canvas->hasPathEditTarget) {
        return hit;
    }
    DocumentLock guard(document);
    auto result = motion::SceneEvaluator::EvaluatePreview(*document->document, EntityId{compositionId}, motion::PreviewTime(frameTime));
    if (!result.hasValue()) {
        return hit;
    }
    motion::SceneState &state = result.value();
    motion::PathEditHandles handles;
    if (!motion::BuildPathEditHandles(state, canvas->pathEditTarget, canvas->pathEditSelectedVertex, handles)) {
        return hit;
    }
    const float viewUnit = canvas->adapter->sceneUnitsPerViewPoint(state.viewportWidth, state.viewportHeight);
    // Vertex chrome is ~7pt; keep hit close so mid-edge insert still works.
    const float handleRadius = 8.0f * viewUnit;
    const float segmentRadius = 6.0f * viewUnit;
    const motion::PathEditHit coreHit = motion::HitTestPathEdit(handles, {sceneX, sceneY}, handleRadius, segmentRadius);
    hit.kind = ToMSPathHandle(coreHit.kind);
    hit.index = coreHit.index;
    hit.segmentT = coreHit.segmentT;
    return hit;
}

void ms_canvas_set_motion_path_selection(MSCanvas *canvas, uint64_t layerId, int selectedKeyframe) {
    if (canvas == nullptr) {
        return;
    }
    if (layerId == 0) {
        canvas->motionPathLayerId = {};
        canvas->motionPathSelectedKeyframe = -1;
        return;
    }
    canvas->motionPathLayerId = EntityId{layerId};
    canvas->motionPathSelectedKeyframe = selectedKeyframe;
}

MSMotionPathHit ms_canvas_hit_motion_path(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, double frameTime, float sceneX, float sceneY) {
    MSMotionPathHit hit{};
    hit.kind = MS_MOTION_PATH_HANDLE_NONE;
    if (canvas == nullptr || canvas->adapter == nullptr || document == nullptr ||
        canvas->hasPathEditTarget || canvas->selectedLayerIds.empty()) {
        return hit;
    }
    DocumentLock guard(document);
    auto result = motion::SceneEvaluator::EvaluatePreview(*document->document, EntityId{compositionId}, motion::PreviewTime(frameTime));
    if (!result.hasValue()) {
        return hit;
    }
    motion::SceneState &state = result.value();
    const float viewUnit = canvas->adapter->sceneUnitsPerViewPoint(state.viewportWidth, state.viewportHeight);
    const float handleRadius = 8.0f * viewUnit;

    // Prefer the layer with an active keyframe selection, then walk selected layers.
    std::vector<EntityId> order;
    order.reserve(canvas->selectedLayerIds.size() + 1);
    if (canvas->motionPathLayerId.isValid()) {
        order.push_back(canvas->motionPathLayerId);
    }
    for (EntityId id : canvas->selectedLayerIds) {
        if (id != canvas->motionPathLayerId) {
            order.push_back(id);
        }
    }

    for (EntityId layerId : order) {
        const int selectedKeyframe = layerId == canvas->motionPathLayerId ? canvas->motionPathSelectedKeyframe : -1;
        motion::MotionPathChrome chrome;
        if (!motion::BuildMotionPathChrome(*document->document, layerId, motion::PreviewTime(frameTime), selectedKeyframe, chrome)) {
            continue;
        }
        const motion::MotionPathHit coreHit = motion::HitTestMotionPath(chrome, {sceneX, sceneY}, handleRadius);
        if (coreHit.kind == motion::MotionPathHandleKind::None) {
            continue;
        }
        hit.kind = ToMSMotionPathHandle(coreHit.kind);
        hit.layerId = layerId.value;
        hit.index = coreHit.index;
        canvas->motionPathLayerId = layerId;
        return hit;
    }
    return hit;
}

void ms_command_motion_path_drag_tangent(MSDocument *document, uint64_t layerId, int keyframeIndex, bool isOut, float sceneX, float sceneY, double frameTime) {
    if (document == nullptr || layerId == 0 || keyframeIndex < 0) {
        return;
    }
    DocumentLock guard(document);
    motion::MotionPathChrome chrome;
    if (!motion::BuildMotionPathChrome(*document->document, EntityId{layerId}, motion::PreviewTime(frameTime), keyframeIndex, chrome)) {
        return;
    }
    const auto updates = motion::MotionPathTangentDragUpdates(*document->document, EntityId{layerId}, static_cast<size_t>(keyframeIndex), isOut, {sceneX, sceneY}, chrome.parentWorldTransform);
    for (const motion::MotionPathSpatialUpdate &update : updates) {
        bridge::Execute(document, std::make_unique<motion::SetSpatialTangentsCommand>(motion::PropertyPath{EntityId{layerId}, "transform.position"}, update.time, update.spatialIn, update.spatialOut));
    }
}

void ms_canvas_draw_frame(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, int64_t frame) {
    ms_canvas_draw_frame_profiled(canvas, document, compositionId, frame, nullptr);
}

void ms_canvas_draw_frame_profiled(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, int64_t frame, MSCanvasFrameProfile *profileOut) {
    ms_canvas_draw_frame_at_time_profiled(canvas, document, compositionId, static_cast<double>(frame), profileOut);
}

void ms_canvas_draw_frame_at_time_profiled(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, double frameTime, MSCanvasFrameProfile *profileOut) {
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

    const bool playbackMode = canvas->drawMode == static_cast<int>(MS_CANVAS_DRAW_MODE_PLAYBACK);
    const int64_t cacheFrame = static_cast<int64_t>(std::floor(frameTime));
    motion::DrawCommandList commands;
    motion::DrawCommandList pathOverlayCommands;
    motion::DrawCommandList pathEditCommands;
    motion::DrawCommandList motionPathCommands;
    motion::DrawCommandList selectionCommands;
    int viewportWidth = 0;
    int viewportHeight = 0;
    motion::Color backgroundColor{};
    float cornerRadius = 0.0f;
    bool usedFrameCache = false;

    if (playbackMode) {
        canvas->frameCommandCache.invalidateIfStale(compositionId, canvas->contentRevision);
        if (const motionstudio::FrameCommandCache::Entry *cached = canvas->frameCommandCache.find(cacheFrame)) {
            commands = cached->commands;
            viewportWidth = cached->viewportWidth;
            viewportHeight = cached->viewportHeight;
            backgroundColor = cached->backgroundColor;
            cornerRadius = cached->cornerRadius;
            profile.layerCount = cached->layerCount;
            profile.sceneEvaluateMs = 0.0;
            profile.buildCommandsMs = 0.0;
            profile.drawCommandCount = commands.size();
            profile.usedFrameCache = true;
            usedFrameCache = true;
        }
    }

    if (!usedFrameCache) {
        const auto evaluateStart = ProfileClock::now();
        auto result = motion::SceneEvaluator::EvaluatePreview(*document->document, EntityId{compositionId}, motion::PreviewTime(frameTime));
        const auto evaluateEnd = ProfileClock::now();
        profile.sceneEvaluateMs = Milliseconds(evaluateStart, evaluateEnd);
        if (!result.hasValue()) {
            profile.totalMs = Milliseconds(totalStart, ProfileClock::now());
            if (profileOut != nullptr) {
                *profileOut = profile;
            }
            return;
        }
        motion::SceneState &state = result.value();
        profile.layerCount = state.layers.size();
        viewportWidth = state.viewportWidth;
        viewportHeight = state.viewportHeight;
        backgroundColor = state.backgroundColor;
        cornerRadius = state.cornerRadius;

        const auto buildStart = ProfileClock::now();
        commands = motion::BuildCommands(state);
        const bool buildEditorChrome = !playbackMode;
        if (buildEditorChrome) {
            const float viewUnit = canvas->adapter->sceneUnitsPerViewPoint(state.viewportWidth, state.viewportHeight);
            const float outlineWidth = 1.5f * viewUnit;
            const float handleSize = 7.0f * viewUnit;
            constexpr motion::Color pathOverlayColor{1.0f, 0.85f, 0.2f, 1.0f};
            std::vector<motion::PathOverlayItem> pathOverlays = motion::CollectMaskPathOverlays(state, canvas->selectedLayerIds, pathOverlayColor);
            pathOverlays.insert(pathOverlays.end(), canvas->customPathOverlays.begin(), canvas->customPathOverlays.end());
            pathOverlayCommands = motion::BuildPathOverlayCommands(pathOverlays, outlineWidth);

            if (canvas->hasPathEditTarget) {
                motion::PathEditHandles handles;
                if (motion::BuildPathEditHandles(state, canvas->pathEditTarget, canvas->pathEditSelectedVertex, handles)) {
                    pathEditCommands = motion::BuildPathEditCommands(handles, outlineWidth, handleSize);
                }
            }

            if (!canvas->hasPathEditTarget) {
                for (EntityId layerId : canvas->selectedLayerIds) {
                    const int selectedKeyframe = layerId == canvas->motionPathLayerId ? canvas->motionPathSelectedKeyframe : -1;
                    motion::MotionPathChrome chrome;
                    if (!motion::BuildMotionPathChrome(*document->document, layerId, motion::PreviewTime(frameTime), selectedKeyframe, chrome)) {
                        continue;
                    }
                    motion::DrawCommandList layerCommands = motion::BuildMotionPathCommands(chrome, outlineWidth, handleSize);
                    motionPathCommands.insert(motionPathCommands.end(), layerCommands.begin(), layerCommands.end());
                }
            }

            if (!canvas->hasPathEditTarget) {
                const motion::EntityId primaryLayerId = canvas->selectedLayerIds.empty() ? motion::EntityId{} : canvas->selectedLayerIds.back();
                selectionCommands = motion::BuildSelectionOutlineCommands(state, canvas->selectedLayerIds,
                                                                          primaryLayerId, outlineWidth, handleSize,
                                                                          canvas->showSelectionAnchor);
            }
        }
        const auto buildEnd = ProfileClock::now();
        profile.buildCommandsMs = Milliseconds(buildStart, buildEnd);
        profile.drawCommandCount = commands.size() + pathOverlayCommands.size() + pathEditCommands.size() + motionPathCommands.size() + selectionCommands.size();

        if (playbackMode) {
            motionstudio::FrameCommandCache::Entry entry;
            entry.viewportWidth = viewportWidth;
            entry.viewportHeight = viewportHeight;
            entry.backgroundColor = backgroundColor;
            entry.cornerRadius = cornerRadius;
            entry.layerCount = profile.layerCount;
            entry.commands = commands;
            canvas->frameCommandCache.put(cacheFrame, std::move(entry));
        }
    }

    const auto beginFrameStart = ProfileClock::now();
    canvas->adapter->beginFrame(viewportWidth, viewportHeight, backgroundColor, cornerRadius);
    const auto beginFrameEnd = ProfileClock::now();
    profile.beginFrameMs = Milliseconds(beginFrameStart, beginFrameEnd);

    const auto playCommandsStart = ProfileClock::now();
    motion::PlayCommands(commands, *canvas->adapter);
    if (!pathOverlayCommands.empty() || !pathEditCommands.empty() || !motionPathCommands.empty() || !selectionCommands.empty()) {
        canvas->adapter->restoreCompositionClip();
        if (!pathOverlayCommands.empty()) {
            motion::PlayCommands(pathOverlayCommands, *canvas->adapter);
        }
        if (!motionPathCommands.empty()) {
            motion::PlayCommands(motionPathCommands, *canvas->adapter);
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
