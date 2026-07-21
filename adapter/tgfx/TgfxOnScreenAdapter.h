#pragma once

#include <memory>

#include "TgfxCanvasAdapter.h"

namespace motion {

// RenderAdapter backed by tgfx on the Metal GPU backend, rendering directly
// into an MTKView's drawable. Each frame acquires the window surface and
// presents on endFrame. Intended for the editor's live preview canvas.
class TgfxOnScreenAdapter : public TgfxCanvasAdapter {
  public:
    // Creates an on-screen adapter for the given view.
    // mtkView: an MTKView instance passed as void* across the C ABI boundary.
    // Returns nullptr when the view is null or Metal is unavailable.
    static std::unique_ptr<TgfxOnScreenAdapter> Make(void *mtkView);

    ~TgfxOnScreenAdapter() override;

  protected:
    bool acquireTarget(int width, int height) override;
    void presentTarget() override;
    // Fits the scene viewport into the drawable (letterboxed, centered).
    void onFrameReady(int sceneWidth, int sceneHeight) override;

  private:
    TgfxOnScreenAdapter();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
