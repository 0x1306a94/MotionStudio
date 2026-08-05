#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#import <Metal/Metal.h>

#include <gtest/gtest.h>

#include <tgfx/core/Bitmap.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Data.h>
#include <tgfx/core/EncodedFormat.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/ImageFilter.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/Point.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/Shader.h>
#include <tgfx/core/Surface.h>
#include <tgfx/core/TileMode.h>
#include <tgfx/gpu/CommandEncoder.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>
#include <tgfx/gpu/RenderPass.h>
#include <tgfx/gpu/RenderPipeline.h>
#include <tgfx/gpu/RuntimeEffect.h>
#include <tgfx/gpu/ShaderModule.h>
#include <tgfx/gpu/ShaderStage.h>
#include <tgfx/gpu/metal/MetalDevice.h>

namespace {

// Fixed fullscreen-triangle vertex shader. Three vertices are placed at
// clip-space (-1,-1), (3,-1), (-1,3) so the visible [-1,1]^2 viewport is fully
// covered by one triangle. uv is derived from position, so no second vertex
// buffer is needed. gl_VertexIndex selects the vertex; SPIRV-Cross maps it to
// Metal's [[vertex_id]], letting us draw three vertices with no buffer bound.
constexpr const char *kVertexShader = R"GLSL(
#version 450
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 pos = vec2(gl_VertexIndex == 1 ? 3.0 : -1.0,
                    gl_VertexIndex == 2 ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
    v_uv = pos * 0.5 + 0.5;
}
)GLSL";

// Ripple fragment, ported from the WGSL water-ripple effect. The fixed vertex
// shader already supplies v_uv at location 0, so only the fragment is ported.
// WGSL vec2f/vec3f/vec4f map to GLSL vec2/vec3/vec4; the single std140 uniform
// block is auto-assigned to binding 0 by tgfx's GLSL preprocessor.
constexpr const char *kFragmentShader = R"GLSL(
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(std140) uniform Uniforms {
    vec4 frameData;
    vec4 inputDimsData;
    vec4 rippleCount;
} u;
void main() {
    vec2 inputDims = max(u.inputDimsData.xy, vec2(1.0));
    float aspect = inputDims.x / max(inputDims.y, 1.0);
    float rippleCount = u.rippleCount.x;

    vec2 p = (v_uv - vec2(0.5)) * vec2(aspect, 1.0);
    float dist = length(p);

    float wave = sin(dist * rippleCount * 6.2831853) * 0.5 + 0.5;
    float falloff = 1.0 - smoothstep(0.3, 0.7, dist);

    vec3 deepBlue = vec3(0.04, 0.18, 0.55);
    vec3 midBlue  = vec3(0.15, 0.55, 0.9);
    vec3 white    = vec3(0.85, 0.95, 1.0);

    vec3 color;
    if (wave < 0.5) {
        color = mix(deepBlue, midBlue, wave * 2.0);
    } else {
        color = mix(midBlue, white, (wave - 0.5) * 2.0);
    }
    color = mix(deepBlue, color, falloff);

    fragColor = vec4(color, 1.0);
}
)GLSL";

struct Pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

