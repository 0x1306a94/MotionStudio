#include "SvgParse.h"

#include <algorithm>
#include <cmath>

#include "tgfx/core/Data.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/svg/SVGCustomParser.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGGradient.h"
#include "tgfx/svg/node/SVGImage.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGUse.h"

namespace motion {
namespace svg {

namespace {

tgfx::SVGIRI ParseHrefValue(const std::string &value) {
    if (!value.empty() && value[0] == '#') {
        return tgfx::SVGIRI(tgfx::SVGIRI::Type::Local, value.substr(1));
    }
    if (value.size() >= 5 && value.compare(0, 5, "data:") == 0) {
        return tgfx::SVGIRI(tgfx::SVGIRI::Type::DataURI, value);
    }
    return tgfx::SVGIRI(tgfx::SVGIRI::Type::Nonlocal, value);
}

class HrefAliasParser : public tgfx::SVGCustomParser {
  public:
    void handleCustomAttribute(tgfx::SVGNode &node, const std::string &name,
                               const std::string &value) override {
        if (name != "href") {
            node.addCustomAttribute(name, value);
            return;
        }
        const tgfx::SVGIRI iri = ParseHrefValue(value);
        switch (node.tag()) {
            case tgfx::SVGTag::Use:
                static_cast<tgfx::SVGUse &>(node).setHref(iri);
                break;
            case tgfx::SVGTag::Image:
                static_cast<tgfx::SVGImage &>(node).setHref(iri);
                break;
            case tgfx::SVGTag::LinearGradient:
            case tgfx::SVGTag::RadialGradient:
                static_cast<tgfx::SVGGradient &>(node).setHref(iri);
                break;
            default:
                node.addCustomAttribute(name, value);
                break;
        }
    }
};

}  // namespace

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
    auto hrefParser = std::make_shared<HrefAliasParser>();
    auto dom = tgfx::SVGDOM::Make(*stream, nullptr, hrefParser);
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
