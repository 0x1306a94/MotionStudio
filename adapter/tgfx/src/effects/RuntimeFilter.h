#pragma once

#include "RenderCache.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <tgfx/core/Image.h>
#include <tgfx/core/Point.h>
#include <tgfx/core/Rect.h>
#include <tgfx/gpu/Attribute.h>
#include <tgfx/gpu/CommandEncoder.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/RenderPass.h>
#include <tgfx/gpu/RenderPipeline.h>
#include <tgfx/gpu/RuntimeEffect.h>
#include <tgfx/gpu/Texture.h>

namespace motion {

#define DEFINE_RUNTIME_FILTER_TYPE                            \
    uint32_t filterType() const override {                    \
        static const uint32_t type = NextRuntimeFilterType(); \
        return type;                                          \
    }

uint32_t NextRuntimeFilterType();

class RuntimeFilter : public tgfx::RuntimeEffect {
  public:
    explicit RuntimeFilter(RenderCache *cache, const std::vector<std::shared_ptr<tgfx::Image>> &extraInputs = {});

    uint32_t typeId() const {
        return filterType();
    }

    tgfx::Rect filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const override;
    bool onDraw(tgfx::CommandEncoder *encoder, const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                std::shared_ptr<tgfx::Texture> outputTexture, const tgfx::Point &offset) const override;

  protected:
    RenderCache *cache = nullptr;

    virtual uint32_t filterType() const = 0;
    virtual tgfx::Rect filterBounds(const tgfx::Rect &srcRect) const;
    virtual std::string onBuildVertexShader() const;
    virtual std::string onBuildFragmentShader() const;
    virtual int sampleCount() const;
    virtual std::vector<tgfx::Attribute> vertexAttributes() const;
    virtual std::vector<tgfx::BindingEntry> uniformBlocks() const;
    virtual std::vector<tgfx::BindingEntry> textureSamplers() const;
    virtual std::vector<float> computeVertices(const tgfx::Texture *source, const tgfx::Texture *target,
                                               const tgfx::Point &offset) const;
    virtual size_t vertexCount() const;
    virtual void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                                  const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                                  const tgfx::Point &offset) const;
    virtual void onConfigurePipeline(tgfx::RenderPipelineDescriptor *descriptor) const;
    virtual std::unique_ptr<FilterResources> onCreateFilterResources() const;
    virtual void onConfigureRenderPass(tgfx::RenderPassDescriptor *desc, FilterResources *resources, tgfx::GPU *gpu,
                                       const std::shared_ptr<tgfx::Texture> &outputTexture) const;

    FilterResources *getFilterResources(tgfx::GPU *gpu) const;

  private:
    std::shared_ptr<tgfx::RenderPipeline> createPipeline(tgfx::GPU *gpu) const;
};

}  // namespace motion
