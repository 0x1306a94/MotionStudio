#include "GaussianBlurFilter.h"

#include <tgfx/core/ImageFilter.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/TileMode.h>

namespace motion {

std::shared_ptr<tgfx::Image> GaussianBlurFilter::Apply(std::shared_ptr<tgfx::Image> input,
                                                       RenderCache * /*cache*/, float blurriness,
                                                       bool repeatEdgePixels, tgfx::Point *offset) {
    if (input == nullptr || blurriness <= 0.0f) {
        return input;
    }
    const float radius = blurriness * 0.5f;
    if (repeatEdgePixels) {
        auto filter = tgfx::ImageFilter::Blur(radius, radius, tgfx::TileMode::Clamp);
        tgfx::Rect clip = tgfx::Rect::MakeWH(static_cast<float>(input->width()),
                                             static_cast<float>(input->height()));
        return input->makeWithFilter(filter, offset, &clip);
    }
    auto filter = tgfx::ImageFilter::Blur(radius, radius);
    return input->makeWithFilter(filter, offset);
}

}  // namespace motion
