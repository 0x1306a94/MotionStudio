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
#include "effects/ColorSourceEffect.h"
#include "effects/Uniform.h"

#include <MotionStudio/common/EntityId.h>

#include <tgfx/core/Bitmap.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/EncodedFormat.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/Rect.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/metal/MetalDevice.h>

using motion::ColorSourceEffect;
using motion::EntityId;
using motion::RenderCache;
using motion::Uniform;
using motion::UniformFormat;

namespace {

// Ripple body for ColorSourceEffect. UniformBlock (inputDimsData + rippleCount)
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

// Adapted from Shadertoy "Digital Atavism" by David A Roberts:
// https://www.shadertoy.com/view/Xs3GWj
// ColorSourceEffect entry is vec4 mainImage(vec2 uv); iResolution maps to
// inputDimsData; iTime/aa are uniforms. Loop bound is compile-time AA_MAX;
// aa selects how many of those samples run (default 1 in the test).
constexpr const char *kXs3GWjMainImage = R"GLSL(
#define AA_MAX 4.0
#define PI 3.141592653589793

vec2 CRTCurveUV(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec2 offset = abs(uv.yx) / vec2(6.0, 4.0);
    uv = uv + uv * offset * offset;
    uv = uv * 0.5 + 0.5;
    return uv;
}
void DrawVignette(inout vec3 color, vec2 uv) {
    float vignette = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
    vignette = clamp(pow(16.0 * vignette, 0.3), 0.0, 1.0);
    color *= vignette;
}
void DrawScanline(inout vec3 color, vec2 uv) {
    float scanline = clamp(0.95 + 0.05 * cos(3.14 * (uv.y + 0.008 * iTime) * 240.0), 0.0, 1.0);
    float grille = 0.85 + 0.15 * clamp(1.5 * cos(3.14 * uv.x * 640.0), 0.0, 1.0);
    color *= scanline * grille * 1.2;
}

float atanp(in vec2 p) { return atan(p.y, p.x); }
float cube_root(float x) { return sign(x) * pow(abs(x), 1.0 / 3.0); }
float sq(float x) { return x * x; }

vec3 margarita(in vec2 p) {
    float z = length(p) - 3.5 * atanp(p) + sin(p.x) + cos(p.y);
    if (mod(z, 7.0 * PI) < PI / 2.0) return vec3(1.0, 0.0, 0.0);
    if (mod(z, 1.0 * PI) < PI / 2.0) return vec3(0.0);
    return vec3(1.0);
}

vec3 digital_bacteria(in vec2 p) {
    p /= 4.0;
    float x = sq(sin(p.x) + p.y) + sq(cos(p.y) + p.x);
    float y = cos(10.0 * p.x) + cos(10.0 * p.y) - sin(p.x * p.y);
    float z = sq(sin(floor(p.x)) + floor(p.y)) + sq(cos(floor(p.y)) + floor(p.x));
    if (17.0 < x && x < 21.0 && 17.0 < z && z < 21.0 && y < 0.0)
        return vec3(1.0, 1.0, 85.0 / 256.0);
    if (17.0 < z && z < 21.0) return vec3(85.0 / 256.0, 0.0, 0.0);
    if (17.0 < x && x < 21.0) return vec3(170.0 / 256.0, 170.0 / 256.0, 0.0);
    return vec3(85.0 / 256.0, 85.0 / 256.0, 0.0);
}

vec3 threesome(in vec2 p) {
    p /= 3.0;
    float z = 1.0;
    z *= sin(length(p + vec2(5.0, 0.0))) * cos(8.0 * atanp(p + vec2(5.0, 0.0)));
    z *= sin(length(p - vec2(5.0, 5.0))) * cos(8.0 * atanp(p - vec2(5.0, 5.0)));
    z *= sin(length(p + vec2(0.0, 5.0))) * cos(8.0 * atanp(p + vec2(0.0, 5.0)));
    if ((-0.1 < z && z < 0.0) || 0.2 < z) return vec3(0.0);
    return vec3(1.0);
}

vec3 plaid_meltdown(in vec2 p) {
    p /= 15.0;
    p += 7.0;
    float a = 2.0 * sin(p.x * sin(p.y) + p.y * sin(p.x));
    float b = cube_root(sin(2.5 * sqrt(2.0) * (p.x - p.y)));
    float c = cube_root(sin(2.5 * sqrt(2.0) * (p.x + p.y)));
    float d = sin(80.0 * p.x) + sin(80.0 * p.y);
    if (0.25 * (a + b + c) > 0.5 * d) return vec3(0.0);
    return vec3(1.0);
}

vec3 sunlight_revealed(in vec2 p) {
    p /= 6.0;
    p.x += 2.0;
    float a = length(vec2(3.0 - p.x, p.y)) + abs(p.y) + abs(1.0 - p.x);
    float f = atan(p.y, p.x - 1.0);
    float c = atan(p.y, p.x - 3.0);
    float R = sq(p.x - 1.0) + sq(p.y);
    vec3 col = vec3(0.0);
    bool doMix = false;
    if (5.0 < a && a < 7.0 && mod(f, PI / 7.0) < PI / 14.0) {
        col += vec3(0.0, 82.0 / 256.0, 173.0 / 256.0);
        if (doMix) col /= 2.0;
        doMix = true;
    }
    if (5.0 < a && a < 7.0 && mod(c, PI / 9.0) < PI / 18.0) {
        col += vec3(1.0, 0.0, 0.0);
        if (doMix) col /= 2.0;
        doMix = true;
    }
    if (5.0 < a && a < 7.0 && mod(f, PI / 8.0) < PI / 16.0) {
        col += vec3(1.0, 1.0, 0.0);
        if (doMix) col /= 2.0;
        doMix = true;
    }
    if ((45.0 - 3.0 * p.x) * PI / 180.0 < f && f < (47.0 - p.x) * PI / 180.0 && p.y > 0.1 * p.x
        && mod(log(R) / log(f), 2.0) < 1.0) {
        col += vec3(1.0);
        if (doMix) col /= 2.0;
    }
    return col;
}

vec4 mainImage(vec2 uv) {
    vec2 iResolution = max(inputDimsData, vec2(1.0));
    vec2 fragCoord = uv * iResolution;
    float sampleCount = max(aa, 1.0);
    float t = mod(iTime, 10.0);
    vec3 color = vec3(0.0);
    for (float i = 0.0; i < AA_MAX * AA_MAX - 0.5; i += 1.0) {
        if (i >= sampleCount * sampleCount - 0.5) {
            break;
        }
        vec2 sampleUV = (fragCoord + vec2(floor(i / sampleCount), mod(i, sampleCount)) / sampleCount)
            / iResolution;
        vec2 crtUV = CRTCurveUV(sampleUV);
        if (crtUV.x < 0.0 || crtUV.x > 1.0 || crtUV.y < 0.0 || crtUV.y > 1.0) {
            continue;
        }
        vec2 p = 50.0 * crtUV - 25.0;
        p *= 0.75 + 0.05 * mod(iTime, 10.0);
        p += mod(iTime, 10.0) - 5.0;
        p.x *= iResolution.x / iResolution.y;
        if (t < 2.0 || 8.0 < t) {
            float fade = smoothstep(0.0, 2.0, t) - smoothstep(8.0, 10.0, t);
            float scale = iResolution.y / 50.0 * sampleCount * fade + 1.0;
            p = floor(p * scale) / scale;
        }

        vec3 c;
        float pattern = mod(0.1 * iTime, 5.0);
        if (pattern < 1.0) c = margarita(p);
        else if (pattern < 2.0) c = plaid_meltdown(p);
        else if (pattern < 3.0) c = sunlight_revealed(p);
        else if (pattern < 4.0) c = threesome(p);
        else c = digital_bacteria(p);

        DrawVignette(c, crtUV);
        DrawScanline(c, sampleUV);
        color += c / (sampleCount * sampleCount);
    }
    return vec4(color, 1.0);
}
)GLSL";

