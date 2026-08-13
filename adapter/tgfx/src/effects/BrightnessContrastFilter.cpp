#include "BrightnessContrastFilter.h"

#include "RenderCache.h"

#include <cstring>

#include <tgfx/core/ImageFilter.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>
#include <tgfx/gpu/RenderPass.h>

namespace motion {

static const char FRAGMENT_SHADER[] = R"(
precision highp float;
in vec2 vertexColor;
uniform sampler2D sTexture;
layout(std140) uniform Args {
    float mBrightness;
    float mContrast;
};
out vec4 tgfx_FragColor;

#define EPSILON 1e-10
vec3 saturate(vec3 v) { return clamp(v, vec3(0.0), vec3(1.0)); }

vec3 HUEtoRGB(float H) {
    float R = abs(H * 6.0 - 3.0) - 1.0;
    float G = 2.0 - abs(H * 6.0 - 2.0);
    float B = 2.0 - abs(H * 6.0 - 4.0);
    return saturate(vec3(R,G,B));
}

vec3 RGBtoHCV(vec3 RGB) {
    vec4 P = (RGB.g < RGB.b) ? vec4(RGB.bg, -1.0, 2.0/3.0) : vec4(RGB.gb, 0.0, -1.0/3.0);
    vec4 Q = (RGB.r < P.x) ? vec4(P.xyw, RGB.r) : vec4(RGB.r, P.yzx);
    float C = Q.x - min(Q.w, Q.y);
    float H = abs((Q.w - Q.y) / (6.0 * C + EPSILON) + Q.z);
    return vec3(H, C, Q.x);
}

vec3 RGBtoHSV(vec3 RGB) {
    vec3 HCV = RGBtoHCV(RGB);
    float S = HCV.y / (HCV.z + EPSILON);
    return vec3(HCV.x, S, HCV.z);
}

vec3 HSVtoRGB(vec3 HSV) {
    vec3 RGB = HUEtoRGB(HSV.x);
    return ((RGB - 1.0) * HSV.y + 1.0) * HSV.z;
}

void main() {
    vec4 color = texture(sTexture, vertexColor);
    vec3 rgbColor = color.rgb * mContrast + 0.5 - mContrast * 0.5;
    vec3 hsvColor = RGBtoHSV(rgbColor);
    hsvColor.z *= (mBrightness + 1.0);
    rgbColor = HSVtoRGB(hsvColor);
    rgbColor += (mBrightness / 2.0);
    tgfx_FragColor = vec4(rgbColor * color.a, color.a);
}
)";

std::shared_ptr<tgfx::Image> BrightnessContrastFilter::Apply(std::shared_ptr<tgfx::Image> input, RenderCache *cache,
                                                             float brightness, float contrast, tgfx::Point *offset) {
    if (input == nullptr) {
        return nullptr;
    }
    auto filter = std::make_shared<BrightnessContrastFilter>(cache, brightness, contrast);
    return input->makeWithFilter(tgfx::ImageFilter::Runtime(filter), offset);
}

BrightnessContrastFilter::BrightnessContrastFilter(RenderCache *cache, float brightness, float contrast)
    : RuntimeFilter(cache)
    , brightness(brightness)
    , contrast(contrast) {
}

std::string BrightnessContrastFilter::onBuildFragmentShader() const {
    return FRAGMENT_SHADER;
}

std::vector<tgfx::BindingEntry> BrightnessContrastFilter::uniformBlocks() const {
    return {{"Args", 0}};
}

void BrightnessContrastFilter::onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *,
                                                const std::vector<std::shared_ptr<tgfx::Texture>> &,
                                                const tgfx::Point &) const {
    struct Uniforms {
        float brightness = 0.0f;
        float contrast = 0.0f;
    };

    Uniforms uniforms = {};
    uniforms.brightness = brightness > 0 ? brightness / 250.f : brightness / 650.f;
    uniforms.contrast = 1.0f + contrast / 300.f;
    if (cache == nullptr) {
        return;
    }
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
