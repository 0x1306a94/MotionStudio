#include "SolidStrokeFilter.h"

#include <algorithm>
#include <cstring>

#include <tgfx/core/ImageFilter.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/RenderPass.h>
#include <tgfx/gpu/Texture.h>

namespace motion {
namespace {

// Isolation snapshots are tight to opaque content. Clamp-to-edge would then
// treat the dilated ring as interior, so Outside must zero srcColor outside UV 0-1.
const char SOLID_STROKE_FRAGMENT_SHADER[] = R"(
        precision highp float;
        uniform sampler2D sTexture;
        uniform sampler2D uOriginalTextureInput;

        layout(std140) uniform FilterUniforms {
            vec3 uColor;
            float uIsUseOriginalTexture;
            vec2 uSize;
            float uIsOutside;
            float uIsCenter;
            float uIsInside;
        };

        in vec2 vertexColor;
        out vec4 tgfx_FragColor;

        const float PI = 3.1415926535;
        float threshold = 0.3;

        float check(vec2 point) {
            vec2 result = step(point, vec2(1.0)) * step(vec2(0.0), point);
            return step(0.5, result.x * result.y);
        }

        void main()
        {
            vec2 point = vertexColor;
            vec4 inputColor = texture(sTexture, point);
            float alphaSum = inputColor.a * check(point);
            for (float i = 0.0; i <= 180.0; i += 11.25) {
                float arc = i * PI / 180.0;
                float measureX = cos(arc) * uSize.x;
                float measureY = sqrt(pow(uSize.x, 2.0) - pow(measureX, 2.0)) * uSize.y / uSize.x;
                point = vertexColor + vec2(measureX, measureY);
                alphaSum += texture(sTexture, point).a * check(point);
                point = vertexColor + vec2(measureX, -measureY);
                alphaSum += texture(sTexture, point).a * check(point);
            }

            vec4 srcColor = (uIsUseOriginalTexture == 1.0) ? texture(uOriginalTextureInput, vertexColor) : inputColor;
            srcColor *= check(vertexColor);

            vec4 result = (alphaSum > 0.0) ? vec4(uColor, 1.0) : vec4(0.0);
            result = (uIsOutside == 1.0 && srcColor.a > threshold) ? srcColor : result;
            result = (uIsCenter == 1.0 && result.a < threshold) ? srcColor : result;
            result = (uIsInside == 1.0 && (result.a < threshold || srcColor.a < threshold)) ? srcColor : result;
            tgfx_FragColor = result;
        }
    )";

const char SOLID_STROKE_THICK_FRAGMENT_SHADER[] = R"(
        precision highp float;
        uniform sampler2D sTexture;
        uniform sampler2D uOriginalTextureInput;

        layout(std140) uniform FilterUniforms {
            vec3 uColor;
            float uIsUseOriginalTexture;
            vec2 uSize;
            float uIsOutside;
            float uIsCenter;
            float uIsInside;
        };

        in vec2 vertexColor;
        out vec4 tgfx_FragColor;

        const float PI = 3.1415926535;
        float threshold = 0.3;

        float check(vec2 point) {
            vec2 result = step(point, vec2(1.0)) * step(vec2(0.0), point);
            return step(0.5, result.x * result.y);
        }

        void main()
        {
            vec2 point = vertexColor;
            vec4 inputColor = texture(sTexture, point);
            float alphaSum = inputColor.a * check(point);
            for (float i = 0.0; i <= 180.0; i += 11.25) {
                float arc = i * PI / 180.0;
                float measureX = cos(arc) * uSize.x;
                float measureY = sqrt(pow(uSize.x, 2.0) - pow(measureX, 2.0)) * uSize.y / uSize.x;
                point = vertexColor + vec2(measureX, measureY);
                alphaSum += texture(sTexture, point).a * check(point);
                point = vertexColor + vec2(measureX, -measureY);
                alphaSum += texture(sTexture, point).a * check(point);
                point = vertexColor + vec2(measureX / 2.0, measureY / 2.0);
                alphaSum += texture(sTexture, point).a * check(point);
                point = vertexColor + vec2(measureX / 2.0, -measureY / 2.0);
                alphaSum += texture(sTexture, point).a * check(point);
            }

            vec4 srcColor = (uIsUseOriginalTexture == 1.0) ? texture(uOriginalTextureInput, vertexColor) : inputColor;
            srcColor *= check(vertexColor);

            vec4 result = (alphaSum > 0.0) ? vec4(uColor, 1.0) : vec4(0.0);
            result = (uIsOutside == 1.0 && srcColor.a > threshold) ? srcColor : result;
            result = (uIsCenter == 1.0 && result.a < threshold) ? srcColor : result;
            result = (uIsInside == 1.0 && (result.a < threshold || srcColor.a < threshold)) ? srcColor : result;

            tgfx_FragColor = result;
        }
    )";

tgfx::Point ToTexturePoint(const tgfx::Texture *source, const tgfx::Point &texturePoint) {
    return {texturePoint.x / static_cast<float>(source->width()),
            texturePoint.y / static_cast<float>(source->height())};
}

tgfx::Point ToVertexPoint(const tgfx::Texture *target, const tgfx::Point &point) {
    return {2.0f * point.x / static_cast<float>(target->width()) - 1.0f,
            2.0f * point.y / static_cast<float>(target->height()) - 1.0f};
}

}  // namespace

