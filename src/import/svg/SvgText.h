#pragma once

#include "MotionStudio/import/svg/SvgImporter.h"
#include "SvgStyle.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/node/SVGText.h"

namespace motion {
namespace svg {

void ImportSvgText(const tgfx::SVGText &text, EntityId parentId, SvgLayerTree &tree,
                   const tgfx::SVGLengthContext &lengthContext, const tgfx::SVGIDMapper *mapper,
                   const ComputedStyle &parentStyle);

}  // namespace svg
}  // namespace motion
