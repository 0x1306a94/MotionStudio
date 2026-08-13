#include "RuntimeFilter.h"

#include <atomic>
#include <cstring>

#include <tgfx/core/Color.h>
#include <tgfx/gpu/GPUBuffer.h>
#include <tgfx/gpu/PixelFormat.h>
#include <tgfx/gpu/Sampler.h>
#include <tgfx/gpu/ShaderModule.h>
#include <tgfx/gpu/ShaderStage.h>
#include <tgfx/gpu/Texture.h>

namespace motion {

namespace {

constexpr char VERTEX_SHADER[] = R"(
in vec2 aPosition;
in vec2 aTextureCoord;
out vec2 vertexColor;
void main() {
  gl_Position = vec4(aPosition.xy, 0, 1);
  vertexColor = aTextureCoord;
}
)";

constexpr char FRAGMENT_SHADER[] = R"(
precision mediump float;
in vec2 vertexColor;
uniform sampler2D sTexture;
out vec4 tgfx_FragColor;

void main() {
    tgfx_FragColor = texture(sTexture, vertexColor);
}
)";

std::string GetVersionPrefix(tgfx::GPU *gpu) {
    auto info = gpu->info();
    const bool isDesktop = info->version.find("OpenGL ES") == std::string::npos;
    return isDesktop ? "#version 150\n\n" : "#version 300 es\n\n";
}

tgfx::Point ToTexturePoint(const tgfx::Texture *source, const tgfx::Point &texturePoint) {
    return {texturePoint.x / static_cast<float>(source->width()),
            texturePoint.y / static_cast<float>(source->height())};
}

tgfx::Point ToVertexPoint(const tgfx::Texture *target, const tgfx::Point &point) {
    return {2.0f * point.x / static_cast<float>(target->width()) - 1.0f,
            2.0f * point.y / static_cast<float>(target->height()) - 1.0f};
}

}  // namespace

uint32_t NextRuntimeFilterType() {
    static std::atomic<uint32_t> next{1};
    uint32_t id = 0;
    do {
        id = next.fetch_add(1, std::memory_order_relaxed);
    } while (id == 0);
    return id;
}

RuntimeFilter::RuntimeFilter(RenderCache *cache, const std::vector<std::shared_ptr<tgfx::Image>> &extraInputs)
    : RuntimeEffect(extraInputs)
    , cache(cache) {
}

tgfx::Rect RuntimeFilter::filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const {
    return filterBounds(srcRect);
}

tgfx::Rect RuntimeFilter::filterBounds(const tgfx::Rect &srcRect) const {
    return srcRect;
}

std::string RuntimeFilter::onBuildVertexShader() const {
    return VERTEX_SHADER;
}

std::string RuntimeFilter::onBuildFragmentShader() const {
    return FRAGMENT_SHADER;
}

int RuntimeFilter::sampleCount() const {
    return 1;
}

std::vector<tgfx::Attribute> RuntimeFilter::vertexAttributes() const {
    return {{"aPosition", tgfx::VertexFormat::Float2}, {"aTextureCoord", tgfx::VertexFormat::Float2}};
}

std::vector<tgfx::BindingEntry> RuntimeFilter::uniformBlocks() const {
    return {};
}

std::vector<tgfx::BindingEntry> RuntimeFilter::textureSamplers() const {
    return {{"sTexture", 0}};
}

size_t RuntimeFilter::vertexCount() const {
    return 4;
}

void RuntimeFilter::onUpdateUniforms(tgfx::RenderPass *, tgfx::GPU *, const std::vector<std::shared_ptr<tgfx::Texture>> &,
                                     const tgfx::Point &) const {
}

void RuntimeFilter::onConfigurePipeline(tgfx::RenderPipelineDescriptor *) const {
}

std::unique_ptr<FilterResources> RuntimeFilter::onCreateFilterResources() const {
    return std::make_unique<FilterResources>();
}

