#pragma once

#include "RuntimeFilter.h"

namespace motion {

class BrightnessContrastFilter : public RuntimeFilter {
  public:
    static std::shared_ptr<tgfx::Image> Apply(std::shared_ptr<tgfx::Image> input, RenderCache *cache, float brightness,
                                              float contrast, tgfx::Point *offset);

    BrightnessContrastFilter(RenderCache *cache, float brightness, float contrast);

  protected:
    DEFINE_RUNTIME_FILTER_TYPE

    std::string onBuildFragmentShader() const override;
    std::vector<tgfx::BindingEntry> uniformBlocks() const override;
    void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                          const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                          const tgfx::Point &offset) const override;

  private:
    float brightness = 0.f;
    float contrast = 0.f;
};

}  // namespace motion
