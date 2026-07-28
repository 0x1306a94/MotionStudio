#pragma once

#include "MotionStudio/render/EndFrameProfile.h"
#include "MotionStudio/render/RenderAdapter.h"

namespace motion {

// RenderAdapter for a live preview canvas: view pan/zoom, chrome drawn
// outside the composition clip, and optional end-frame profiling.
// Offscreen adapters may leave setViewTransform / sceneUnitsPerViewPoint
// at their defaults.
class PreviewCanvasAdapter : public RenderAdapter {
  public:
    ~PreviewCanvasAdapter() override = default;

    // Sets the user view transform applied on top of the fit-to-target
    // transform every frame. Default is a no-op.
    // zoom: magnification relative to fit (1 = fit to target).
    // panX: horizontal translation in view points.
    // panY: vertical translation in view points.
    virtual void setViewTransform(float /*zoom*/, float /*panX*/, float /*panY*/) {
    }

    // Converts a visual distance in view points into scene units for the
    // current view transform. Default returns 1 (1:1 mapping).
    // sceneWidth / sceneHeight: composition viewport size in scene pixels.
    virtual float sceneUnitsPerViewPoint(int /*sceneWidth*/, int /*sceneHeight*/) const {
        return 1.0f;
    }

    // Restores the canvas to the scene transform without the composition clip.
    // Preview chrome such as selection outlines can then draw outside the
    // composition bounds while still sharing the same scene-to-target mapping.
    virtual void restoreCompositionClip() = 0;

    // Returns timing for the most recent endFrame call.
    virtual const EndFrameProfile &endFrameProfile() const = 0;
};

}  // namespace motion