Pixel PixelAt(const std::vector<uint8_t> &pixels, int width, int x, int y) {
    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

// Standalone tgfx Metal environment modeled on TgfxRenderAdapter::RecreateTarget:
// owns the Metal device and a Surface with a readable RGBA8 target. The tgfx
// context lock is non-reentrant, so callers hold it for the whole draw+read via
// lockContext/unlockContext rather than nesting per-operation locks.
class TestEnvironment {
  public:
    static std::unique_ptr<TestEnvironment> Make(int width, int height) {
        auto env = std::unique_ptr<TestEnvironment>(new TestEnvironment());
        env->mtlDevice_ = MTLCreateSystemDefaultDevice();
        if (!env->mtlDevice_) {
            return nullptr;
        }
        env->device_ = tgfx::MetalDevice::MakeFrom((__bridge void *)env->mtlDevice_);
        if (!env->device_) {
            return nullptr;
        }
        auto *context = env->device_->lockContext();
        if (!context) {
            return nullptr;
        }
        env->surface_ = tgfx::Surface::Make(context, width, height, tgfx::ColorType::RGBA_8888);
        env->device_->unlock();
        if (!env->surface_) {
            return nullptr;
        }
        env->width_ = width;
        env->height_ = height;
        return env;
    }

    // Locks the context for a draw+read batch. Returns nullptr if the device
    // is lost; caller must pair with unlockContext().
    tgfx::Context *lockContext() {
        return device_->lockContext();
    }

    void unlockContext() {
        device_->unlock();
    }

    tgfx::Surface *surface() {
        return surface_.get();
    }

  private:
    id<MTLDevice> mtlDevice_ = nil;
    std::shared_ptr<tgfx::Device> device_;
    std::shared_ptr<tgfx::Surface> surface_;
    int width_ = 0;
    int height_ = 0;
};

// RuntimeShader: tgfx::RuntimeEffect subclass implementing the "fixed vertex +
// user fragment + uniform" shell from tgfx discussion #1529. The render
// pipeline is built once at construction; onDraw only uploads the uniform
// buffer and issues a single fullscreen-triangle draw into the output texture
// tgfx provides. tgfx owns output-texture allocation and command submission.
class RuntimeShader : public tgfx::RuntimeEffect {
  public:
    RuntimeShader(tgfx::GPU *gpu, int width, int height)
        : width_(width)
        , height_(height)
        , uniformBytes_(48, 0) {
        tgfx::ShaderModuleDescriptor vertexDesc;
        vertexDesc.code = kVertexShader;
        vertexDesc.stage = tgfx::ShaderStage::Vertex;
        auto vertexModule = gpu->createShaderModule(vertexDesc);

        tgfx::ShaderModuleDescriptor fragmentDesc;
        fragmentDesc.code = kFragmentShader;
        fragmentDesc.stage = tgfx::ShaderStage::Fragment;
        auto fragmentModule = gpu->createShaderModule(fragmentDesc);

        tgfx::RenderPipelineDescriptor pipelineDesc;
        pipelineDesc.vertex.module = vertexModule;
        pipelineDesc.vertex.entryPoint = "main";
        pipelineDesc.fragment.module = fragmentModule;
        pipelineDesc.fragment.entryPoint = "main";
        auto &colorAttachment = pipelineDesc.fragment.colorAttachments.emplace_back();
        colorAttachment.format = tgfx::PixelFormat::RGBA_8888;
        colorAttachment.blendEnable = false;
        pipeline_ = gpu->createRenderPipeline(pipelineDesc);
    }

    bool ready() const {
        return pipeline_ != nullptr;
    }

    // Sets the three-vec4 uniform block (frameData, inputDimsData, rippleCount)
    // matching the GLSL Uniforms block above. Each array is one vec4 (16 bytes).
    void setUniforms(const std::array<float, 4> &frameData,
                     const std::array<float, 4> &inputDimsData,
                     const std::array<float, 4> &rippleCount) {
        size_t offset = 0;
        std::memcpy(uniformBytes_.data() + offset, frameData.data(), 16);
        offset += 16;
        std::memcpy(uniformBytes_.data() + offset, inputDimsData.data(), 16);
        offset += 16;
        std::memcpy(uniformBytes_.data() + offset, rippleCount.data(), 16);
    }

  private:
    tgfx::Rect filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const override {
        return tgfx::Rect::MakeWH(static_cast<float>(width_), static_cast<float>(height_));
    }

    bool onDraw(tgfx::CommandEncoder *encoder,
                const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                std::shared_ptr<tgfx::Texture> outputTexture, const tgfx::Point &offset) const override {
        auto *gpu = encoder->gpu();
        auto uniformBuffer = gpu->createBuffer(uniformBytes_.size(), tgfx::GPUBufferUsage::UNIFORM);
        if (!uniformBuffer) {
            return false;
        }
        void *mapped = uniformBuffer->map();
        if (!mapped) {
            return false;
        }
        std::memcpy(mapped, uniformBytes_.data(), uniformBytes_.size());
        uniformBuffer->unmap();

        tgfx::RenderPassDescriptor passDescriptor(outputTexture, tgfx::LoadAction::Clear,
                                                  tgfx::StoreAction::Store);
        auto renderPass = encoder->beginRenderPass(passDescriptor);
        if (!renderPass) {
            return false;
        }
        renderPass->setPipeline(pipeline_);
        renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformBytes_.size());
        renderPass->draw(tgfx::PrimitiveType::Triangles, 3);
        renderPass->end();
        return true;
    }

    int width_;
    int height_;
    std::shared_ptr<tgfx::RenderPipeline> pipeline_;
    std::vector<uint8_t> uniformBytes_;
};

