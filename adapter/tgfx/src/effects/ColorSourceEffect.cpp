#include "ColorSourceEffect.h"

#include "RenderCache.h"
#include "UniformData.h"

#include <cstring>

#include <tgfx/core/Data.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/ImageFilter.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Point.h>
#include <tgfx/core/Shader.h>
#include <tgfx/core/TileMode.h>
#include <tgfx/gpu/Attribute.h>
#include <tgfx/gpu/CommandEncoder.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>
#include <tgfx/gpu/PixelFormat.h>
#include <tgfx/gpu/RenderPass.h>
#include <tgfx/gpu/RenderPipeline.h>
#include <tgfx/gpu/RuntimeEffect.h>
#include <tgfx/gpu/ShaderModule.h>
#include <tgfx/gpu/ShaderStage.h>
#include <tgfx/gpu/Texture.h>
#include <tgfx/platform/Print.h>

namespace motion {

namespace {

bool HasUniform(const std::vector<Uniform> &uniforms, const std::string &name) {
    for (const auto &uniform : uniforms) {
        if (uniform.name() == name) {
            return true;
        }
    }
    return false;
}

void PrependShadertoyBuiltinUniforms(std::vector<Uniform> &uniforms) {
    std::vector<Uniform> builtins;
    auto add = [&](const char *name, UniformFormat format, int count = 1) {
        if (!HasUniform(uniforms, name)) {
            builtins.emplace_back(name, format, count);
        }
    };

    add("iResolution", UniformFormat::Float3);
    add("iTime", UniformFormat::Float);
    add("iTimeDelta", UniformFormat::Float);
    add("iFrameRate", UniformFormat::Float);
    add("iFrame", UniformFormat::Int);
    // tgfx RuntimeImageFilter may allocate a clip subset RT; these map v_uv back
    // into the logical sourceBounds UV space (see onDraw).
    add("iRenderOrigin", UniformFormat::Float2);
    add("iRenderSize", UniformFormat::Float2);

    uniforms.insert(uniforms.begin(), builtins.begin(), builtins.end());
}

void WriteShadertoyBuiltinUniforms(UniformData *uniformData, const tgfx::Rect &sourceBounds, const ColorSourceFrameContext &frameContext, float timeDelta) {
    const float resolution[3] = {sourceBounds.width(), sourceBounds.height(), 1.f};
    uniformData->setData("iResolution", resolution, sizeof(resolution));
    uniformData->setData("iTime", frameContext.timeSeconds);
    uniformData->setData("iTimeDelta", timeDelta);

    const int32_t frame = static_cast<int32_t>(frameContext.frameIndex);
    uniformData->setData("iFrameRate", frameContext.frameRate);
    uniformData->setData("iFrame", frame);
}

void WriteRenderSubsetUniforms(UniformData *uniformData, const tgfx::Point &offset, const tgfx::Texture &outputTexture) {
    const float origin[2] = {-offset.x, -offset.y};
    const float size[2] = {static_cast<float>(outputTexture.width()), static_cast<float>(outputTexture.height())};
    uniformData->setData("iRenderOrigin", origin, sizeof(origin));
    uniformData->setData("iRenderSize", size, sizeof(size));
}

}  // namespace

// Fullscreen triangle driven by aPosition (GLES-portable). Three clip-space
// vertices cover [-1,1]^2; uv is derived from position. Avoid gl_VertexIndex —
// RuntimeEffect shaders are GLES-style and GL backends compile them as-is.
constexpr const char *VERTEX_SHADER = R"GLSL(
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
    return GetVersionPrefix(gpu) + VERTEX_SHADER;
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

    uniformBlock += "};\n\n";

