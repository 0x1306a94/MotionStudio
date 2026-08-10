#include "TgfxRenderAdapter.h"

#import <Metal/Metal.h>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Backend.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/ImageOrigin.h>
#include <tgfx/gpu/metal/MetalDevice.h>
#include <tgfx/gpu/metal/MetalTypes.h>

namespace motion {

struct TgfxRenderAdapter::Impl {
    id<MTLDevice> mtlDevice;
    id<MTLTexture> texture;
    int width = 0;
    int height = 0;
};

TgfxRenderAdapter::TgfxRenderAdapter()
    : impl_(std::make_unique<Impl>()) {
}

TgfxRenderAdapter::~TgfxRenderAdapter() = default;

std::unique_ptr<TgfxRenderAdapter> TgfxRenderAdapter::Make(int width, int height) {
    auto adapter = std::unique_ptr<TgfxRenderAdapter>(new TgfxRenderAdapter());
    adapter->impl_->mtlDevice = MTLCreateSystemDefaultDevice();
    if (!adapter->impl_->mtlDevice) {
        return nullptr;
    }
    // tgfx takes the id<MTLDevice> itself as void*, not a pointer to the id.
    adapter->device_ = tgfx::MetalDevice::MakeFrom((__bridge void *)adapter->impl_->mtlDevice);
    if (!adapter->device_) {
        return nullptr;
    }
    if (!adapter->RecreateTarget(width, height)) {
        return nullptr;
    }
    return adapter;
}

bool TgfxRenderAdapter::RecreateTarget(int width, int height) {
    auto *context = device_->lockContext();
    if (!context) {
        return false;
    }
    // Drop PathRef / mask caches and purge GPU resources before the new target.
    releaseGpuCaches(context);
    impl_->texture = nil;

    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:static_cast<size_t>(width) height:static_cast<size_t>(height) mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
    impl_->texture = [impl_->mtlDevice newTextureWithDescriptor:descriptor];

    // The texture field holds the id<MTLTexture> itself (tgfx bridges it back).
    tgfx::MetalTextureInfo textureInfo;
    textureInfo.texture = (__bridge const void *)impl_->texture;
    textureInfo.format = MTLPixelFormatRGBA8Unorm;
    tgfx::BackendTexture backendTexture(textureInfo, width, height);
    surface_ = tgfx::Surface::MakeFrom(context, backendTexture, tgfx::ImageOrigin::TopLeft);
    impl_->width = width;
    impl_->height = height;
    device_->unlock();
    return surface_ != nullptr;
}

bool TgfxRenderAdapter::acquireTarget(int width, int height) {
    if (width != impl_->width || height != impl_->height || !surface_) {
        if (!RecreateTarget(width, height)) {
            return false;
        }
    }
    return device_->lockContext() != nullptr;
}

void TgfxRenderAdapter::presentTarget() {
    device_->unlock();
}

void TgfxRenderAdapter::drawPreviewBackdrop() {
    if (!surface_) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    if (canvas != nullptr) {
        canvas->clear();
    }
}

bool TgfxRenderAdapter::ReadPixels(std::vector<uint8_t> &pixels) {
    if (!surface_) {
        return false;
    }
    auto *context = device_->lockContext();
    if (!context) {
        return false;
    }
    tgfx::ImageInfo info = tgfx::ImageInfo::Make(impl_->width, impl_->height, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);
    pixels.resize(info.rowBytes() * static_cast<size_t>(info.height()));
    const bool ok = surface_->readPixels(info, pixels.data());
    device_->unlock();
    return ok;
}

int TgfxRenderAdapter::width() const {
    return impl_->width;
}

int TgfxRenderAdapter::height() const {
    return impl_->height;
}

}  // namespace motion
