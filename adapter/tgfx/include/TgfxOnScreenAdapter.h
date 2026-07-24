#pragma once

#include <memory>

#include "TgfxCanvasAdapter.h"

namespace motion {

// RenderAdapter backed by tgfx on the Metal GPU backend, rendering directly
// into an MTKView's drawable. The window surface is reused across frames and
// recreated only when the drawable size changes; endFrame presents the
// current drawable. Intended for the editor's live preview canvas.
class TgfxOnScreenAdapter : public TgfxCanvasAdapter {
  public:
    // Creates an on-screen adapter for the given view.
    // mtkView: an MTKView instance passed as void* across the C ABI boundary.
    // Returns nullptr when the view is null or Metal is unavailable.
    static std::unique_ptr<TgfxOnScreenAdapter> Make(void *mtkView);

    ~TgfxOnScreenAdapter() override;

    void setPreviewBackdrop(PreviewBackdrop backdrop) override;
    PreviewBackdrop previewBackdrop() const override;

  protected:
    bool acquireTarget(int width, int height) override;
    void presentTarget() override;
    void drawPreviewBackdrop() override;
    // Fits the scene into the drawable (AE Fit Up to 100%: letterboxed,
    // centered, never scaled above 1:1), then paints composition background.
    void onFrameReady(int sceneWidth, int sceneHeight, Color backgroundColor, float cornerRadius) override;
    bool compositionBackgroundSrcOver() const override;

  private:
    TgfxOnScreenAdapter();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    PreviewBackdrop previewBackdrop_ = PreviewBackdrop::Transparent;
};

}  // namespace motion
