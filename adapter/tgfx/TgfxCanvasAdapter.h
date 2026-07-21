#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/render/RenderAdapter.h"

namespace tgfx {
class Device;
class Surface;
}

namespace motion {

// Shared RenderAdapter implementation backed by a tgfx canvas. Subclasses only
// supply the render target: an offscreen texture (TgfxRenderAdapter) or an
// on-screen window (TgfxOnScreenAdapter). All path/paint/matrix conversions
// and canvas state tracking live here.
class TgfxCanvasAdapter : public RenderAdapter {
  public:
    ~TgfxCanvasAdapter() override;

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

    std::shared_ptr<tgfx::Device> device_;
    std::shared_ptr<tgfx::Surface> surface_;

  private:
    float opacity_ = 1;
    BlendMode blendMode_ = BlendMode::Normal;
    std::vector<float> opacityStack_;
    std::vector<BlendMode> blendStack_;
};

}  // namespace motion
