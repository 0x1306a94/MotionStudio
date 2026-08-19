#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "MotionStudio/common/Expected.h"
#include "tgfx/svg/SVGDOM.h"

namespace motion {
namespace svg {

struct ParsedSvg {
    std::shared_ptr<tgfx::SVGDOM> dom;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

Expected<ParsedSvg, std::string> ParseSvgBytes(const void *bytes, size_t length);
int ResolveRootSourceSize(const tgfx::SVGRoot &root, float *outWidth, float *outHeight);

}  // namespace svg
}  // namespace motion
