#pragma once

#include <string>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/import/svg/SvgImporter.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/node/SVGNode.h"

namespace motion {
namespace svg {

struct ComputedStyle {
    bool hasFill = true;
    Color fillColor{0.f, 0.f, 0.f, 1.f};
    float fillOpacity = 1.f;
    FillRule fillRule = FillRule::NonZero;
    bool hasStroke = false;
    Color strokeColor{0.f, 0.f, 0.f, 1.f};
    float strokeOpacity = 1.f;
    float strokeWidth = 1.f;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.f;
    bool hasDash = false;
    Color currentColor{0.f, 0.f, 0.f, 1.f};
    bool visible = true;
    bool displayNone = false;
    std::string fontFamily;
    float fontSize = 16.f;
    std::string fontStyle;
    bool fontBold = false;
    std::string textAnchor;
    std::string fillIri;
    std::string strokeIri;
};

std::string MappedFontStyle(const ComputedStyle &style);
std::string FirstFontFamily(const std::string &family);

ComputedStyle ResolveStyle(const tgfx::SVGNode &node, const ComputedStyle &parent,
                           const tgfx::SVGLengthContext &lengthContext);
void ApplyStyles(Layer &layer, const ComputedStyle &style);
bool TryMapGradient(const tgfx::SVGPaint &paint, const tgfx::SVGIDMapper &mapper,
                    const Vec2 &boundsMin, const Vec2 &boundsSize, GradientPaint *out,
                    std::vector<Diagnostic> *diagnostics);
void ApplyPaintStyles(Layer &layer, const ComputedStyle &style, const tgfx::SVGIDMapper *mapper,
                      Vec2 boundsMin, Vec2 boundsSize, std::vector<Diagnostic> *diagnostics);

}  // namespace svg
}  // namespace motion
