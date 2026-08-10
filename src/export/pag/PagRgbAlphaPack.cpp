#include "PagRgbAlphaPack.h"

#include <cstring>

namespace motion {
namespace pag_export {
namespace {

int EvenAlign(int value) {
    return value + (value & 1);
}

}  // namespace

bool PackRgbAlphaSideBySide(const uint8_t *rgba, int width, int height, size_t rowBytes,
                            std::vector<uint8_t> *outPixels, int *outWidth, int *outHeight) {
    if (rgba == nullptr || outPixels == nullptr || outWidth == nullptr || outHeight == nullptr ||
        width <= 0 || height <= 0 || rowBytes < static_cast<size_t>(width) * 4) {
        return false;
    }

    const int packedWidth = EvenAlign(width * 2);
    const int packedHeight = EvenAlign(height);
    const size_t outRowBytes = static_cast<size_t>(packedWidth) * 4;
    outPixels->assign(outRowBytes * static_cast<size_t>(packedHeight), 0);

    for (int y = 0; y < height; ++y) {
        const uint8_t *srcRow = rgba + static_cast<size_t>(y) * rowBytes;
        uint8_t *dstRow = outPixels->data() + static_cast<size_t>(y) * outRowBytes;
        for (int x = 0; x < width; ++x) {
            const uint8_t *src = srcRow + static_cast<size_t>(x) * 4;
            uint8_t *left = dstRow + static_cast<size_t>(x) * 4;
            left[0] = src[0];
            left[1] = src[1];
            left[2] = src[2];
            left[3] = 255;

            uint8_t *right = dstRow + static_cast<size_t>(width + x) * 4;
            right[0] = src[3];
            right[1] = src[3];
            right[2] = src[3];
            right[3] = 255;
        }
    }

    *outWidth = packedWidth;
    *outHeight = packedHeight;
    return true;
}

}  // namespace pag_export
}  // namespace motion
