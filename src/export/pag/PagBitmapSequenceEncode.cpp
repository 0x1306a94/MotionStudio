#include "PagBitmapSequenceEncode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "PagExportErrorUtil.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Pixmap.h"

namespace motion {
namespace pag_export {
namespace {

struct ImageRect {
    int xPos = 0;
    int yPos = 0;
    int width = 0;
    int height = 0;
};

bool IsKeyFrame(pag::Frame curFrame, pag::Frame lastKeyFrame, int diffSize, int fullSize,
                int keyFrameRate) {
    const pag::Frame frameDistance = curFrame - lastKeyFrame;
    if (curFrame == 0) {
        return true;
    }
    if (diffSize == 0) {
        return false;
    }
    if (diffSize == fullSize) {
        return true;
    }
    if (diffSize > static_cast<int>(fullSize * 0.9) && frameDistance > 5) {
        return true;
    }
    if (diffSize > static_cast<int>(fullSize * 0.75) && keyFrameRate > 20 &&
        frameDistance > (keyFrameRate / 2)) {
        return true;
    }
    if (keyFrameRate > 0 && frameDistance > keyFrameRate) {
        return true;
    }
    return false;
}

void GetImageDiffRect(ImageRect *rect, const uint8_t *preImage, const uint8_t *curImage, int width,
                      int height, int stride) {
    if (rect == nullptr || preImage == nullptr || curImage == nullptr) {
        return;
    }
    int minX = width - 1;
    int minY = height - 1;
    int maxX = 0;
    int maxY = 0;
    for (int y = 0; y < height; ++y) {
        const auto *preData = reinterpret_cast<const uint32_t *>(static_cast<const void *>(preImage + y * stride));
        const auto *curData = reinterpret_cast<const uint32_t *>(static_cast<const void *>(curImage + y * stride));
        for (int x = 0; x < width; ++x) {
            if (preData[x] == curData[x]) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (minX <= maxX) {
        rect->xPos = minX;
        rect->yPos = minY;
        rect->width = maxX - minX + 1;
        rect->height = maxY - minY + 1;
    } else {
        rect->xPos = 0;
        rect->yPos = 0;
        rect->width = 0;
        rect->height = 0;
    }
}

void ClipTransparentEdge(ImageRect *rect, const uint8_t *srcData, int width, int height,
                         int stride) {
    if (rect == nullptr || srcData == nullptr) {
        return;
    }
    int minX = width - 1;
    int minY = height - 1;
    int maxX = 0;
    int maxY = 0;
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = srcData + y * stride;
        for (int x = 0; x < width; ++x) {
            if (row[x * 4 + 3] == 0) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (minX <= maxX) {
        rect->xPos = minX;
        rect->yPos = minY;
        rect->width = maxX - minX + 1;
        rect->height = maxY - minY + 1;
    } else {
        rect->xPos = 0;
        rect->yPos = 0;
        rect->width = 0;
        rect->height = 0;
    }
}

bool InRange(int value, int start, int length) {
    return value >= start && value <= start + length;
}

void ExpandRectRange(ImageRect *dstRect, const ImageRect &srcRect, const ImageRect &lastRect,
                     int width, int height, int expand) {
    if (dstRect == nullptr) {
        return;
    }
    int leftExpand = 0;
    int topExpand = 0;
    int rightExpand = 0;
    int bottomExpand = 0;
    if (InRange(srcRect.xPos, lastRect.xPos, lastRect.width)) {
        leftExpand = std::min(expand, srcRect.xPos);
    }
    if (InRange(srcRect.xPos + srcRect.width, lastRect.xPos, lastRect.width)) {
        rightExpand = std::min(expand, width - (srcRect.xPos + srcRect.width));
    }
    if (InRange(srcRect.yPos, lastRect.yPos, lastRect.height)) {
        topExpand = std::min(expand, srcRect.yPos);
    }
    if (InRange(srcRect.yPos + srcRect.height, lastRect.yPos, lastRect.height)) {
        bottomExpand = std::min(expand, height - (srcRect.yPos + srcRect.height));
    }
    dstRect->xPos = srcRect.xPos - leftExpand;
    dstRect->yPos = srcRect.yPos - topExpand;
    dstRect->width = srcRect.width + leftExpand + rightExpand;
    dstRect->height = srcRect.height + topExpand + bottomExpand;
}

std::unique_ptr<pag::ByteData> EncodeRectWebP(const uint8_t *rgba, int width, int height,
                                              int rowBytes, int quality) {
    if (rgba == nullptr || width <= 0 || height <= 0 || rowBytes <= 0) {
        return nullptr;
    }
    const tgfx::ImageInfo info = tgfx::ImageInfo::Make(width, height, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied, rowBytes);
    const tgfx::Pixmap pixmap(info, rgba);
    const int clampedQuality = quality < 0 ? 0 : (quality > 100 ? 100 : quality);
    std::shared_ptr<tgfx::Data> encoded = tgfx::ImageCodec::Encode(pixmap, tgfx::EncodedFormat::WEBP, clampedQuality);
    if (encoded == nullptr || encoded->size() == 0) {
        return nullptr;
    }
    return pag::ByteData::MakeCopy(encoded->data(), encoded->size());
}

}  // namespace

BitmapSize ComputeBitmapSize(int compositionWidth, int compositionHeight, float bitmapScale,
                             int bitmapMaxResolution) {
    BitmapSize size;
    float factor = bitmapScale;
    if (factor > 0.99f) {
        factor = 1.0f;
    }
    if (bitmapMaxResolution > 0) {
        const int shorterSide = static_cast<int>(std::min(compositionWidth, compositionHeight) * factor);
        if (shorterSide > bitmapMaxResolution) {
            factor *= static_cast<float>(bitmapMaxResolution) / static_cast<float>(shorterSide);
        }
    }
    if (factor > 0.99f) {
        factor = 1.0f;
    }
    size.factor = factor;
    size.width = static_cast<int>(std::ceil(compositionWidth * factor));
    size.height = static_cast<int>(std::ceil(compositionHeight * factor));
    return size;
}

Expected<void, PagExportError> EncodeBitmapSequence(BitmapFrameSource *frameSource,
                                                    pag::BitmapSequence *sequence, FrameTime start,
                                                    FrameTime end, int width, int height,
                                                    int keyFrameInterval, int imageQuality,
                                                    const volatile int *cancelFlag) {
    if (frameSource == nullptr || sequence == nullptr || width <= 0 || height <= 0 || end <= start) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "", "invalid PAG bitmap sequence options"));
    }

