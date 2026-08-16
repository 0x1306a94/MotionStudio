#pragma once

#include "RuntimeFilter.h"

namespace motion {

class AlphaEdgeDetectFilter : public RuntimeFilter {
  public:
    explicit AlphaEdgeDetectFilter(RenderCache *cache);

  protected:
    DEFINE_RUNTIME_FILTER_TYPE

    std::string onBuildFragmentShader() const override;
    std::vector<tgfx::BindingEntry> uniformBlocks() const override;
    void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                          const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                          const tgfx::Point &offset) const override;
};

}  // namespace motion
