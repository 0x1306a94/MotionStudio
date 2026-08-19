#include "SvgStyle.h"

#include <memory>

#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/StrokePosition.h"
#include "tgfx/svg/SVGTypes.h"

namespace motion {
namespace svg {

namespace {

Color ToMotionColor(const tgfx::Color &color) {
    return {color.red, color.green, color.blue, color.alpha};
}

Color ResolvePaintColor(const tgfx::SVGPaint &paint, const Color &currentColor) {
    if (paint.color().type() == tgfx::SVGColor::Type::CurrentColor) {
        return currentColor;
    }
    return ToMotionColor(paint.color().color());
}

Color WithOpacity(Color color, float opacity) {
    color.a *= opacity;
    return color;
}

}  // namespace

ComputedStyle ResolveStyle(const tgfx::SVGNode &node, const ComputedStyle &parent,
                           const tgfx::SVGLengthContext &lengthContext) {
    ComputedStyle style = parent;
    style.displayNone = false;

    const auto &color = node.getColor();
    if (color.isValue()) {
        style.currentColor = ToMotionColor(*color);
    }

    const auto &fill = node.getFill();
    if (fill.isValue()) {
        if (fill->type() == tgfx::SVGPaint::Type::None) {
            style.hasFill = false;
        } else if (fill->type() == tgfx::SVGPaint::Type::Color) {
            style.hasFill = true;
            style.fillColor = ResolvePaintColor(*fill, style.currentColor);
        }
    }

    const auto &fillOpacity = node.getFillOpacity();
    if (fillOpacity.isValue()) {
        style.fillOpacity = *fillOpacity;
    }

    const auto &fillRule = node.getFillRule();
    if (fillRule.isValue()) {
        style.fillRule = fillRule->type() == tgfx::SVGFillRule::Type::EvenOdd ? FillRule::EvenOdd
                                                                              : FillRule::NonZero;
    }

    const auto &stroke = node.getStroke();
    if (stroke.isValue()) {
        if (stroke->type() == tgfx::SVGPaint::Type::None) {
            style.hasStroke = false;
        } else if (stroke->type() == tgfx::SVGPaint::Type::Color) {
            style.hasStroke = true;
            style.strokeColor = ResolvePaintColor(*stroke, style.currentColor);
        }
    }

    const auto &strokeOpacity = node.getStrokeOpacity();
    if (strokeOpacity.isValue()) {
        style.strokeOpacity = *strokeOpacity;
    }

    const auto &strokeWidth = node.getStrokeWidth();
    if (strokeWidth.isValue()) {
        style.strokeWidth =
            lengthContext.resolve(*strokeWidth, tgfx::SVGLengthContext::LengthType::Other);
    }

    const auto &lineCap = node.getStrokeLineCap();
    if (lineCap.isValue()) {
        switch (*lineCap) {
            case tgfx::SVGLineCap::Round:
                style.cap = LineCap::Round;
                break;
            case tgfx::SVGLineCap::Square:
                style.cap = LineCap::Square;
                break;
            case tgfx::SVGLineCap::Butt:
                style.cap = LineCap::Butt;
                break;
        }
    }

    const auto &lineJoin = node.getStrokeLineJoin();
    if (lineJoin.isValue()) {
        switch (lineJoin->type()) {
            case tgfx::SVGLineJoin::Type::Round:
                style.join = LineJoin::Round;
                break;
            case tgfx::SVGLineJoin::Type::Bevel:
                style.join = LineJoin::Bevel;
                break;
            case tgfx::SVGLineJoin::Type::Miter:
            case tgfx::SVGLineJoin::Type::Inherit:
                style.join = LineJoin::Miter;
                break;
        }
    }

    const auto &miterLimit = node.getStrokeMiterLimit();
    if (miterLimit.isValue()) {
        style.miterLimit = *miterLimit;
    }

    const auto &dash = node.getStrokeDashArray();
    if (dash.isValue()) {
        style.hasDash = dash->type() == tgfx::SVGDashArray::Type::DashArray &&
            !dash->dashArray().empty();
    }

    const auto &display = node.getDisplay();
    if (display.isValue() && *display == tgfx::SVGDisplay::None) {
        style.displayNone = true;
    }

    const auto &visibility = node.getVisibility();
    if (visibility.isValue()) {
        style.visible = visibility->type() == tgfx::SVGVisibility::Type::Visible;
    }

    const auto &fontFamily = node.getFontFamily();
    if (fontFamily.isValue() && fontFamily->type() == tgfx::SVGFontFamily::Type::Family) {
        style.fontFamily = fontFamily->family();
    }

    const auto &fontSize = node.getFontSize();
    if (fontSize.isValue() && fontSize->type() == tgfx::SVGFontSize::Type::Length) {
        style.fontSize =
            lengthContext.resolve(fontSize->size(), tgfx::SVGLengthContext::LengthType::Other);
    }

    const auto &fontStyle = node.getFontStyle();
    if (fontStyle.isValue()) {
        switch (fontStyle->type()) {
            case tgfx::SVGFontStyle::Type::Italic:
            case tgfx::SVGFontStyle::Type::Oblique:
                style.fontStyle = "Italic";
                break;
            case tgfx::SVGFontStyle::Type::Normal:
                style.fontStyle = "";
                break;
            case tgfx::SVGFontStyle::Type::Inherit:
                break;
        }
    }

    const auto &textAnchor = node.getTextAnchor();
    if (textAnchor.isValue()) {
        switch (textAnchor->type()) {
            case tgfx::SVGTextAnchor::Type::Middle:
                style.textAnchor = "middle";
                break;
            case tgfx::SVGTextAnchor::Type::End:
                style.textAnchor = "end";
                break;
            case tgfx::SVGTextAnchor::Type::Start:
            case tgfx::SVGTextAnchor::Type::Inherit:
                style.textAnchor = "start";
                break;
        }
    }

    if (node.tag() == tgfx::SVGTag::Line || node.tag() == tgfx::SVGTag::Polyline) {
        style.hasFill = false;
    }
    return style;
}

void ApplyStyles(Layer &layer, const ComputedStyle &style) {
    if (style.hasFill) {
        auto fill = std::make_unique<FillStyle>();
        fill->color.setStaticValue(WithOpacity(style.fillColor, style.fillOpacity));
        fill->fillRule = style.fillRule;
        layer.styles.push_back(std::move(fill));
    }
    if (style.hasStroke) {
        auto stroke = std::make_unique<StrokeStyle>();
        stroke->color.setStaticValue(WithOpacity(style.strokeColor, style.strokeOpacity));
        stroke->width.setStaticValue(style.strokeWidth);
        stroke->cap = style.cap;
        stroke->join = style.join;
        stroke->miterLimit = style.miterLimit;
        stroke->position = StrokePosition::Center;
        layer.styles.push_back(std::move(stroke));
    }
}

}  // namespace svg
}  // namespace motion
