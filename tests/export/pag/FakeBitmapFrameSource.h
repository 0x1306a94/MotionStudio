#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
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

    void setFrameColor(FrameTime time, uint8_t red, uint8_t green, uint8_t blue,
                       uint8_t alpha = 255) {
        FrameTint tint;
        tint.red = red;
        tint.green = green;
        tint.blue = blue;
        tint.alpha = alpha;
        frameTints_[time] = tint;
    }

    Expected<void, std::string> prepare(const Document &document, EntityId hostCompositionId,
                                        EntityId rootLayerId, TimeRange visibleRange,
                                        int pixelWidth, int pixelHeight) override {
        (void)rootLayerId;
        return prepareFromSize(document, hostCompositionId, visibleRange, pixelWidth, pixelHeight);
    }

    Expected<void, std::string> prepareComposition(const Document &document, EntityId compositionId,
                                                   TimeRange visibleRange, int pixelWidth,
                                                   int pixelHeight) override {
        return prepareFromSize(document, compositionId, visibleRange, pixelWidth, pixelHeight);
    }

    Expected<BitmapFrame, std::string> renderFrame(FrameTime time) override {
        if (!prepared_) {
            return Unexpected(std::string("not prepared"));
        }
        if (!visibleRange_.contains(time)) {
            return Unexpected(std::string("time outside visible range"));
        }
        uint8_t red = red_;
        uint8_t green = green_;
        uint8_t blue = blue_;
        uint8_t alpha = alpha_;
        auto tintIt = frameTints_.find(time);
        if (tintIt != frameTints_.end()) {
            red = tintIt->second.red;
            green = tintIt->second.green;
            blue = tintIt->second.blue;
            alpha = tintIt->second.alpha;
        }
        const size_t byteCount = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u;
        pixels_.assign(byteCount, 0);
        for (size_t index = 0; index + 3 < byteCount; index += 4) {
            pixels_[index] = red;
            pixels_[index + 1] = green;
            pixels_[index + 2] = blue;
            pixels_[index + 3] = alpha;
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

    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }

  private:
    struct FrameTint {
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        uint8_t alpha = 255;
    };

    Expected<void, std::string> prepareFromSize(const Document &document, EntityId compositionId,
                                                TimeRange visibleRange, int pixelWidth,
                                                int pixelHeight) {
        if (pixelWidth <= 0 || pixelHeight <= 0) {
            return Unexpected(std::string("invalid bitmap size"));
        }
        const Composition *host = nullptr;
        for (const auto &composition : document.compositions) {
            if (composition && composition->id == compositionId) {
                host = composition.get();
                break;
            }
        }
        if (host == nullptr) {
            return Unexpected(std::string("composition not found"));
        }
        (void)host;
        width_ = pixelWidth;
        height_ = pixelHeight;
        visibleRange_ = visibleRange;
        prepared_ = true;
        return Expected<void, std::string>();
    }

    uint8_t red_ = 255;
    uint8_t green_ = 0;
    uint8_t blue_ = 0;
    uint8_t alpha_ = 255;
    int width_ = 0;
    int height_ = 0;
    TimeRange visibleRange_ = {};
    std::vector<uint8_t> pixels_ = {};
    std::unordered_map<int64_t, FrameTint> frameTints_ = {};
    bool prepared_ = false;
};

}  // namespace motion
