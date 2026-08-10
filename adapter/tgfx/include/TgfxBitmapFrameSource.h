#pragma once

#include <memory>

#include "MotionStudio/export/BitmapFrameSource.h"

namespace motion {

// Offscreen tgfx BitmapFrameSource for PAG _bmp export.
// Renders via Evaluate → BuildCommands → PlayCommands, then Surface readPixels
// (premultiplied RGBA8). Forces cornerRadius = 0.
// Caller must pass exact pixelWidth/pixelHeight from ComputeBitmapSize.
// The offscreen adapter is reused across prepare/finish when size is unchanged so
// ColorSource shader pipelines stay warm; only pixel size changes recreate it.
class TgfxBitmapFrameSource : public BitmapFrameSource {
  public:
    TgfxBitmapFrameSource();
    ~TgfxBitmapFrameSource() override;

    Expected<void, std::string> prepare(const Document &document, EntityId hostCompositionId,
                                        EntityId rootLayerId, TimeRange visibleRange,
                                        int pixelWidth, int pixelHeight) override;

    Expected<void, std::string> prepareComposition(const Document &document, EntityId compositionId,
                                                   TimeRange visibleRange, int pixelWidth,
                                                   int pixelHeight) override;

    Expected<BitmapFrame, std::string> renderFrame(FrameTime time) override;

    void finish() override;

  private:
    Expected<void, std::string> ensureAdapter(int pixelWidth, int pixelHeight);
    void resetSession();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