    // Map the (possibly clipped) output RT back into full-image UV so a subset
    // pass does not squeeze the whole mainImage into the visible strip.
    std::string footer = R"GLSL(
void main() {
    vec2 dims = max(iResolution.xy, vec2(1.0));
    vec2 uv = (iRenderOrigin + v_uv * iRenderSize) / dims;
    out_fragColor = mainImage(uv);
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

std::shared_ptr<ColorSourceEffect> ColorSourceEffect::Make(EntityId shaderId, std::string mainImage, std::vector<Uniform> uniforms, const tgfx::Rect &sourceBounds, RenderCache *cache) {
    if (!shaderId.isValid()) {
        return nullptr;
    }
    return std::shared_ptr<ColorSourceEffect>(new ColorSourceEffect(shaderId, std::move(mainImage), std::move(uniforms), sourceBounds, cache));
}

ColorSourceEffect::ColorSourceEffect(EntityId shaderId, std::string mainImage, std::vector<Uniform> uniforms, tgfx::Rect sourceBounds, RenderCache *cache)
    : shaderId_(shaderId)
    , mainImage_(std::move(mainImage))
    , sourceBounds_(sourceBounds)
    , cache_(cache) {
    std::vector<Uniform> finalUniforms = std::move(uniforms);
    PrependShadertoyBuiltinUniforms(finalUniforms);
    uniformData_ = std::make_unique<UniformData>(std::move(finalUniforms));
    uniformBytes_.assign(uniformData_->size(), 0);
    if (!uniformBytes_.empty()) {
        uniformData_->setBuffer(uniformBytes_.data());
    }
}

void ColorSourceEffect::setFrameContext(ColorSourceFrameContext frameContext) {
    frameContext_ = frameContext;
}

UniformData *ColorSourceEffect::getUniformData() const {
    return uniformData_.get();
}

bool ColorSourceEffect::preparePipeline() {
    if (cache_ == nullptr || cache_->getContext() == nullptr) {
        return false;
    }
    return getOrCreatePipeline(cache_->getContext()->gpu()) != nullptr;
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

    auto shader = tgfx::Shader::MakeImageShader(filtered, tgfx::TileMode::Clamp, tgfx::TileMode::Clamp);
    if (shader == nullptr) {
        return nullptr;
    }
    if (sourceBounds_.left != 0.0f || sourceBounds_.top != 0.0f) {
        shader = shader->makeWithMatrix(tgfx::Matrix::MakeTrans(sourceBounds_.left, sourceBounds_.top));
    }
    return shader;
}

std::shared_ptr<tgfx::RenderPipeline> ColorSourceEffect::createPipeline(tgfx::GPU *gpu) const {
    if (cache_ == nullptr || uniformData_ == nullptr || gpu == nullptr || mainImage_.empty()) {
        return nullptr;
    }

    auto vertexCode = BuildVertexShaderSource(gpu);
    auto fragmentCode = BuildFragmentShaderSource(gpu, uniformData_->uniforms(), mainImage_);

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
#if DEBUG
    tgfx::PrintLog("shaderId: %llu\nvertex shader:\n%s\nfragment shader:\n%s\n", shaderId_, vertexDesc.code.c_str(), fragmentDesc.code.c_str());
#endif
    tgfx::RenderPipelineDescriptor pipelineDesc;
    tgfx::VertexBufferLayout vertexLayout({{"aPosition", tgfx::VertexFormat::Float2}});
    pipelineDesc.vertex.bufferLayouts = {vertexLayout};
    pipelineDesc.vertex.module = vertexModule;
    pipelineDesc.vertex.entryPoint = "main";
    pipelineDesc.fragment.module = fragmentModule;
    pipelineDesc.fragment.entryPoint = "main";
    tgfx::PipelineColorAttachment colorAttachment = {};
    colorAttachment.format = tgfx::PixelFormat::RGBA_8888;
    colorAttachment.blendEnable = true;
    colorAttachment.srcColorBlendFactor = tgfx::BlendFactor::One;
    colorAttachment.dstColorBlendFactor = tgfx::BlendFactor::OneMinusSrcAlpha;
    colorAttachment.srcAlphaBlendFactor = tgfx::BlendFactor::One;
    colorAttachment.dstAlphaBlendFactor = tgfx::BlendFactor::OneMinusSrcAlpha;
    pipelineDesc.fragment.colorAttachments.push_back(colorAttachment);
    return gpu->createRenderPipeline(pipelineDesc);
}

std::shared_ptr<tgfx::RenderPipeline> ColorSourceEffect::getOrCreatePipeline(tgfx::GPU *gpu) const {
    if (cache_ == nullptr || uniformData_ == nullptr || mainImage_.empty() || !shaderId_.isValid()) {
        return nullptr;
    }

    auto pipeline = cache_->findColorSourcePipeline(shaderId_);
    if (pipeline != nullptr) {
        return pipeline;
    }

    pipeline = createPipeline(gpu);
    if (pipeline == nullptr) {
        return nullptr;
    }
    cache_->addColorSourcePipeline(shaderId_, pipeline);
    return pipeline;
}

tgfx::Rect ColorSourceEffect::filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const {
    return tgfx::Rect::MakeWH(sourceBounds_.width(), sourceBounds_.height());
}

bool ColorSourceEffect::onDraw(tgfx::CommandEncoder *encoder, const std::vector<std::shared_ptr<tgfx::Texture>> & /*inputTextures*/, std::shared_ptr<tgfx::Texture> outputTexture, const tgfx::Point &offset) const {
    if (sourceBounds_.isEmpty() || cache_ == nullptr || uniformData_ == nullptr || outputTexture == nullptr || encoder == nullptr) {
        return false;
    }

    auto *gpu = encoder->gpu();
    auto pipeline = getOrCreatePipeline(gpu);
    if (pipeline == nullptr) {
        return false;
    }

    auto vertexBuffer = cache_->getFullscreenVertexBuffer(gpu);
    if (vertexBuffer == nullptr) {
        return false;
    }

    float timeDelta = 0.f;
    if (hasPreviousTime_) {
        timeDelta = frameContext_.timeSeconds - lastTimeSeconds_;
    } else if (frameContext_.frameRate > 0.f) {
        timeDelta = 1.f / frameContext_.frameRate;
    }
    lastTimeSeconds_ = frameContext_.timeSeconds;
    hasPreviousTime_ = true;
    WriteShadertoyBuiltinUniforms(uniformData_.get(), sourceBounds_, frameContext_, timeDelta);
    WriteRenderSubsetUniforms(uniformData_.get(), offset, *outputTexture);

    auto slice = cache_->acquireUniformSlice(uniformBytes_.size());
    if (slice.buffer == nullptr) {
        return false;
    }

    void *mapped = slice.buffer->map(slice.offset, uniformBytes_.size());
    if (mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, uniformBytes_.data(), uniformBytes_.size());
    slice.buffer->unmap();

    tgfx::RenderPassDescriptor passDescriptor(outputTexture, tgfx::LoadAction::Clear, tgfx::StoreAction::Store);
    auto renderPass = encoder->beginRenderPass(passDescriptor);
    if (renderPass == nullptr) {
        return false;
    }
    renderPass->setPipeline(pipeline);
    renderPass->setUniformBuffer(0, slice.buffer, slice.offset, uniformBytes_.size());
    renderPass->setVertexBuffer(0, vertexBuffer);
    renderPass->draw(tgfx::PrimitiveType::Triangles, 3);
    renderPass->end();
    return true;
}

}  // namespace motion