// Adapted from Shadertoy "Clouds" by drift:
// https://www.shadertoy.com/view/4tdSWr
// ColorSourceEffect entry is vec4 mainImage(vec2 uv); iResolution maps to
// inputDimsData; iTime is a uniform.
constexpr const char *kCloudsMainImage = R"GLSL(
const float cloudscale = 1.1;
const float speed = 0.03;
const float clouddark = 0.5;
const float cloudlight = 0.3;
const float cloudcover = 0.2;
const float cloudalpha = 8.0;
const float skytint = 0.5;
const vec3 skycolour1 = vec3(0.2, 0.4, 0.6);
const vec3 skycolour2 = vec3(0.4, 0.7, 1.0);

const mat2 m = mat2(1.6, 1.2, -1.2, 1.6);

vec2 hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise(in vec2 p) {
    const float K1 = 0.366025404;
    const float K2 = 0.211324865;
    vec2 i = floor(p + (p.x + p.y) * K1);
    vec2 a = p - i + (i.x + i.y) * K2;
    vec2 o = (a.x > a.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 b = a - o + K2;
    vec2 c = a - 1.0 + 2.0 * K2;
    vec3 h = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    vec3 n = h * h * h * h * vec3(dot(a, hash(i + 0.0)), dot(b, hash(i + o)), dot(c, hash(i + 1.0)));
    return dot(n, vec3(70.0));
}

float fbm(vec2 n) {
    float total = 0.0;
    float amplitude = 0.1;
    for (int i = 0; i < 7; i++) {
        total += noise(n) * amplitude;
        n = m * n;
        amplitude *= 0.4;
    }
    return total;
}

vec4 mainImage(vec2 uv) {
    vec2 iResolution = max(inputDimsData, vec2(1.0));
    vec2 p = uv;
    vec2 sampleUV = p * vec2(iResolution.x / iResolution.y, 1.0);
    float time = iTime * speed;
    float q = fbm(sampleUV * cloudscale * 0.5);

    float r = 0.0;
    sampleUV *= cloudscale;
    sampleUV -= q - time;
    float weight = 0.8;
    for (int i = 0; i < 8; i++) {
        r += abs(weight * noise(sampleUV));
        sampleUV = m * sampleUV + time;
        weight *= 0.7;
    }

    float f = 0.0;
    sampleUV = p * vec2(iResolution.x / iResolution.y, 1.0);
    sampleUV *= cloudscale;
    sampleUV -= q - time;
    weight = 0.7;
    for (int i = 0; i < 8; i++) {
        f += weight * noise(sampleUV);
        sampleUV = m * sampleUV + time;
        weight *= 0.6;
    }

    f *= r + f;

    float c = 0.0;
    time = iTime * speed * 2.0;
    sampleUV = p * vec2(iResolution.x / iResolution.y, 1.0);
    sampleUV *= cloudscale * 2.0;
    sampleUV -= q - time;
    weight = 0.4;
    for (int i = 0; i < 7; i++) {
        c += weight * noise(sampleUV);
        sampleUV = m * sampleUV + time;
        weight *= 0.6;
    }

    float c1 = 0.0;
    time = iTime * speed * 3.0;
    sampleUV = p * vec2(iResolution.x / iResolution.y, 1.0);
    sampleUV *= cloudscale * 3.0;
    sampleUV -= q - time;
    weight = 0.4;
    for (int i = 0; i < 7; i++) {
        c1 += abs(weight * noise(sampleUV));
        sampleUV = m * sampleUV + time;
        weight *= 0.6;
    }

    c += c1;

    vec3 skycolour = mix(skycolour2, skycolour1, p.y);
    vec3 cloudcolour = vec3(1.1, 1.1, 0.9) * clamp(clouddark + cloudlight * c, 0.0, 1.0);

    f = cloudcover + cloudalpha * f * r;

    vec3 result = mix(skycolour, clamp(skytint * skycolour + cloudcolour, 0.0, 1.0), clamp(f + c, 0.0, 1.0));
    return vec4(result, 1.0);
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

TEST(ColorSourceEffectTest, FillsStarPathWithEffect) {
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
    auto effect = ColorSourceEffect::Make(effectId, std::move(uniforms));
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
        const std::string webpPath = OutputPath("ColorSourceEffect_RippleStar_Fill.webp");
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
        const std::string webpPath = OutputPath("ColorSourceEffect_RippleStar_Stroke.webp");
        ASSERT_TRUE(SaveWebp(pixels, kSize, kSize, webpPath))
            << "failed to save " << webpPath;
    }

    env->unlockContext();
}

TEST(ColorSourceEffectTest, RendersShadertoyXs3GWjFrames) {
    constexpr int kWidth = 1600;
    constexpr int kHeight = 900;
    auto env = TestEnvironment::Make(kWidth, kHeight);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->gpu(), nullptr);

    RenderCache cache;
    cache.attachToContext(context);

    auto *canvas = env->surface()->getCanvas();
    const tgfx::Rect fullFrame = tgfx::Rect::MakeWH(static_cast<float>(kWidth), static_cast<float>(kHeight));
    const tgfx::Size sourceSize = tgfx::Size::Make(static_cast<float>(kWidth), static_cast<float>(kHeight));
    tgfx::ImageInfo info = tgfx::ImageInfo::Make(kWidth, kHeight, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);

    const EntityId effectId{2};
    cache.setMainImageSource(effectId, kXs3GWjMainImage);

    std::vector<Uniform> uniforms = {
        {"iTime", UniformFormat::Float},
        {"aa", UniformFormat::Float},
    };
    auto effect = ColorSourceEffect::Make(effectId, std::move(uniforms));
    ASSERT_NE(effect, nullptr);
    effect->prepare(sourceSize, &cache);

    auto *uniformData = effect->getUniformData();
    ASSERT_NE(uniformData, nullptr);
    uniformData->setData("aa", 1.0f);

    auto fillShader = effect->makeImageShader();
    ASSERT_NE(fillShader, nullptr);
    tgfx::Paint paint;
    paint.setShader(fillShader);

    // iTime midpoints for each pattern band of mod(0.1*iTime, 5).
    struct Frame {
        float iTime;
        const char *name;
    };
    const Frame frames[] = {
        {5.0f, "margarita"},
        {15.0f, "plaid_meltdown"},
        {25.0f, "sunlight_revealed"},
        {35.0f, "threesome"},
        {45.0f, "digital_bacteria"},
    };

    for (const auto &frame : frames) {
        uniformData->setData("iTime", frame.iTime);

        canvas->clear();
        canvas->drawRect(fullFrame, paint);

        std::vector<uint8_t> pixels(static_cast<size_t>(info.rowBytes() * info.height()));
        ASSERT_TRUE(env->surface()->readPixels(info, pixels.data())) << "readPixels failed for " << frame.name;
        const std::string webpPath = OutputPath(std::string("ColorSourceEffect_Xs3GWj_") + frame.name + ".webp");
        ASSERT_TRUE(SaveWebp(pixels, kWidth, kHeight, webpPath)) << "failed to save " << webpPath;
    }

    env->unlockContext();
}

