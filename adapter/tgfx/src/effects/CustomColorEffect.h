#pragma once

#include "Uniform.h"
#include "UniformData.h"

#include <MotionStudio/common/EntityId.h>
#include <tgfx/gpu/RuntimeEffect.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace tgfx {
class GPU;
class RenderPipeline;
class GPUBuffer;
class Shader;
};  // namespace tgfx

namespace motion {
class RenderCache;
struct CustomColorEffectResource;

/**
 * Lightweight procedural color RuntimeEffect. User supplies a GLSL mainImage(uv)
 * plus uniforms; output is a generated color field sized to prepare()'s AABB.
 */
class CustomColorEffect : public tgfx::RuntimeEffect, public std::enable_shared_from_this<CustomColorEffect> {
  public:
    static std::shared_ptr<CustomColorEffect> Make(EntityId effectId, std::vector<Uniform> uniforms);

    void prepare(const tgfx::Size &sourceSize, RenderCache *cache);
    UniformData *getUniformData() const;

    // Builds a Clamp ImageShader. Uses a shared 1x1 placeholder source; output
    // size comes from prepare()'s sourceSize (the shape AABB).
    std::shared_ptr<tgfx::Shader> makeImageShader();

  private:
    CustomColorEffect(EntityId effectId, std::vector<Uniform> uniforms);

    std::shared_ptr<tgfx::RenderPipeline> createPipeline(tgfx::GPU *gpu) const;
    CustomColorEffectResource *getEffectResource(tgfx::GPU *gpu) const;

    tgfx::Rect filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const override;

    bool onDraw(tgfx::CommandEncoder *encoder, const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures, std::shared_ptr<tgfx::Texture> outputTexture, const tgfx::Point &offset) const override;

  private:
    EntityId effectId_ = {};
    std::unique_ptr<UniformData> uniformData_ = nullptr;
    // Written from const onDraw via UniformData; CPU shadow of the UBO.
    mutable std::vector<uint8_t> uniformBytes_ = {};
    tgfx::Size sourceSize_ = {};
    RenderCache *cache_ = nullptr;
};
}  // namespace motion
