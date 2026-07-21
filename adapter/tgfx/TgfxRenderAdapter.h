#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "MotionStudio/render/RenderAdapter.h"

namespace motion {

// RenderAdapter backed by tgfx on the Metal GPU backend, rendering into an
// offscreen texture. Pixels can be read back for snapshot testing or export.
class TgfxRenderAdapter : public RenderAdapter {
  public:
    // Creates an offscreen adapter with the given initial size.
    // width: initial framebuffer width in pixels.
    // height: initial framebuffer height in pixels.
    // Returns nullptr when Metal is unavailable on this machine.
    static std::unique_ptr<TgfxRenderAdapter> Make(int width, int height);

    ~TgfxRenderAdapter() override;

    void beginFrame(int width, int height, Color clearColor) override;
    void endFrame() override;
    void save() override;
    void restore() override;
    void concatTransform(const Mat3 &matrix) override;
    void setOpacity(float opacity) override;
    void setBlendMode(BlendMode mode) override;
    void drawPath(const BezierPath &path, const Paint &paint) override;
    void strokePath(const BezierPath &path, const Paint &paint, float width, LineCap cap,
                    LineJoin join) override;
    void clipPath(const BezierPath &path, FillRule rule) override;

    // Reads back the current framebuffer as RGBA8 premultiplied pixels in
    // top-left origin. Call after endFrame.
    // pixels: output buffer, resized to width * height * 4.
    bool ReadPixels(std::vector<uint8_t> &pixels);

    int width() const;
    int height() const;

  private:
    TgfxRenderAdapter();
    bool RecreateTarget(int width, int height);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