std::shared_ptr<tgfx::ImageFilter> SolidStrokeFilter::Create(RenderCache *cache,
                                                             const SolidStrokeOption &option,
                                                             SolidStrokeMode mode,
                                                             std::shared_ptr<tgfx::Image> originalImage) {
    if (!option.valid()) {
        return nullptr;
    }
    std::shared_ptr<tgfx::RuntimeEffect> effect;
    if (mode == SolidStrokeMode::Normal) {
        if (originalImage) {
            effect = std::make_shared<SolidStrokeNormalFilter>(cache, option, std::move(originalImage));
        } else {
            effect = std::make_shared<SolidStrokeNormalFilter>(cache, option);
        }
    } else if (originalImage) {
        effect = std::make_shared<SolidStrokeThickFilter>(cache, option, std::move(originalImage));
    } else {
        effect = std::make_shared<SolidStrokeThickFilter>(cache, option);
    }
    return tgfx::ImageFilter::Runtime(effect);
}

SolidStrokeFilter::SolidStrokeFilter(RenderCache *cache, const SolidStrokeOption &option)
    : RuntimeFilter(cache)
    , option(option) {
}

SolidStrokeFilter::SolidStrokeFilter(RenderCache *cache, const SolidStrokeOption &option,
                                     std::shared_ptr<tgfx::Image> originalImage)
    : RuntimeFilter(cache, {std::move(originalImage)})
    , option(option)
    , hasOriginalImage(true) {
}

std::string SolidStrokeNormalFilter::onBuildFragmentShader() const {
    return SOLID_STROKE_FRAGMENT_SHADER;
}

std::string SolidStrokeThickFilter::onBuildFragmentShader() const {
    return SOLID_STROKE_THICK_FRAGMENT_SHADER;
}

std::vector<tgfx::BindingEntry> SolidStrokeFilter::uniformBlocks() const {
    return {{"FilterUniforms", 0}};
}

std::vector<tgfx::BindingEntry> SolidStrokeFilter::textureSamplers() const {
    return {{"sTexture", 0}, {"uOriginalTextureInput", 1}};
}

void SolidStrokeFilter::onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *,
                                         const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                                         const tgfx::Point &) const {
    if (inputTextures.empty() || cache == nullptr) {
        return;
    }
    const float spreadSizeX = std::min(option.spreadSizeX, STROKE_MAX_SPREAD_SIZE);
    const float spreadSizeY = std::min(option.spreadSizeY, STROKE_MAX_SPREAD_SIZE);

    struct Uniforms {
        float color[3] = {};
        float isUseOriginalTexture = 0.0f;
        float size[2] = {};
        float isOutside = 0.0f;
        float isCenter = 0.0f;
        float isInside = 0.0f;
    };
    Uniforms uniforms = {};
    uniforms.color[0] = option.color.r;
    uniforms.color[1] = option.color.g;
    uniforms.color[2] = option.color.b;
    uniforms.size[0] = spreadSizeX / static_cast<float>(inputTextures[0]->width());
    uniforms.size[1] = spreadSizeY / static_cast<float>(inputTextures[0]->height());
    if (option.position.has_value()) {
        uniforms.isOutside = *option.position == StrokePosition::Outside ? 1.0f : 0.0f;
        uniforms.isCenter = *option.position == StrokePosition::Center ? 1.0f : 0.0f;
        uniforms.isInside = *option.position == StrokePosition::Inside ? 1.0f : 0.0f;
    }
    uniforms.isUseOriginalTexture = (hasOriginalImage && inputTextures.size() > 1) ? 1.0f : 0.0f;

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

std::vector<float> SolidStrokeFilter::computeVertices(const tgfx::Texture *source,
                                                      const tgfx::Texture *target,
                                                      const tgfx::Point &) const {
    const tgfx::Rect outputBounds = tgfx::Rect::MakeWH(target->width(), target->height());
    const tgfx::Point contentPoint[4] = {{outputBounds.left, outputBounds.bottom},
                                         {outputBounds.right, outputBounds.bottom},
                                         {outputBounds.left, outputBounds.top},
                                         {outputBounds.right, outputBounds.top}};
    const float deltaX = -option.spreadSizeX;
    const float deltaY = -option.spreadSizeY;
    const tgfx::Point texturePoints[4] = {
        {deltaX, outputBounds.height() + deltaY},
        {outputBounds.width() + deltaX, outputBounds.height() + deltaY},
        {deltaX, deltaY},
        {outputBounds.width() + deltaX, deltaY}};

    std::vector<float> vertices = {};
    vertices.reserve(16);
    for (size_t i = 0; i < 4; i++) {
        const tgfx::Point vertexPoint = ToVertexPoint(target, contentPoint[i]);
        vertices.push_back(vertexPoint.x);
        vertices.push_back(vertexPoint.y);
        const tgfx::Point texturePoint = ToTexturePoint(source, texturePoints[i]);
        vertices.push_back(texturePoint.x);
        vertices.push_back(texturePoint.y);
    }
    return vertices;
}

tgfx::Rect SolidStrokeFilter::filterBounds(const tgfx::Rect &srcRect) const {
    tgfx::Rect dest = srcRect.makeOutset(option.spreadSizeX, option.spreadSizeY);
    dest.offset(option.offsetX, option.offsetY);
    return dest;
}

}  // namespace motion