TEST(ColorSourceEffectTest, RendersShadertoyCloudsFrames) {
    constexpr int kWidth = 1600;
    constexpr int kHeight = 900;
    auto env = TestEnvironment::Make(kWidth, kHeight);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->gpu(), nullptr);

    RenderCache cache;
    cache.attachToContext(context);

    auto *canvas = env->surface()->getCanvas();
    const tgfx::Rect fullFrame = tgfx::Rect::MakeWH(static_cast<float>(kWidth), static_cast<float>(kHeight));
    const tgfx::Size sourceSize = tgfx::Size::Make(static_cast<float>(kWidth), static_cast<float>(kHeight));
    tgfx::ImageInfo info = tgfx::ImageInfo::Make(kWidth, kHeight, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);

    const EntityId effectId{3};
    cache.setMainImageSource(effectId, kCloudsMainImage);

    std::vector<Uniform> uniforms = {
        {"iTime", UniformFormat::Float},
    };
    auto effect = ColorSourceEffect::Make(effectId, std::move(uniforms));
    ASSERT_NE(effect, nullptr);
    effect->prepare(sourceSize, &cache);

    auto *uniformData = effect->getUniformData();
    ASSERT_NE(uniformData, nullptr);

    auto fillShader = effect->makeImageShader();
    ASSERT_NE(fillShader, nullptr);
    tgfx::Paint paint;
    paint.setShader(fillShader);

    struct Frame {
        float iTime;
        const char *name;
    };
    const Frame frames[] = {
        {0.0f, "t0"},
        {10.0f, "t10"},
        {30.0f, "t30"},
        {60.0f, "t60"},
    };

    for (const auto &frame : frames) {
        uniformData->setData("iTime", frame.iTime);

        canvas->clear();
        canvas->drawRect(fullFrame, paint);

        std::vector<uint8_t> pixels(static_cast<size_t>(info.rowBytes() * info.height()));
        ASSERT_TRUE(env->surface()->readPixels(info, pixels.data())) << "readPixels failed for " << frame.name;
        const std::string webpPath = OutputPath(std::string("ColorSourceEffect_Clouds_") + frame.name + ".webp");
        ASSERT_TRUE(SaveWebp(pixels, kWidth, kHeight, webpPath)) << "failed to save " << webpPath;
    }

    env->unlockContext();
}
