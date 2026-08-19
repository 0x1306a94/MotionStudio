#include "SvgStyle.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGGradient.h"
#include "tgfx/svg/node/SVGLinearGradient.h"
#include "tgfx/svg/node/SVGRadialGradient.h"
#include "tgfx/svg/node/SVGStop.h"

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

std::string LocalIriId(const tgfx::SVGIRI &iri) {
    std::string id = iri.iri();
    if (!id.empty() && id.front() == '#') {
        id = id.substr(1);
    }
    return id;
}

void PushDiagnostic(std::vector<Diagnostic> *diagnostics, const std::string &code,
                    const std::string &message) {
    if (diagnostics == nullptr) {
        return;
    }
    Diagnostic diagnostic = {};
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostics->push_back(diagnostic);
}

const tgfx::SVGNode *FindMappedNode(const tgfx::SVGIDMapper &mapper, const std::string &id) {
    const auto it = mapper.find(id);
    if (it == mapper.end()) {
        return nullptr;
    }
    return it->second.get();
}

const tgfx::SVGGradient *AsGradient(const tgfx::SVGNode *node) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->tag() == tgfx::SVGTag::LinearGradient || node->tag() == tgfx::SVGTag::RadialGradient) {
        return static_cast<const tgfx::SVGGradient *>(node);
    }
    return nullptr;
}

size_t CountStops(const tgfx::SVGGradient &gradient) {
    size_t count = 0;
    for (const auto &child : gradient.getChildren()) {
        if (child && child->tag() == tgfx::SVGTag::Stop) {
            count += 1;
        }
    }
    return count;
}

Color ResolveStopColor(const tgfx::SVGStop &stop, const Color &currentColor) {
    const auto &stopColor = stop.getStopColor();
    if (!stopColor.isValue()) {
        return {0.f, 0.f, 0.f, 1.f};
    }
    if (stopColor->type() == tgfx::SVGColor::Type::CurrentColor) {
        return currentColor;
    }
    return ToMotionColor(stopColor->color());
}

void CollectStops(const tgfx::SVGGradient &gradient, std::vector<GradientStop> &out) {
    const tgfx::SVGLengthContext unitContext(tgfx::Size::Make(1.f, 1.f));
    for (const auto &child : gradient.getChildren()) {
        if (!child || child->tag() != tgfx::SVGTag::Stop) {
            continue;
        }
        const auto &stop = static_cast<const tgfx::SVGStop &>(*child);
        float offset = unitContext.resolve(stop.getOffset(), tgfx::SVGLengthContext::LengthType::Other);
        offset = std::clamp(offset, 0.f, 1.f);
        Color color = ResolveStopColor(stop, {0.f, 0.f, 0.f, 1.f});
        if (stop.getStopOpacity().isValue()) {
            color.a *= *stop.getStopOpacity();
        }
        GradientStop mapped = {};
        mapped.color.setStaticValue(color);
        mapped.position.setStaticValue(offset);
        out.push_back(mapped);
    }
}

bool StopPositionLess(const GradientStop &lhs, const GradientStop &rhs) {
    return lhs.position.staticValue() < rhs.position.staticValue();
}

float ResolveGradientCoord(const tgfx::SVGLength &length, tgfx::SVGLengthContext::LengthType type,
                           bool objectBBox, float min, const Vec2 &boundsSize,
                           const tgfx::SVGLengthContext &userContext) {
    if (objectBBox) {
        tgfx::SVGLengthContext resolved(tgfx::Size::Make(boundsSize.x, boundsSize.y));
        resolved.setBoundingBoxUnits(tgfx::SVGObjectBoundingBoxUnits(
            tgfx::SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox));
        return resolved.resolve(length, type);
    }
    return userContext.resolve(length, type) - min;
}

