#pragma once

#include <memory>
#include <optional>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/StrokePosition.h"
#include "RuntimeFilter.h"

#include <tgfx/core/ImageFilter.h>

namespace motion {

constexpr float STROKE_MAX_SPREAD_SIZE = 25.0f;
constexpr float STROKE_SPREAD_MIN_THICK_SIZE = 12.0f;

enum class SolidStrokeMode { Normal,
                             Thick };

struct SolidStrokeOption {
    std::optional<StrokePosition> position;
    Color color = {};
    float spreadSizeX = 0.0f;
    float spreadSizeY = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    bool valid() const {
        return spreadSizeX != 0.0f || spreadSizeY != 0.0f || offsetX != 0.0f || offsetY != 0.0f;
    }
};

class SolidStrokeFilter : public RuntimeFilter {
  public:
    static std::shared_ptr<tgfx::ImageFilter> Create(RenderCache *cache, const SolidStrokeOption &option,
                                                     SolidStrokeMode mode,
                                                     std::shared_ptr<tgfx::Image> originalImage = nullptr);

    SolidStrokeFilter(RenderCache *cache, const SolidStrokeOption &option);
    SolidStrokeFilter(RenderCache *cache, const SolidStrokeOption &option,
                      std::shared_ptr<tgfx::Image> originalImage);

    std::vector<tgfx::BindingEntry> uniformBlocks() const override;
    std::vector<tgfx::BindingEntry> textureSamplers() const override;
    std::vector<float> computeVertices(const tgfx::Texture *source, const tgfx::Texture *target,
                                       const tgfx::Point &offset) const override;
    tgfx::Rect filterBounds(const tgfx::Rect &srcRect) const override;
    void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                          const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                          const tgfx::Point &offset) const override;

  protected:
    SolidStrokeOption option = {};
    bool hasOriginalImage = false;
};

class SolidStrokeNormalFilter : public SolidStrokeFilter {
  public:
    using SolidStrokeFilter::SolidStrokeFilter;

  protected:
    DEFINE_RUNTIME_FILTER_TYPE
    std::string onBuildFragmentShader() const override;
};

class SolidStrokeThickFilter : public SolidStrokeFilter {
  public:
    using SolidStrokeFilter::SolidStrokeFilter;

  protected:
    DEFINE_RUNTIME_FILTER_TYPE
    std::string onBuildFragmentShader() const override;
};

}  // namespace motion
