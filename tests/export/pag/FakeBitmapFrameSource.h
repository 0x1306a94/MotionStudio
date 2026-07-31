#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "MotionStudio/export/BitmapFrameSource.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

namespace motion {

// Solid-color BitmapFrameSource for PAG export tests.
class FakeBitmapFrameSource : public BitmapFrameSource {
  public:
    explicit FakeBitmapFrameSource(uint8_t red = 255, uint8_t green = 0, uint8_t blue = 0,
                                   uint8_t alpha = 255)
        : red_(red)
        , green_(green)
        , blue_(blue)
        , alpha_(alpha) {
    }

    Expected<void, std::string> prepare(const Document &document, EntityId hostCompositionId,
                                        EntityId rootLayerId, TimeRange visibleRange,
                                        float bitmapScale) override {
        (void)rootLayerId;
        if (bitmapScale <= 0.0f) {
            return Unexpected(std::string("bitmapScale must be > 0"));
        }
        const Composition *host = nullptr;
        for (const auto &composition : document.compositions) {
            if (composition && composition->id == hostCompositionId) {
                host = composition.get();
                break;
            }
        }
        if (host == nullptr) {
            return Unexpected(std::string("host composition not found"));
        }
        width_ = static_cast<int>(
            std::lround(static_cast<double>(host->width) * static_cast<double>(bitmapScale)));
        height_ = static_cast<int>(
            std::lround(static_cast<double>(host->height) * static_cast<double>(bitmapScale)));
        if (width_ <= 0 || height_ <= 0) {
            return Unexpected(std::string("invalid fallback size"));
        }
        visibleRange_ = visibleRange;
        const size_t byteCount = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u;
        pixels_.assign(byteCount, 0);
        for (size_t index = 0; index + 3 < byteCount; index += 4) {
            pixels_[index] = red_;
            pixels_[index + 1] = green_;
            pixels_[index + 2] = blue_;
            pixels_[index + 3] = alpha_;
        }
        prepared_ = true;
        return Expected<void, std::string>();
    }

    Expected<BitmapFrame, std::string> renderFrame(FrameTime time) override {
        if (!prepared_) {
            return Unexpected(std::string("not prepared"));
        }
        if (!visibleRange_.contains(time)) {
            return Unexpected(std::string("time outside visible range"));
        }
        BitmapFrame frame;
        frame.width = width_;
        frame.height = height_;
        frame.rgba = pixels_.data();
        frame.rowBytes = static_cast<size_t>(width_) * 4u;
        frame.premultiplied = true;
        return frame;
    }

    void finish() override {
        prepared_ = false;
        pixels_.clear();
    }

  private:
    uint8_t red_ = 255;
    uint8_t green_ = 0;
    uint8_t blue_ = 0;
    uint8_t alpha_ = 255;
    int width_ = 0;
    int height_ = 0;
    TimeRange visibleRange_ = {};
    std::vector<uint8_t> pixels_ = {};
    bool prepared_ = false;
};

}  // namespace motion
