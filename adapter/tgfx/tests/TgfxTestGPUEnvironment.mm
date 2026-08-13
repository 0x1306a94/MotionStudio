#include "TgfxTestGPUEnvironment.h"

#include <cmath>
#include <filesystem>
#include <fstream>

#import <Metal/Metal.h>

#include <tgfx/core/Bitmap.h>
#include <tgfx/core/Canvas.h>
#include <tgfx/core/EncodedFormat.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/gpu/Device.h>
#include <tgfx/gpu/metal/MetalDevice.h>

namespace tgfx_test {

struct TgfxTestGPUEnvironment::Data {
    id<MTLDevice> mtlDevice = nil;
    std::shared_ptr<tgfx::Device> device = nullptr;
    std::shared_ptr<tgfx::Surface> surface = nullptr;
};

TgfxTestGPUEnvironment::TgfxTestGPUEnvironment()
    : data_(std::make_unique<Data>()) {
}

TgfxTestGPUEnvironment::~TgfxTestGPUEnvironment() = default;

std::unique_ptr<TgfxTestGPUEnvironment> TgfxTestGPUEnvironment::Make(int width, int height) {
    auto env = std::unique_ptr<TgfxTestGPUEnvironment>(new TgfxTestGPUEnvironment());
    env->data_->mtlDevice = MTLCreateSystemDefaultDevice();
    if (!env->data_->mtlDevice) {
        return nullptr;
    }
    env->data_->device = tgfx::MetalDevice::MakeFrom((__bridge void *)env->data_->mtlDevice);
    if (!env->data_->device) {
        return nullptr;
    }
    auto *context = env->data_->device->lockContext();
    if (!context) {
        return nullptr;
    }
    env->data_->surface = tgfx::Surface::Make(context, width, height, tgfx::ColorType::RGBA_8888);
    env->data_->device->unlock();
    if (!env->data_->surface) {
        return nullptr;
    }
    return env;
}

tgfx::Context *TgfxTestGPUEnvironment::lockContext() {
    return data_->device->lockContext();
}

void TgfxTestGPUEnvironment::unlockContext() {
    data_->device->unlock();
}

tgfx::Surface *TgfxTestGPUEnvironment::surface() {
    return data_->surface.get();
}

Pixel PixelAt(const std::vector<uint8_t> &pixels, int width, int x, int y) {
    const size_t offset =
        (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

int ChannelDelta(uint8_t a, uint8_t b) {
    return std::abs(static_cast<int>(a) - static_cast<int>(b));
}

std::shared_ptr<tgfx::Image> MakeSolidImage(tgfx::Context *context, int size, tgfx::Color color) {
    auto surface = tgfx::Surface::Make(context, size, size, tgfx::ColorType::RGBA_8888);
    if (surface == nullptr) {
        return nullptr;
    }
    surface->getCanvas()->clear(color);
    return surface->makeImageSnapshot();
}

bool ReadCenter(tgfx::Surface *surface, int size, Pixel *out) {
    tgfx::ImageInfo info =
        tgfx::ImageInfo::Make(size, size, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);
    std::vector<uint8_t> pixels(static_cast<size_t>(info.rowBytes() * info.height()));
    if (!surface->readPixels(info, pixels.data())) {
        return false;
    }
    *out = PixelAt(pixels, size, size / 2, size / 2);
    return true;
}

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

}  // namespace tgfx_test
