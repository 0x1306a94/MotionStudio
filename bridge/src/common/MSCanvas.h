#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/render/PathEditHandles.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/PreviewCanvasAdapter.h"

struct MSCanvas {
    std::unique_ptr<motion::PreviewCanvasAdapter> adapter;
    std::vector<motion::EntityId> selectedLayerIds;
    bool hasPathEditTarget = false;
    motion::PathEditTarget pathEditTarget;
    int pathEditSelectedVertex = -1;
    std::vector<motion::PathOverlayItem> customPathOverlays;
};
