#include "SvgParse.h"

#include <algorithm>
#include <cmath>

#include "tgfx/core/Data.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/node/SVGNode.h"

namespace motion {
namespace svg {

int ResolveRootSourceSize(const tgfx::SVGRoot &root, float *outWidth, float *outHeight) {
    tgfx::Size viewport = tgfx::Size::Make(100.f, 100.f);
    if (root.getViewBox().has_value()) {
        viewport = root.getViewBox()->size();
    }
    const tgfx::SVGLengthContext lengthContext(viewport);
    const float width =
        lengthContext.resolve(root.getWidth(), tgfx::SVGLengthContext::LengthType::Horizontal);
    const float height =
        lengthContext.resolve(root.getHeight(), tgfx::SVGLengthContext::LengthType::Vertical);
    if (width <= 0.f || height <= 0.f) {
        return 0;
    }
    if (outWidth != nullptr) {
        *outWidth = width;
    }
    if (outHeight != nullptr) {
        *outHeight = height;
    }
    return 1;
}

Expected<ParsedSvg, std::string> ParseSvgBytes(const void *bytes, size_t length) {
    if (length == 0) {
        return Unexpected<std::string>("empty svg");
    }
    auto data = tgfx::Data::MakeWithCopy(bytes, length);
    if (!data) {
        return Unexpected<std::string>("invalid svg");
    }
    auto stream = tgfx::Stream::MakeFromData(data);
    if (!stream) {
        return Unexpected<std::string>("invalid svg");
    }
    auto dom = tgfx::SVGDOM::Make(*stream);
    if (!dom || !dom->getRoot() || dom->getRoot()->tag() != tgfx::SVGTag::Svg) {
        return Unexpected<std::string>("invalid svg");
    }
    float width = 0.f;
    float height = 0.f;
    if (ResolveRootSourceSize(*dom->getRoot(), &width, &height) == 0) {
        return Unexpected<std::string>("invalid svg");
    }
    ParsedSvg parsed = {};
    parsed.dom = std::move(dom);
    parsed.sourceWidth = std::max(1, static_cast<int>(std::lround(width)));
    parsed.sourceHeight = std::max(1, static_cast<int>(std::lround(height)));
    return parsed;
}

}  // namespace svg
}  // namespace motion