void RuntimeFilter::onConfigureRenderPass(tgfx::RenderPassDescriptor *, FilterResources *, tgfx::GPU *,
                                          const std::shared_ptr<tgfx::Texture> &) const {
}

std::vector<float> RuntimeFilter::computeVertices(const tgfx::Texture *source, const tgfx::Texture *target,
                                                  const tgfx::Point &offset) const {
    auto inputBounds = tgfx::Rect::MakeWH(source->width(), source->height());
    auto targetBounds = filterBounds(inputBounds);
    tgfx::Point contentPoint[4] = {{targetBounds.left, targetBounds.bottom},
                                   {targetBounds.right, targetBounds.bottom},
                                   {targetBounds.left, targetBounds.top},
                                   {targetBounds.right, targetBounds.top}};
    tgfx::Point texturePoints[4] = {{inputBounds.left, inputBounds.bottom},
                                    {inputBounds.right, inputBounds.bottom},
                                    {inputBounds.left, inputBounds.top},
                                    {inputBounds.right, inputBounds.top}};

    std::vector<float> vertices = {};
    vertices.reserve(16);
    for (size_t i = 0; i < 4; i++) {
        auto vertexPoint = ToVertexPoint(target, contentPoint[i] + offset);
        vertices.push_back(vertexPoint.x);
        vertices.push_back(vertexPoint.y);
        auto texturePoint = ToTexturePoint(source, texturePoints[i]);
        vertices.push_back(texturePoint.x);
        vertices.push_back(texturePoint.y);
    }
    return vertices;
}

std::shared_ptr<tgfx::RenderPipeline> RuntimeFilter::createPipeline(tgfx::GPU *gpu) const {
    if (gpu == nullptr) {
        return nullptr;
    }

    tgfx::ShaderModuleDescriptor vertexModule = {};
    vertexModule.code = GetVersionPrefix(gpu) + onBuildVertexShader();
    vertexModule.stage = tgfx::ShaderStage::Vertex;
    auto vertexShader = gpu->createShaderModule(vertexModule);
    if (vertexShader == nullptr) {
        return nullptr;
    }

    tgfx::ShaderModuleDescriptor fragmentModule = {};
    fragmentModule.code = GetVersionPrefix(gpu) + onBuildFragmentShader();
    fragmentModule.stage = tgfx::ShaderStage::Fragment;
    auto fragmentShader = gpu->createShaderModule(fragmentModule);
    if (fragmentShader == nullptr) {
        return nullptr;
    }

    tgfx::RenderPipelineDescriptor descriptor = {};
    tgfx::VertexBufferLayout vertexLayout(vertexAttributes());
    descriptor.vertex.bufferLayouts = {vertexLayout};
    descriptor.vertex.module = vertexShader;
    descriptor.vertex.entryPoint = "main";
    descriptor.fragment.module = fragmentShader;
    descriptor.fragment.entryPoint = "main";
    tgfx::PipelineColorAttachment colorAttachment = {};
    colorAttachment.format = tgfx::PixelFormat::RGBA_8888;
    colorAttachment.blendEnable = true;
    colorAttachment.srcColorBlendFactor = tgfx::BlendFactor::One;
    colorAttachment.dstColorBlendFactor = tgfx::BlendFactor::OneMinusSrcAlpha;
    colorAttachment.srcAlphaBlendFactor = tgfx::BlendFactor::One;
    colorAttachment.dstAlphaBlendFactor = tgfx::BlendFactor::OneMinusSrcAlpha;
    descriptor.fragment.colorAttachments.push_back(colorAttachment);
    descriptor.layout.textureSamplers = textureSamplers();
    descriptor.layout.uniformBlocks = uniformBlocks();
    descriptor.multisample.count = sampleCount();
    onConfigurePipeline(&descriptor);
    return gpu->createRenderPipeline(descriptor);
}

