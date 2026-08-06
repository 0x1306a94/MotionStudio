#include "ColorSourceEffect.h"

#include "RenderCache.h"
#include "UniformData.h"

#include <cassert>
#include <cstring>

#include <tgfx/core/Data.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/ImageFilter.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Point.h>
#include <tgfx/core/Shader.h>
#include <tgfx/core/TileMode.h>
#include <tgfx/gpu/Attribute.h>
#include <tgfx/gpu/CommandEncoder.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>
#include <tgfx/gpu/PixelFormat.h>
#include <tgfx/gpu/RenderPass.h>
#include <tgfx/gpu/RenderPipeline.h>
#include <tgfx/gpu/RuntimeEffect.h>
#include <tgfx/gpu/ShaderModule.h>
#include <tgfx/gpu/ShaderStage.h>

namespace motion {

// Fullscreen triangle driven by aPosition (GLES-portable). Three clip-space
// vertices cover [-1,1]^2; uv is derived from position. Avoid gl_VertexIndex —
// RuntimeEffect shaders are GLES-style and GL backends compile them as-is.
constexpr const char *kVertexShader = R"GLSL(
in vec2 aPosition;
out vec2 v_uv;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    v_uv = aPosition * 0.5 + 0.5;
}
)GLSL";

static std::string GetVersionPrefix(tgfx::GPU *gpu) {
    auto info = gpu->info();
    auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
    return isDesktop ? "#version 150\n\n" : "#version 300 es\n\n";
}

static bool IsDesktopGPU(tgfx::GPU *gpu) {
    auto info = gpu->info();
    return info->version.find("OpenGL ES") == std::string::npos;
}

std::string BuildVertexShaderSource(tgfx::GPU *gpu) {
    return GetVersionPrefix(gpu) + kVertexShader;
}

static void AppendUniformDeclaration(std::string &out, const Uniform &uniform) {
    out += UniformFormatGLSLTypeName(uniform.format());
    out += " ";
    out += uniform.name();
    if (uniform.isArray()) {
        out += "[";
        out += std::to_string(uniform.count());
        out += "]";
    }
    out += ";\n";
}

std::string BuildFragmentShaderSource(tgfx::GPU *gpu, const std::vector<Uniform> &uniforms, const std::string &mainImage) {
    std::string header;
    if (!IsDesktopGPU(gpu)) {
        header += "precision highp float;\n";
    }
    header += "in vec2 v_uv;\n";
    header += "out vec4 out_fragColor;\n";

    std::string uniformBlock = "\nlayout(std140) uniform UniformBlock {\n";
    std::string samplerUniforms;

    for (const auto &uniform : uniforms) {
        if (IsSamplerFormat(uniform.format())) {
            samplerUniforms += "uniform ";
            AppendUniformDeclaration(samplerUniforms, uniform);
        } else {
            uniformBlock += "    ";
            AppendUniformDeclaration(uniformBlock, uniform);
        }
    }

    uniformBlock += "};\n";

    std::string footer = R"GLSL(
void main() {
    out_fragColor = mainImage(v_uv);
}
)GLSL";

    return GetVersionPrefix(gpu) + header + uniformBlock + samplerUniforms + mainImage + footer;
}

static std::shared_ptr<tgfx::Image> CreatePlaceholderImage() {
    tgfx::ImageInfo info = tgfx::ImageInfo::Make(1, 1, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);
    std::vector<uint8_t> pixels(4, 0);
    auto data = tgfx::Data::MakeWithCopy(pixels.data(), pixels.size());
    return tgfx::Image::MakeFrom(info, data);
}

static std::shared_ptr<tgfx::Image> GetPlaceholderImage() {
    static std::shared_ptr<tgfx::Image> image = CreatePlaceholderImage();
    return image;
}

std::shared_ptr<ColorSourceEffect> ColorSourceEffect::Make(EntityId effectId, std::vector<Uniform> uniforms) {
    return std::shared_ptr<ColorSourceEffect>(new ColorSourceEffect(effectId, std::move(uniforms)));
}

ColorSourceEffect::ColorSourceEffect(EntityId effectId, std::vector<Uniform> uniforms)
    : effectId_(effectId) {
    Uniform inputDimsData{"inputDimsData", UniformFormat::Float2};
    uniforms.insert(uniforms.begin(), inputDimsData);
    uniformData_ = std::make_unique<UniformData>(std::move(uniforms));
    uniformBytes_.assign(uniformData_->size(), 0);
    if (!uniformBytes_.empty()) {
        uniformData_->setBuffer(uniformBytes_.data());
    }
}

void ColorSourceEffect::prepare(const tgfx::Size &sourceSize, RenderCache *cache) {
    sourceSize_ = sourceSize;
    cache_ = cache;
}

UniformData *ColorSourceEffect::getUniformData() const {
    return uniformData_.get();
}

std::shared_ptr<tgfx::Shader> ColorSourceEffect::makeImageShader() {
    auto source = GetPlaceholderImage();
    if (source == nullptr) {
        return nullptr;
    }

    tgfx::Point offset{};
    auto filtered = source->makeWithFilter(tgfx::ImageFilter::Runtime(shared_from_this()), &offset);
    if (filtered == nullptr) {
        return nullptr;
    }

    return tgfx::Shader::MakeImageShader(filtered, tgfx::TileMode::Clamp, tgfx::TileMode::Clamp);
}

