#include "AlphaEdgeDetectFilter.h"

#include <cstring>

#include <tgfx/gpu/RenderPass.h>

namespace motion {
namespace {

const char FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec2 vertexColor;
        uniform sampler2D sTexture;

        layout(std140) uniform FilterUniforms {
            float mHorizontalStep;
            float mVerticalStep;
        };

        out vec4 tgfx_FragColor;

        float threshold = 0.9;

        void main() {
            float alphaSum = 0.0;
            alphaSum += floor(texture(sTexture, vertexColor + vec2(-mHorizontalStep, -mVerticalStep)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(-mHorizontalStep, 0.0)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(-mHorizontalStep, mVerticalStep)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(mHorizontalStep, -mVerticalStep)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(mHorizontalStep, 0.0)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(mHorizontalStep, mVerticalStep)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(0.0, -mVerticalStep)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(0.0, 0.0)).a + threshold);
            alphaSum += floor(texture(sTexture, vertexColor + vec2(0.0, mVerticalStep)).a + threshold);
            tgfx_FragColor = (alphaSum > 0.0 && alphaSum < 9.0) ? vec4(1.0) : vec4(0.0);
        }
    )";

}  // namespace

AlphaEdgeDetectFilter::AlphaEdgeDetectFilter(RenderCache *cache)
    : RuntimeFilter(cache) {
}

std::string AlphaEdgeDetectFilter::onBuildFragmentShader() const {
    return FRAGMENT_SHADER;
}

std::vector<tgfx::BindingEntry> AlphaEdgeDetectFilter::uniformBlocks() const {
    return {{"FilterUniforms", 0}};
}

void AlphaEdgeDetectFilter::onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *,
                                             const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                                             const tgfx::Point &) const {
    if (inputTextures.empty() || cache == nullptr) {
        return;
    }
    struct Uniforms {
        float horizontalStep = 0.0f;
        float verticalStep = 0.0f;
    };
    Uniforms uniforms = {};
    uniforms.horizontalStep = 1.0f / static_cast<float>(inputTextures[0]->width());
    uniforms.verticalStep = 1.0f / static_cast<float>(inputTextures[0]->height());

    auto slice = cache->acquireUniformSlice(sizeof(Uniforms));
    if (slice.buffer == nullptr) {
        return;
    }
    void *mapped = slice.buffer->map(slice.offset, sizeof(Uniforms));
    if (mapped == nullptr) {
        return;
    }
    std::memcpy(mapped, &uniforms, sizeof(Uniforms));
    slice.buffer->unmap();
    renderPass->setUniformBuffer(0, slice.buffer, slice.offset, sizeof(Uniforms));
}

}  // namespace motion