    if (IsPagExportCancelled(cancelFlag)) {
        frameSource->finish();
        return Unexpected(MakeCancelledPagExportError());
    }

    const int stride = width * 4;
    const int fullSize = width * height;
    std::vector<uint8_t> previous(static_cast<size_t>(stride) * static_cast<size_t>(height), 0);
    std::vector<uint8_t> current(static_cast<size_t>(stride) * static_cast<size_t>(height), 0);
    ImageRect lastKeyFrameDiffRect{0, 0, width, height};
    pag::Frame lastKeyFrame = 0;
    bool encodeFailed = false;
    bool cancelled = false;

    for (FrameTime time = start; time < end; ++time) {
        if (IsPagExportCancelled(cancelFlag)) {
            cancelled = true;
            break;
        }
        Expected<BitmapFrame, std::string> rendered = frameSource->renderFrame(time);
        if (!rendered.hasValue()) {
            encodeFailed = true;
            break;
        }
        const BitmapFrame &frame = rendered.value();
        if (frame.width != width || frame.height != height || frame.rgba == nullptr) {
            encodeFailed = true;
            break;
        }
        for (int y = 0; y < height; ++y) {
            std::memcpy(current.data() + static_cast<size_t>(y) * stride, frame.rgba + static_cast<size_t>(y) * frame.rowBytes, static_cast<size_t>(stride));
        }

        auto *pagFrame = new pag::BitmapFrame();
        sequence->frames.push_back(pagFrame);

        const pag::Frame localFrame = static_cast<pag::Frame>(time - start);
        ImageRect diffRect{0, 0, width, height};
        if (localFrame == 0) {
            diffRect = {0, 0, width, height};
        } else {
            GetImageDiffRect(&diffRect, previous.data(), current.data(), width, height, stride);
        }

        ImageRect encodeRect{0, 0, width, height};
        const int diffSize = diffRect.width * diffRect.height;
        const bool isKeyFrame = IsKeyFrame(localFrame, lastKeyFrame, diffSize, fullSize, keyFrameInterval);
        if (isKeyFrame) {
            diffRect = {0, 0, width, height};
            lastKeyFrame = localFrame;
            ClipTransparentEdge(&diffRect, current.data(), width, height, stride);
            encodeRect = diffRect;
        } else if (diffRect.width > 0 && diffRect.height > 0) {
            ExpandRectRange(&encodeRect, diffRect, lastKeyFrameDiffRect, width, height, 4);
        } else {
            encodeRect = {0, 0, 0, 0};
        }

        if (diffRect.width > 0 && diffRect.height > 0 && encodeRect.width > 0 && encodeRect.height > 0) {
            const uint8_t *data = current.data() + encodeRect.yPos * stride + encodeRect.xPos * 4;
            std::unique_ptr<pag::ByteData> webp = EncodeRectWebP(data, encodeRect.width, encodeRect.height, stride, imageQuality);
            if (webp == nullptr) {
                encodeFailed = true;
                break;
            }
            auto *bitmapRect = new pag::BitmapRect();
            bitmapRect->x = encodeRect.xPos;
            bitmapRect->y = encodeRect.yPos;
            bitmapRect->fileBytes = webp.release();
            pagFrame->bitmaps.push_back(bitmapRect);
        }
        pagFrame->isKeyframe = isKeyFrame;
        lastKeyFrameDiffRect = diffRect;
        std::swap(current, previous);
    }

    frameSource->finish();
    if (cancelled) {
        return Unexpected(MakeCancelledPagExportError());
    }
    if (encodeFailed || sequence->frames.empty()) {
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "", "PAG bitmap frame encode failed"));
    }
    return Expected<void, PagExportError>();
}

}  // namespace pag_export
}  // namespace motion