std::shared_ptr<tgfx::Image> MakePlaceholderImage() {
    // 1x1 transparent source; RuntimeShader ignores it and emits its own
    // colors, but makeWithFilter requires a non-null source image.
    tgfx::ImageInfo info = tgfx::ImageInfo::Make(1, 1, tgfx::ColorType::RGBA_8888,
                                                 tgfx::AlphaType::Premultiplied);
    std::vector<uint8_t> pixels(4, 0);
    auto data = tgfx::Data::MakeWithCopy(pixels.data(), pixels.size());
    return tgfx::Image::MakeFrom(info, data);
}

// Builds a five-pointed star path centered at (cx, cy) with alternating outer
// and inner vertices, starting with the top point.
tgfx::Path MakeFivePointStar(float cx, float cy, float outerRadius, float innerRadius) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr int pointCount = 5;
    tgfx::Path path;
    for (int i = 0; i < pointCount * 2; ++i) {
        const float angle = -kPi * 0.5f + i * (kPi / pointCount);
        const float radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const float x = cx + radius * std::cos(angle);
        const float y = cy + radius * std::sin(angle);
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    path.close();
    return path;
}

// Encodes RGBA8 premultiplied pixels to a webp file at path. Uses tgfx's own
// Bitmap encode, which handles color-type conversion to its native layout.
bool SaveWebp(const std::vector<uint8_t> &rgba, int width, int height, const std::string &path) {
    tgfx::Bitmap bitmap(width, height, false, false);
    if (bitmap.isEmpty()) {
        return false;
    }
    auto info = tgfx::ImageInfo::Make(width, height, tgfx::ColorType::RGBA_8888,
                                      tgfx::AlphaType::Premultiplied);
    if (!bitmap.writePixels(info, rgba.data())) {
        return false;
    }
    auto data = bitmap.encode(tgfx::EncodedFormat::WEBP, 100);
    if (!data) {
        return false;
    }
    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(data->data()),
              static_cast<std::streamsize>(data->size()));
    return out.good();
}

std::string OutputPath(const std::string &fileName) {
    return (std::filesystem::path(__FILE__).parent_path() / "out" / fileName).string();
}

}  // namespace

TEST(RuntimeShaderTest, FillsStarPathWithShader) {
    constexpr int kSize = 256;
    auto env = TestEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);
    auto *gpu = context->gpu();
    ASSERT_NE(gpu, nullptr);

    auto shader = std::make_shared<RuntimeShader>(gpu, kSize, kSize);
    ASSERT_TRUE(shader->ready()) << "pipeline creation failed";
    // frameData = (time, dimsX, dimsY, _); inputDimsData = (inputW, inputH, _, _);
    // rippleCount = (ring count, _, _, _). Square input so aspect = 1.
    shader->setUniforms({0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f},
                        {static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 0.0f},
                        {5.0f, 0.0f, 0.0f, 0.0f});

    auto source = MakePlaceholderImage();
    ASSERT_NE(source, nullptr);
    tgfx::Point offset{};
    auto filtered = source->makeWithFilter(
        tgfx::ImageFilter::Runtime(shader), &offset);
    ASSERT_NE(filtered, nullptr);

    auto fillShader = tgfx::Shader::MakeImageShader(filtered, tgfx::TileMode::Clamp,
                                                    tgfx::TileMode::Clamp);
    ASSERT_NE(fillShader, nullptr);
    tgfx::Paint paint;
    paint.setShader(fillShader);

    auto star = MakeFivePointStar(static_cast<float>(kSize) * 0.5f,
                                  static_cast<float>(kSize) * 0.5f, 110.0f, 45.0f);
    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawPath(star, paint);

    tgfx::ImageInfo info = tgfx::ImageInfo::Make(kSize, kSize, tgfx::ColorType::RGBA_8888,
                                                 tgfx::AlphaType::Premultiplied);
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(info.rowBytes() * info.height()));
        ASSERT_TRUE(env->surface()->readPixels(info, pixels.data()));
        const std::string webpPath = OutputPath("RuntimeShader_RippleStar_Fill.webp");
        ASSERT_TRUE(SaveWebp(pixels, kSize, kSize, webpPath))
            << "failed to save " << webpPath;
    }

    paint.setStyle(tgfx::PaintStyle::Stroke);
    paint.setStrokeWidth(10);
    canvas->clear();
    canvas->drawPath(star, paint);
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(info.rowBytes() * info.height()));
        ASSERT_TRUE(env->surface()->readPixels(info, pixels.data()));
        const std::string webpPath = OutputPath("RuntimeShader_RippleStar_Stroke.webp");
        ASSERT_TRUE(SaveWebp(pixels, kSize, kSize, webpPath))
            << "failed to save " << webpPath;
    }

    env->unlockContext();
}
