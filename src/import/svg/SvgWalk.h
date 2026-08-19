#pragma once

#include "MotionStudio/import/svg/SvgImporter.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/node/SVGRoot.h"

namespace motion {
namespace svg {

void WalkSvgRoot(const tgfx::SVGRoot &root, const tgfx::SVGIDMapper &mapper, SvgLayerTree &tree);

}  // namespace svg
}  // namespace motion
