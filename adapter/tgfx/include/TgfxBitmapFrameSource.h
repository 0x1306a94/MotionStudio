#pragma once

#include <memory>

#include "MotionStudio/export/BitmapFrameSource.h"

namespace motion {

// Offscreen tgfx BitmapFrameSource for PAG _bmp export.
// Renders via Evaluate → BuildCommands → PlayCommands, then Surface readPixels
// (premultiplied RGBA8). Forces cornerRadius = 0.
class TgfxBitmapFrameSource : public BitmapFrameSource {
  public:
    TgfxBitmapFrameSource();
    ~TgfxBitmapFrameSource() override;

    Expected<void, std::string> prepare(const Document &document, EntityId hostCompositionId,
                                        EntityId rootLayerId, TimeRange visibleRange,
                                        float bitmapScale) override;

    Expected<void, std::string> prepareComposition(const Document &document, EntityId compositionId,
                                                   TimeRange visibleRange,
                                                   float bitmapScale) override;

    Expected<BitmapFrame, std::string> renderFrame(FrameTime time) override;

    void finish() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
