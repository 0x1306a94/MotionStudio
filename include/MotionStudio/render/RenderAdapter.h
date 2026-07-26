#pragma once

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/Paint.h"
#include "MotionStudio/render/PreviewBackdrop.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "MotionStudio/render/StrokeOptions.h"

namespace motion {

// Immediate-mode, stack-based render backend. Implementations map these calls
// onto a concrete 2D API (Metal via tgfx, CoreGraphics, ...). save/restore
// scope matrix, opacity, blend mode and clip state together.
class RenderAdapter {
  public:
    virtual ~RenderAdapter() = default;

    // Begins a frame: resizes/clears the target.
    // width: target width in pixels.
    // height: target height in pixels.
    // backgroundColor: composition background fill color.
    // cornerRadius: composition clip radius in pixels.
    virtual void beginFrame(int width, int height, Color backgroundColor, float cornerRadius) = 0;

    // Ends the frame and flushes pending drawing.
    virtual void endFrame() = 0;

    virtual void save() = 0;
    virtual void restore() = 0;

    // Left-multiplies the current transform.
    // matrix: transform to concatenate.
    virtual void concatTransform(const Mat3 &matrix) = 0;

    // Sets the opacity applied to subsequent draws within the current scope.
    // opacity: opacity in [0, 1].
    virtual void setOpacity(float opacity) = 0;

    // Sets the blend mode for subsequent draws within the current scope.
    // mode: blend mode to apply.
    virtual void setBlendMode(BlendMode mode) = 0;

    // Fills geometry (path / rect / ellipse).
    // geometry: layer-local shape to fill.
    // paint: fill paint (color + fill rule).
    virtual void drawPath(const ShapeGeometry &geometry, const Paint &paint) = 0;

    // Strokes geometry (path / rect / ellipse).
    // geometry: layer-local shape to stroke.
    // paint: stroke paint.
    // options: pen geometry, alignment and trim window.
    virtual void strokePath(const ShapeGeometry &geometry, const Paint &paint,
                            const StrokeOptions &options) = 0;

    // Intersects the current clip with geometry.
    // geometry: clip shape.
    // rule: fill rule used to interpret path geometry.
    virtual void clipPath(const ShapeGeometry &geometry, FillRule rule) = 0;

    // Sets live-preview chrome behind the composition. Default is a no-op;
    // on-screen adapters override this. Offscreen adapters ignore it.
    // backdrop: solid black or transparency checkerboard.
    virtual void setPreviewBackdrop(PreviewBackdrop) {
    }

    // Returns the current live-preview chrome mode. Default is Black.
    virtual PreviewBackdrop previewBackdrop() const {
        return PreviewBackdrop::Black;
    }
};

// Replays a command list against an adapter.
// commands: commands to replay.
// adapter: target render backend.
void PlayCommands(const DrawCommandList &commands, RenderAdapter &adapter);

}  // namespace motion