std::shared_ptr<tgfx::RenderPipeline> ColorSourceEffect::createPipeline(tgfx::GPU *gpu) const {
    if (cache_ == nullptr || uniformData_ == nullptr || gpu == nullptr) {
        return nullptr;
    }

    const auto *mainImage = cache_->findMainImageSource(effectId_);
    if (mainImage == nullptr || mainImage->empty()) {
        return nullptr;
    }

    auto vertexCode = BuildVertexShaderSource(gpu);
    auto fragmentCode = BuildFragmentShaderSource(gpu, uniformData_->uniforms(), *mainImage);

    tgfx::ShaderModuleDescriptor vertexDesc;
    vertexDesc.code = std::move(vertexCode);
    vertexDesc.stage = tgfx::ShaderStage::Vertex;
    auto vertexModule = gpu->createShaderModule(vertexDesc);
    if (vertexModule == nullptr) {
        return nullptr;
    }

    tgfx::ShaderModuleDescriptor fragmentDesc;
    fragmentDesc.code = std::move(fragmentCode);
    fragmentDesc.stage = tgfx::ShaderStage::Fragment;
    auto fragmentModule = gpu->createShaderModule(fragmentDesc);
    if (fragmentModule == nullptr) {
        return nullptr;
    }

    tgfx::RenderPipelineDescriptor pipelineDesc;
    tgfx::VertexBufferLayout vertexLayout({{"aPosition", tgfx::VertexFormat::Float2}});
    pipelineDesc.vertex.bufferLayouts = {vertexLayout};
    pipelineDesc.vertex.module = vertexModule;
    pipelineDesc.vertex.entryPoint = "main";
    pipelineDesc.fragment.module = fragmentModule;
    pipelineDesc.fragment.entryPoint = "main";
    auto &colorAttachment = pipelineDesc.fragment.colorAttachments.emplace_back();
    colorAttachment.format = tgfx::PixelFormat::RGBA_8888;
    colorAttachment.blendEnable = false;
    return gpu->createRenderPipeline(pipelineDesc);
}

ColorSourceEffectResource *ColorSourceEffect::getEffectResource(tgfx::GPU *gpu) const {
    auto resources = cache_->findColorSourceEffectResource(effectId_);
    if (resources == nullptr) {
        auto pipeline = createPipeline(gpu);
        if (pipeline == nullptr) {
            return nullptr;
        }

        auto buffer = gpu->createBuffer(uniformData_->size(), tgfx::GPUBufferUsage::UNIFORM);
        if (buffer == nullptr) {
            return nullptr;
        }

        auto newResources = std::make_unique<ColorSourceEffectResource>();
        newResources->pipeline = std::move(pipeline);
        newResources->uniformBuffer = std::move(buffer);
        resources = newResources.get();
        cache_->addColorSourceEffectResource(effectId_, std::move(newResources));
    }
    assert(resources->pipeline != nullptr);
    return resources;
}

tgfx::Rect ColorSourceEffect::filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const {
    return tgfx::Rect::MakeWH(sourceSize_.width, sourceSize_.height);
}

bool ColorSourceEffect::onDraw(tgfx::CommandEncoder *encoder, const std::vector<std::shared_ptr<tgfx::Texture>> & /*inputTextures*/, std::shared_ptr<tgfx::Texture> outputTexture, const tgfx::Point & /*offset*/) const {
    if (sourceSize_.isZero() || cache_ == nullptr || uniformData_ == nullptr || outputTexture == nullptr || encoder == nullptr) {
        return false;
    }

    auto *gpu = encoder->gpu();
    auto *resources = getEffectResource(gpu);
    if (resources == nullptr || resources->pipeline == nullptr || resources->uniformBuffer == nullptr) {
        return false;
    }

    auto vertexBuffer = cache_->getFullscreenVertexBuffer(gpu);
    if (vertexBuffer == nullptr) {
        return false;
    }

    const float inputDims[2] = {sourceSize_.width, sourceSize_.height};
    uniformData_->setData("inputDimsData", inputDims, sizeof(inputDims));

    void *mapped = resources->uniformBuffer->map();
    if (mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, uniformBytes_.data(), uniformBytes_.size());
    resources->uniformBuffer->unmap();

    tgfx::RenderPassDescriptor passDescriptor(outputTexture, tgfx::LoadAction::Clear, tgfx::StoreAction::Store);
    auto renderPass = encoder->beginRenderPass(passDescriptor);
    if (renderPass == nullptr) {
        return false;
    }
    renderPass->setPipeline(resources->pipeline);
    renderPass->setUniformBuffer(0, resources->uniformBuffer, 0, uniformBytes_.size());
    renderPass->setVertexBuffer(0, vertexBuffer);
    renderPass->draw(tgfx::PrimitiveType::Triangles, 3);
    renderPass->end();
    return true;
}

}  // namespace motion
