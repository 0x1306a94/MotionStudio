#include "motionstudio_bridge.h"

#include <utility>
#include <vector>

#include "../common/DocumentLock.h"
#include "../common/MSDocument.h"

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/SceneEvaluator.h"

#include "TgfxOnScreenAdapter.h"

using motion::EntityId;

struct MSCanvas {
    std::unique_ptr<motion::TgfxOnScreenAdapter> adapter;
    std::vector<EntityId> selectedLayerIds;
};

namespace {
using ProfileClock = std::chrono::steady_clock;

double Milliseconds(ProfileClock::time_point start, ProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

};  // namespace

MSCanvas *ms_canvas_create(void *mtkView) {
    auto adapter = motion::TgfxOnScreenAdapter::Make(mtkView);
    if (!adapter) {
        return nullptr;
    }
    auto *canvas = new MSCanvas();
    canvas->adapter = std::move(adapter);
    return canvas;
}

void ms_canvas_destroy(MSCanvas *canvas) {
    delete canvas;
}

void ms_canvas_set_preview_backdrop(MSCanvas *canvas, int backdrop) {
    if (canvas == nullptr || canvas->adapter == nullptr) {
        return;
    }
    const auto mode = backdrop == 1 ? motion::PreviewBackdrop::Transparent : motion::PreviewBackdrop::Black;
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

void ms_canvas_draw_frame(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, int64_t frame) {
    ms_canvas_draw_frame_profiled(canvas, document, compositionId, frame, nullptr);
}

void ms_canvas_draw_frame_profiled(MSCanvas *canvas, MSDocument *document, uint64_t compositionId, int64_t frame, MSCanvasFrameProfile *profileOut) {
    ms_canvas_draw_frame_at_time_profiled(canvas, document, compositionId,
                                          static_cast<double>(frame), profileOut);
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
    const motion::SceneState &state = result.value();
    profile.layerCount = state.layers.size();

    const auto buildStart = ProfileClock::now();
    motion::DrawCommandList commands = motion::BuildCommands(state);
    const float viewUnit =
        canvas->adapter->sceneUnitsPerViewPoint(state.viewportWidth, state.viewportHeight);
    const float outlineWidth = 1.5f * viewUnit;
    const float handleSize = 7.0f * viewUnit;
    // Yellow path chrome (distinct from blue selection handles).
    constexpr motion::Color pathOverlayColor{1.0f, 0.85f, 0.2f, 1.0f};
    const std::vector<motion::PathOverlayItem> pathOverlays =
        motion::CollectMaskPathOverlays(state, canvas->selectedLayerIds, pathOverlayColor);
    motion::DrawCommandList pathOverlayCommands =
        motion::BuildPathOverlayCommands(pathOverlays, outlineWidth);
    const motion::EntityId primaryLayerId =
        canvas->selectedLayerIds.empty() ? motion::EntityId{} : canvas->selectedLayerIds.back();
    motion::DrawCommandList selectionCommands = motion::BuildSelectionOutlineCommands(
        state, canvas->selectedLayerIds, primaryLayerId, outlineWidth, handleSize);
    const auto buildEnd = ProfileClock::now();
    profile.buildCommandsMs = Milliseconds(buildStart, buildEnd);
    profile.drawCommandCount =
        commands.size() + pathOverlayCommands.size() + selectionCommands.size();

    const auto beginFrameStart = ProfileClock::now();
    canvas->adapter->beginFrame(state.viewportWidth, state.viewportHeight, state.backgroundColor, state.cornerRadius);
    const auto beginFrameEnd = ProfileClock::now();
    profile.beginFrameMs = Milliseconds(beginFrameStart, beginFrameEnd);

    const auto playCommandsStart = ProfileClock::now();
    motion::PlayCommands(commands, *canvas->adapter);
    if (!pathOverlayCommands.empty() || !selectionCommands.empty()) {
        canvas->adapter->restoreCompositionClip();
        if (!pathOverlayCommands.empty()) {
            motion::PlayCommands(pathOverlayCommands, *canvas->adapter);
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
    const motion::TgfxEndFrameProfile &endFrameProfile = canvas->adapter->endFrameProfile();
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
