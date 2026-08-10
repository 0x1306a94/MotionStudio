#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace motion {
namespace pag_export {

// Packs RGBA W×H into a side-by-side buffer for PAG VideoSequence alpha strip.
// outWidth = evenAlign(W*2), outHeight = evenAlign(H).
// Left W×H: RGB from src (A ignored for color channels); right W×H: R=G=B=src.A, A=255.
// outRowBytes = outWidth * 4. Caller owns outPixels.
bool PackRgbAlphaSideBySide(const uint8_t *rgba, int width, int height, size_t rowBytes,
                            std::vector<uint8_t> *outPixels, int *outWidth, int *outHeight);

}  // namespace pag_export
}  // namespace motion
