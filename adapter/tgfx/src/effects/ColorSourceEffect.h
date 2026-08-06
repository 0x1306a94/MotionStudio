#pragma once

#include "Uniform.h"
#include "UniformData.h"

#include <tgfx/core/Rect.h>
#include <tgfx/gpu/RuntimeEffect.h>

#include <memory>
#include <string>
#include <vector>

namespace tgfx {
class GPU;
class RenderPipeline;
class GPUBuffer;
class Shader;
};  // namespace tgfx

namespace motion {
class RenderCache;

// Shadertoy-compatible playback context written into built-in uniforms each draw.
struct ColorSourceFrameContext {
    float timeSeconds = 0.f;
    int64_t frameIndex = 0;
    float frameRate = 30.f;
};

/**
 * Lightweight procedural color RuntimeEffect. User supplies a GLSL mainImage(uv)
 * plus uniforms; output is a generated color field sized to sourceBounds.
 */
class ColorSourceEffect : public tgfx::RuntimeEffect, public std::enable_shared_from_this<ColorSourceEffect> {
  public:
    static std::shared_ptr<ColorSourceEffect> Make(std::string mainImage, std::vector<Uniform> uniforms,
                                                   const tgfx::Rect &sourceBounds, RenderCache *cache);

    void setFrameContext(ColorSourceFrameContext frameContext);
    UniformData *getUniformData() const;

    // Builds a Clamp ImageShader placed at sourceBounds origin.
    std::shared_ptr<tgfx::Shader> makeImageShader();

  private:
    ColorSourceEffect(std::string mainImage, std::vector<Uniform> uniforms, tgfx::Rect sourceBounds,
                      RenderCache *cache);

    std::shared_ptr<tgfx::RenderPipeline> createPipeline(tgfx::GPU *gpu) const;
    std::shared_ptr<tgfx::RenderPipeline> getOrCreatePipeline(tgfx::GPU *gpu) const;

    tgfx::Rect filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const override;

    bool onDraw(tgfx::CommandEncoder *encoder, const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures, std::shared_ptr<tgfx::Texture> outputTexture, const tgfx::Point &offset) const override;

  private:
    std::string mainImage_ = {};
    std::unique_ptr<UniformData> uniformData_ = nullptr;
    // Written from const onDraw via UniformData; CPU shadow of the UBO.
    mutable std::vector<uint8_t> uniformBytes_ = {};
    tgfx::Rect sourceBounds_ = {};
    RenderCache *cache_ = nullptr;
    ColorSourceFrameContext frameContext_ = {};
    mutable float lastTimeSeconds_ = 0.f;
    mutable bool hasPreviousTime_ = false;
};
}  // namespace motion
