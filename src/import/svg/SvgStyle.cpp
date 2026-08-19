#include "SvgStyle.h"

#include <memory>

#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/StrokePosition.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/SVGTypes.h"

namespace motion {
namespace svg {

namespace {

Color ToMotionColor(const tgfx::Color &color) {
    return {color.red, color.green, color.blue, color.alpha};
}

Color ColorFromPaint(const tgfx::SVGPaint &paint) {
    if (paint.color().type() == tgfx::SVGColor::Type::Color) {
        return ToMotionColor(paint.color().color());
    }
    return {0.f, 0.f, 0.f, 1.f};
}

}  // namespace

ComputedStyle ResolveNodeStyle(const tgfx::SVGNode &node,
                               const tgfx::SVGLengthContext &lengthContext) {
    ComputedStyle style = {};
    if (node.tag() == tgfx::SVGTag::Line || node.tag() == tgfx::SVGTag::Polyline) {
        style.hasFill = false;
    }
    const auto &fill = node.getFill();
    if (fill.isValue()) {
        if (fill->type() == tgfx::SVGPaint::Type::None) {
            style.hasFill = false;
        } else if (fill->type() == tgfx::SVGPaint::Type::Color) {
            style.hasFill = true;
            style.fillColor = ColorFromPaint(*fill);
        }
    }
    const auto &stroke = node.getStroke();
    if (stroke.isValue()) {
        if (stroke->type() == tgfx::SVGPaint::Type::None) {
            style.hasStroke = false;
        } else if (stroke->type() == tgfx::SVGPaint::Type::Color) {
            style.hasStroke = true;
            style.strokeColor = ColorFromPaint(*stroke);
        }
    }
    const auto &strokeWidth = node.getStrokeWidth();
    if (strokeWidth.isValue()) {
        style.strokeWidth =
            lengthContext.resolve(*strokeWidth, tgfx::SVGLengthContext::LengthType::Other);
    }
    return style;
}

void ApplyStyles(Layer &layer, const ComputedStyle &style) {
    if (style.hasFill) {
        auto fill = std::make_unique<FillStyle>();
        fill->color.setStaticValue(style.fillColor);
        layer.styles.push_back(std::move(fill));
    }
    if (style.hasStroke) {
        auto stroke = std::make_unique<StrokeStyle>();
        stroke->color.setStaticValue(style.strokeColor);
        stroke->width.setStaticValue(style.strokeWidth);
        stroke->position = StrokePosition::Center;
        layer.styles.push_back(std::move(stroke));
    }
}

}  // namespace svg
}  // namespace motion
