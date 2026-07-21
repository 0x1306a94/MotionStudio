#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/Paint.h"

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
    // clearColor: background fill color.
    virtual void beginFrame(int width, int height, Color clearColor) = 0;

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

    // Fills a path.
    // path: path to fill.
    // paint: fill paint (color + fill rule).
    virtual void drawPath(const BezierPath &path, const Paint &paint) = 0;

    // Strokes a path.
    // path: path to stroke.
    // paint: stroke paint.
    // width: stroke width.
    // cap: line cap style.
    // join: line join style.
    virtual void strokePath(const BezierPath &path, const Paint &paint, float width,
                            LineCap cap, LineJoin join) = 0;

    // Intersects the current clip with a path.
    // path: clip path.
    // rule: fill rule used to interpret the path.
    virtual void clipPath(const BezierPath &path, FillRule rule) = 0;
};

// Replays a command list against an adapter.
// commands: commands to replay.
// adapter: target render backend.
void PlayCommands(const DrawCommandList &commands, RenderAdapter &adapter);

}  // namespace motion
