#pragma once

#include <memory>

#include <tgfx/core/Image.h>
#include <tgfx/core/Point.h>

namespace motion {

class RenderCache;

class GaussianBlurFilter {
  public:
    // Applies a two-pass Gaussian blur. Radius is blurriness/2, matching PAG.
    // input: source image; null returns null.
    // cache: unused, kept for the shared Apply signature.
    // blurriness: AE-style blur amount in pixels.
    // repeatEdgePixels: Clamp + clip to input size when true.
    // offset: optional filter origin shift written by makeWithFilter.
    static std::shared_ptr<tgfx::Image> Apply(std::shared_ptr<tgfx::Image> input, RenderCache *cache,
                                              float blurriness, bool repeatEdgePixels,
                                              tgfx::Point *offset);
};

}  // namespace motion
