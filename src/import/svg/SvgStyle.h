#pragma once

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/Layer.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/node/SVGNode.h"

namespace motion {
namespace svg {

struct ComputedStyle {
    bool hasFill = true;
    Color fillColor{0.f, 0.f, 0.f, 1.f};
    bool hasStroke = false;
    Color strokeColor{0.f, 0.f, 0.f, 1.f};
    float strokeWidth = 1.f;
};

ComputedStyle ResolveNodeStyle(const tgfx::SVGNode &node, const tgfx::SVGLengthContext &lengthContext);
void ApplyStyles(Layer &layer, const ComputedStyle &style);

}  // namespace svg
}  // namespace motion
