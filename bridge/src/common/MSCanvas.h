#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "FrameCommandCache.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/render/GradientEditHandles.h"
#include "MotionStudio/render/MotionPathChrome.h"
#include "MotionStudio/render/PathEditHandles.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/PreviewCanvasAdapter.h"

struct MSCanvas {
    std::unique_ptr<motion::PreviewCanvasAdapter> adapter;
    std::vector<motion::EntityId> selectedLayerIds;
    // When false, selection chrome omits the anchor crosshair (App container mode).
    bool showSelectionAnchor = true;
    // When false, selection chrome omits corner/edge resize knobs (point text).
    bool showSelectionScaleHandles = true;
    bool hasPathEditTarget = false;
    motion::PathEditTarget pathEditTarget;
    int pathEditSelectedVertex = -1;
    // Motion-path chrome: selectedKeyframe is an index into position keyframes
    // of motionPathLayerId (-1 = none). layerId 0 clears selection chrome.
    motion::EntityId motionPathLayerId;
    int motionPathSelectedKeyframe = -1;
    bool hasGradientEditTarget = false;
    motion::GradientEditTarget gradientEditTarget;
    std::vector<motion::PathOverlayItem> customPathOverlays;
    // Local post-multiply preview matrices for live ShapePath resize.
    // effectiveWorld = layer.worldTransform * L. Not part of Document.
    std::unordered_map<motion::EntityId, motion::Mat3> previewTransforms;
    // Mirrors MS_CANVAS_DRAW_MODE (0 = EDIT, 1 = PLAYBACK).
    int drawMode = 0;
    motionstudio::FrameCommandCache frameCommandCache = {};
};