Vec2 MapGradientPoint(const tgfx::Matrix &matrix, Vec2 point) {
    const tgfx::Point mapped = matrix.mapXY(point.x, point.y);
    return {mapped.x, mapped.y};
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
            style.fillIri.clear();
        } else if (fill->type() == tgfx::SVGPaint::Type::Color) {
            style.hasFill = true;
            style.fillIri.clear();
            style.fillColor = ResolvePaintColor(*fill, style.currentColor);
        } else if (fill->type() == tgfx::SVGPaint::Type::IRI) {
            style.hasFill = true;
            style.fillIri = LocalIriId(fill->iri());
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
            style.strokeIri.clear();
        } else if (stroke->type() == tgfx::SVGPaint::Type::Color) {
            style.hasStroke = true;
            style.strokeIri.clear();
            style.strokeColor = ResolvePaintColor(*stroke, style.currentColor);
        } else if (stroke->type() == tgfx::SVGPaint::Type::IRI) {
            style.hasStroke = true;
            style.strokeIri = LocalIriId(stroke->iri());
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
        style.fillIri.clear();
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

bool TryMapGradient(const tgfx::SVGPaint &paint, const tgfx::SVGIDMapper &mapper,
                    const Vec2 &boundsMin, const Vec2 &boundsSize, GradientPaint *out,
                    std::vector<Diagnostic> *diagnostics) {
    if (out == nullptr || paint.type() != tgfx::SVGPaint::Type::IRI) {
        return false;
    }
    if (paint.iri().type() != tgfx::SVGIRI::Type::Local) {
        PushDiagnostic(diagnostics, "paint.unresolved", "fill or stroke IRI could not be resolved");
        return false;
    }
    const tgfx::SVGNode *node = FindMappedNode(mapper, LocalIriId(paint.iri()));
    if (node == nullptr || node->tag() == tgfx::SVGTag::Pattern) {
        PushDiagnostic(diagnostics, "paint.unresolved", "fill or stroke IRI could not be resolved");
        return false;
    }
    for (int depth = 0; depth < 8; ++depth) {
        const tgfx::SVGGradient *gradient = AsGradient(node);
        if (gradient == nullptr) {
            PushDiagnostic(diagnostics, "paint.unresolved",
                           "fill or stroke IRI could not be resolved");
            return false;
        }
        if (CountStops(*gradient) >= 2u) {
            break;
        }
        const std::string href = LocalIriId(gradient->getHref());
        if (href.empty()) {
            break;
        }
        const tgfx::SVGNode *next = FindMappedNode(mapper, href);
        if (next == nullptr) {
            break;
        }
        node = next;
    }
    const tgfx::SVGGradient *gradient = AsGradient(node);
    if (gradient == nullptr) {
        PushDiagnostic(diagnostics, "paint.unresolved", "fill or stroke IRI could not be resolved");
        return false;
    }
    std::vector<GradientStop> stops;
    CollectStops(*gradient, stops);
    if (stops.size() < 2) {
        PushDiagnostic(diagnostics, "gradient.stops", "gradient has fewer than two stops");
        return false;
    }
    std::sort(stops.begin(), stops.end(), StopPositionLess);

    const bool objectBBox = gradient->getGradientUnits().type() ==
        tgfx::SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox;
    const tgfx::SVGLengthContext userContext(tgfx::Size::Make(boundsSize.x, boundsSize.y));
    Vec2 start = {};
    Vec2 end = {};
    if (node->tag() == tgfx::SVGTag::LinearGradient) {
        const auto &linear = static_cast<const tgfx::SVGLinearGradient &>(*node);
        start.x = ResolveGradientCoord(linear.getX1(), tgfx::SVGLengthContext::LengthType::Horizontal,
                                       objectBBox, boundsMin.x, boundsSize, userContext);
        start.y = ResolveGradientCoord(linear.getY1(), tgfx::SVGLengthContext::LengthType::Vertical,
                                       objectBBox, boundsMin.y, boundsSize, userContext);
        end.x = ResolveGradientCoord(linear.getX2(), tgfx::SVGLengthContext::LengthType::Horizontal,
                                     objectBBox, boundsMin.x, boundsSize, userContext);
        end.y = ResolveGradientCoord(linear.getY2(), tgfx::SVGLengthContext::LengthType::Vertical,
                                     objectBBox, boundsMin.y, boundsSize, userContext);
        out->type = GradientType::Linear;
    } else {
        const auto &radial = static_cast<const tgfx::SVGRadialGradient &>(*node);
        start.x = ResolveGradientCoord(radial.getCx(), tgfx::SVGLengthContext::LengthType::Horizontal,
                                       objectBBox, boundsMin.x, boundsSize, userContext);
        start.y = ResolveGradientCoord(radial.getCy(), tgfx::SVGLengthContext::LengthType::Vertical,
                                       objectBBox, boundsMin.y, boundsSize, userContext);
        const float radius =
            ResolveGradientCoord(radial.getR(), tgfx::SVGLengthContext::LengthType::Other,
                                 objectBBox, 0.f, boundsSize, userContext);
        end = {start.x + radius, start.y};
        if (radial.getFx().has_value() || radial.getFy().has_value()) {
            float focalX = start.x;
            float focalY = start.y;
            if (radial.getFx().has_value()) {
                focalX = ResolveGradientCoord(*radial.getFx(),
                                              tgfx::SVGLengthContext::LengthType::Horizontal,
                                              objectBBox, boundsMin.x, boundsSize, userContext);
            }
            if (radial.getFy().has_value()) {
                focalY = ResolveGradientCoord(*radial.getFy(),
                                              tgfx::SVGLengthContext::LengthType::Vertical,
                                              objectBBox, boundsMin.y, boundsSize, userContext);
            }
            if (std::fabs(focalX - start.x) > 1e-4f || std::fabs(focalY - start.y) > 1e-4f) {
                PushDiagnostic(diagnostics, "gradient.focal",
                               "radial gradient focal point is not imported");
            }
        }
        out->type = GradientType::Radial;
    }
    if (gradient->getSpreadMethod().type() != tgfx::SVGSpreadMethod::Type::Pad) {
        PushDiagnostic(diagnostics, "gradient.spread",
                       "gradient spreadMethod other than pad is not imported");
    }
    start = MapGradientPoint(gradient->getGradientTransform(), start);
    end = MapGradientPoint(gradient->getGradientTransform(), end);
    out->start.setStaticValue(start);
    out->end.setStaticValue(end);
    out->stops = std::move(stops);
    return true;
}

void ApplyPaintStyles(Layer &layer, const ComputedStyle &style, const tgfx::SVGIDMapper *mapper,
                      Vec2 boundsMin, Vec2 boundsSize, std::vector<Diagnostic> *diagnostics) {
    if (style.hasFill) {
        auto fill = std::make_unique<FillStyle>();
        fill->fillRule = style.fillRule;
        bool applied = false;
        if (!style.fillIri.empty()) {
            if (mapper == nullptr) {
                PushDiagnostic(diagnostics, "paint.unresolved",
                               "fill or stroke IRI could not be resolved");
            } else {
                const tgfx::SVGPaint paint(tgfx::SVGIRI(tgfx::SVGIRI::Type::Local, style.fillIri),
                                           tgfx::SVGColor());
                GradientPaint gradient;
                if (TryMapGradient(paint, *mapper, boundsMin, boundsSize, &gradient, diagnostics)) {
                    for (GradientStop &stop : gradient.stops) {
                        stop.color.setStaticValue(
                            WithOpacity(stop.color.staticValue(), style.fillOpacity));
                    }
                    fill->paintMode = StylePaintMode::Gradient;
                    fill->gradient = std::move(gradient);
                    applied = true;
                }
            }
        } else {
            fill->color.setStaticValue(WithOpacity(style.fillColor, style.fillOpacity));
            applied = true;
        }
        if (applied) {
            layer.styles.push_back(std::move(fill));
        }
    }
    if (style.hasStroke) {
        auto stroke = std::make_unique<StrokeStyle>();
        stroke->width.setStaticValue(style.strokeWidth);
        stroke->cap = style.cap;
        stroke->join = style.join;
        stroke->miterLimit = style.miterLimit;
        stroke->position = StrokePosition::Center;
        bool applied = false;
        if (!style.strokeIri.empty()) {
            if (mapper == nullptr) {
                PushDiagnostic(diagnostics, "paint.unresolved",
                               "fill or stroke IRI could not be resolved");
            } else {
                const tgfx::SVGPaint paint(tgfx::SVGIRI(tgfx::SVGIRI::Type::Local, style.strokeIri),
                                           tgfx::SVGColor());
                GradientPaint gradient;
                if (TryMapGradient(paint, *mapper, boundsMin, boundsSize, &gradient, diagnostics)) {
                    for (GradientStop &stop : gradient.stops) {
                        stop.color.setStaticValue(
                            WithOpacity(stop.color.staticValue(), style.strokeOpacity));
                    }
                    stroke->paintMode = StylePaintMode::Gradient;
                    stroke->gradient = std::move(gradient);
                    applied = true;
                }
            }
        } else {
            stroke->color.setStaticValue(WithOpacity(style.strokeColor, style.strokeOpacity));
            applied = true;
        }
        if (applied) {
            layer.styles.push_back(std::move(stroke));
        }
    }
}

}  // namespace svg
}  // namespace motion
