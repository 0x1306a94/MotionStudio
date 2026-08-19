#include "SvgLength.h"

#include "tgfx/core/Size.h"

namespace motion {
namespace svg {

tgfx::SVGLengthContext MakeRootLengthContext(int sourceWidth, int sourceHeight) {
    const float width = sourceWidth > 0 ? static_cast<float>(sourceWidth) : 100.f;
    const float height = sourceHeight > 0 ? static_cast<float>(sourceHeight) : 100.f;
    return tgfx::SVGLengthContext(tgfx::Size::Make(width, height));
}

}  // namespace svg
}  // namespace motion