FilterResources *RuntimeFilter::getFilterResources(tgfx::GPU *gpu) const {
    if (cache == nullptr || gpu == nullptr) {
        return nullptr;
    }
    auto type = filterType();
    auto resources = cache->findFilterResources(type);
    if (resources != nullptr) {
        return resources;
    }
    auto pipeline = createPipeline(gpu);
    if (pipeline == nullptr) {
        return nullptr;
    }
    tgfx::SamplerDescriptor samplerDesc(tgfx::AddressMode::ClampToEdge, tgfx::AddressMode::ClampToEdge,
                                        tgfx::FilterMode::Linear, tgfx::FilterMode::Linear, tgfx::MipmapMode::None);
    auto sampler = gpu->createSampler(samplerDesc);
    auto newResources = onCreateFilterResources();
    if (newResources == nullptr) {
        return nullptr;
    }
    newResources->pipeline = std::move(pipeline);
    newResources->sampler = std::move(sampler);
    resources = newResources.get();
    cache->addFilterResources(type, std::move(newResources));
    return resources;
}

bool RuntimeFilter::onDraw(tgfx::CommandEncoder *encoder, const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                           std::shared_ptr<tgfx::Texture> outputTexture, const tgfx::Point &offset) const {
    if (inputTextures.empty() || outputTexture == nullptr || encoder == nullptr) {
        return false;
    }

    auto gpu = encoder->gpu();
    auto resources = getFilterResources(gpu);
    if (resources == nullptr || resources->pipeline == nullptr) {
        return false;
    }

    auto msaaSampleCount = sampleCount();
    std::shared_ptr<tgfx::Texture> renderTexture = nullptr;
    if (msaaSampleCount > 1) {
        tgfx::TextureDescriptor textureDesc(outputTexture->width(), outputTexture->height(), outputTexture->format(),
                                            false, msaaSampleCount, tgfx::TextureUsage::RENDER_ATTACHMENT);
        renderTexture = gpu->createTexture(textureDesc);
        if (renderTexture == nullptr) {
            renderTexture = outputTexture;
            msaaSampleCount = 1;
        }
    } else {
        renderTexture = outputTexture;
    }

    tgfx::RenderPassDescriptor renderPassDesc;
    if (msaaSampleCount > 1) {
        renderPassDesc = tgfx::RenderPassDescriptor(renderTexture, tgfx::LoadAction::Clear, tgfx::StoreAction::Store,
                                                    tgfx::PMColor::Transparent(), outputTexture);
    } else {
        renderPassDesc = tgfx::RenderPassDescriptor(renderTexture, tgfx::LoadAction::Clear, tgfx::StoreAction::Store);
    }
    onConfigureRenderPass(&renderPassDesc, resources, gpu, outputTexture);

    auto renderPass = encoder->beginRenderPass(renderPassDesc);
    if (renderPass == nullptr) {
        return false;
    }

    renderPass->setPipeline(resources->pipeline);

    auto vertices = computeVertices(inputTextures[0].get(), outputTexture.get(), offset);
    auto vertexBuffer = gpu->createBuffer(vertices.size() * sizeof(float), tgfx::GPUBufferUsage::VERTEX);
    if (vertexBuffer == nullptr) {
        renderPass->end();
        return false;
    }

    auto data = vertexBuffer->map();
    if (data == nullptr) {
        renderPass->end();
        return false;
    }
    std::memcpy(data, vertices.data(), vertices.size() * sizeof(float));
    vertexBuffer->unmap();

    renderPass->setVertexBuffer(0, vertexBuffer);
    renderPass->setTexture(0, inputTextures[0], resources->sampler);

    for (size_t i = 1; i < inputTextures.size(); i++) {
        renderPass->setTexture(static_cast<unsigned>(i), inputTextures[i], resources->sampler);
    }

    onUpdateUniforms(renderPass.get(), gpu, inputTextures, offset);

    renderPass->draw(tgfx::PrimitiveType::TriangleStrip, static_cast<uint32_t>(vertexCount()));
    renderPass->end();
    return true;
}

}  // namespace motion
