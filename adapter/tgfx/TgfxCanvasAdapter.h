#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/render/RenderAdapter.h"

namespace tgfx {
class AutoCanvasRestore;
class Device;
class Surface;
}  // namespace tgfx

namespace motion {

// Shared RenderAdapter implementation backed by a tgfx canvas. Subclasses only
// supply the render target: an offscreen texture (TgfxRenderAdapter) or an
// on-screen window (TgfxOnScreenAdapter). All path/paint/matrix conversions
// and canvas state tracking live here.
class TgfxCanvasAdapter : public RenderAdapter {
  public:
    ~TgfxCanvasAdapter() override;

    // width/height semantics is adapter-defined: the offscreen adapter treats
    // them as the target texture size; the on-screen adapter treats them as
    // the scene viewport size (the drawable size is authoritative and the
    // scene is fit-transformed into it).
    void beginFrame(int width, int height, Color clearColor) final;
    void endFrame() final;

    void save() override;
    void restore() override;
    void concatTransform(const Mat3 &matrix) override;
    void setOpacity(float opacity) override;
    void setBlendMode(BlendMode mode) override;
    void drawPath(const BezierPath &path, const Paint &paint) override;
    void strokePath(const BezierPath &path, const Paint &paint, float width, LineCap cap,
                    LineJoin join) override;
    void clipPath(const BezierPath &path, FillRule rule) override;

  protected:
    TgfxCanvasAdapter();

    // Acquires the render target for a frame and locks the device context.
    // Implementations must populate surface_ and return true on success.
    // width: requested frame width in pixels (offscreen targets only).
    // height: requested frame height in pixels (offscreen targets only).
    virtual bool acquireTarget(int width, int height) = 0;

    // Flushes pending drawing, presents the frame and unlocks the context.
    virtual void presentTarget() = 0;

    // Draws the preview chrome behind the composition (letterbox area). Default
    // is a no-op; the on-screen adapter paints black or a transparency grid.
    virtual void drawPreviewBackdrop();

    // Called after the preview backdrop, before any draw command. Applies any
    // viewport transform then always paints the composition backgroundColor
    // into the scene rect. Subclasses that transform (on-screen fit) must call
    // the base implementation after their transform.
    virtual void onFrameReady(int sceneWidth, int sceneHeight, Color backgroundColor);

    // Blend mode for the composition background rect. Black preview chrome uses
    // Src; the transparency grid needs SrcOver to avoid dark AA fringes.
    virtual bool compositionBackgroundSrcOver() const {
        return false;
    }

    std::shared_ptr<tgfx::Device> device_;
    std::shared_ptr<tgfx::Surface> surface_;

  private:
    float opacity_ = 1;
    BlendMode blendMode_ = BlendMode::Normal;
    std::vector<float> opacityStack_;
    std::vector<BlendMode> blendStack_;
    // Spans beginFrame→endFrame; restores canvas state when the frame ends.
    // Declared after surface_ so destruction still has a live canvas.
    std::unique_ptr<tgfx::AutoCanvasRestore> frameRestore_;
};

}  // namespace motion
