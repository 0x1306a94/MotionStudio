#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#import <Metal/Metal.h>

#include <gtest/gtest.h>

#include "RenderCache.h"
#include "effects/CustomColorEffect.h"
#include "effects/Uniform.h"

#include <MotionStudio/common/EntityId.h>

#include <tgfx/core/Bitmap.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/EncodedFormat.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/metal/MetalDevice.h>

using motion::CustomColorEffect;
using motion::EntityId;
using motion::RenderCache;
using motion::Uniform;
using motion::UniformFormat;

namespace {

// Ripple body for CustomColorEffect. UniformBlock (inputDimsData + rippleCount)
// is injected by BuildFragmentShaderSource; this only supplies the mainImage(uv)
// entry used by the generated main().
constexpr const char *kMainImage = R"GLSL(
vec4 mainImage(vec2 uv) {
    vec2 inputDims = max(inputDimsData, vec2(1.0));
    float aspect = inputDims.x / max(inputDims.y, 1.0);

    vec2 p = (uv - vec2(0.5)) * vec2(aspect, 1.0);
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

    return vec4(color, 1.0);
}
)GLSL";

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
    auto info = tgfx::ImageInfo::Make(width, height, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);
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
    out.write(reinterpret_cast<const char *>(data->data()), static_cast<std::streamsize>(data->size()));
    return out.good();
}

std::string OutputPath(const std::string &fileName) {
    return (std::filesystem::path(__FILE__).parent_path() / "out" / fileName).string();
}

}  // namespace

TEST(CustomColorEffectTest, FillsStarPathWithEffect) {
    constexpr int kSize = 256;
    auto env = TestEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->gpu(), nullptr);

    auto star = MakeFivePointStar(static_cast<float>(kSize) * 0.5f, static_cast<float>(kSize) * 0.5f, 110.0f, 45.0f);

    const EntityId effectId{1};
    RenderCache cache;
    cache.attachToContext(context);
    cache.setMainImageSource(effectId, kMainImage);

    std::vector<Uniform> uniforms = {
        {"rippleCount", UniformFormat::Float},
    };
    auto effect = CustomColorEffect::Make(effectId, std::move(uniforms));
    ASSERT_NE(effect, nullptr);
    effect->prepare(star.getBounds().size(), &cache);

    auto *uniformData = effect->getUniformData();
    ASSERT_NE(uniformData, nullptr);
    const float rippleCount = 5.0f;
    uniformData->setData("rippleCount", rippleCount);

    auto fillShader = effect->makeImageShader();
    ASSERT_NE(fillShader, nullptr);
    tgfx::Paint paint;
    paint.setShader(fillShader);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawPath(star, paint);

    tgfx::ImageInfo info = tgfx::ImageInfo::Make(kSize, kSize, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(info.rowBytes() * info.height()));
        ASSERT_TRUE(env->surface()->readPixels(info, pixels.data()));
        const std::string webpPath = OutputPath("CustomColorEffect_RippleStar_Fill.webp");
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
        const std::string webpPath = OutputPath("CustomColorEffect_RippleStar_Stroke.webp");
        ASSERT_TRUE(SaveWebp(pixels, kSize, kSize, webpPath))
            << "failed to save " << webpPath;
    }

    env->unlockContext();
}
