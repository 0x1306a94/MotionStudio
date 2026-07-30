#pragma once

#include <optional>
#include <string>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/EvaluatedTextItem.h"
#include "MotionStudio/render/MaskApplyMode.h"
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

    // Begins an offscreen layer for content that will be masked.
    virtual void beginLayer() = 0;

    // Ends the offscreen layer and composites it with accumulated coverage.
    virtual void endLayer() = 0;

    // Begins recording coverage for the current offscreen layer.
    // mode: how EndMask should interpret the recorded coverage.
    virtual void beginMask(MaskApplyMode mode) = 0;

    // Ends coverage recording and applies it to the current offscreen layer.
    virtual void endMask() = 0;

    // Adds one path mask contribution while inside BeginMask(PathCoverage).
    // geometry: mask path in the current transform space.
    // mode / opacity / inverted / feather / expansion: AE mask parameters.
    virtual void drawMaskPath(const ShapeGeometry &geometry, MaskMode mode, float opacity,
                              bool inverted, float feather, float expansion) = 0;

    // Draws an image into the layer-local container rectangle [0,0]–container.
    // path: absolute filesystem path to the image file.
    // containerSize: layer-local container size in pixels.
    // intrinsicSize: source image pixel size.
    // mode: how pixels map into the container (PAGScaleMode semantics).
    virtual void drawImage(const std::string &path, Vec2 containerSize, Vec2 intrinsicSize,
                           ImageScaleMode mode) = 0;

    // Draws boxed text in the layer-local container [0,0]–containerSize.
    // text: UTF-8 string (may be empty).
    // fontSize: model size cap (shrink applies when boxTextMode is true).
    // containerSize: fixed text box size.
    // boxTextMode: true = wrap + shrink to fit; false = wrap + clip overflow.
    // align: horizontal alignment within the box.
    // fontFamily: system font family name.
    // fontStyle: system style name within the family; empty = default/Regular.
    // styles: fill/stroke passes in order; empty draws a black fill.
    virtual void drawText(const std::string &text, float fontSize, Vec2 containerSize,
                          bool boxTextMode, TextAlign align, const std::string &fontFamily,
                          const std::string &fontStyle, const std::vector<TextDrawStyle> &styles) = 0;

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
