#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/render/MotionPathChrome.h"
#include "MotionStudio/render/PathEditHandles.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/PreviewCanvasAdapter.h"

struct MSCanvas {
    std::unique_ptr<motion::PreviewCanvasAdapter> adapter;
    std::vector<motion::EntityId> selectedLayerIds;
    bool hasPathEditTarget = false;
    motion::PathEditTarget pathEditTarget;
    int pathEditSelectedVertex = -1;
    // Motion-path chrome: selectedKeyframe is an index into position keyframes
    // of motionPathLayerId (-1 = none). layerId 0 clears selection chrome.
    motion::EntityId motionPathLayerId;
    int motionPathSelectedKeyframe = -1;
    std::vector<motion::PathOverlayItem> customPathOverlays;
    // Mirrors MS_CANVAS_DRAW_MODE (0 = EDIT, 1 = PLAYBACK).
    int drawMode = 0;
    uint64_t contentRevision = 0;
};
