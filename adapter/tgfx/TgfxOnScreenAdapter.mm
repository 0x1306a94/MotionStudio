#include "TgfxOnScreenAdapter.h"

#import <MetalKit/MetalKit.h>

#include <algorithm>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/ImageOrigin.h>
#include <tgfx/gpu/metal/MetalWindow.h>

namespace motion {

struct TgfxOnScreenAdapter::Impl {
    MTKView *view = nil;
    std::shared_ptr<tgfx::MetalWindow> window;
    tgfx::Context *context = nullptr;
};

TgfxOnScreenAdapter::TgfxOnScreenAdapter()
    : impl_(std::make_unique<Impl>()) {
}

TgfxOnScreenAdapter::~TgfxOnScreenAdapter() = default;

std::unique_ptr<TgfxOnScreenAdapter> TgfxOnScreenAdapter::Make(void *mtkView) {
    if (mtkView == nullptr) {
        return nullptr;
    }
    auto adapter = std::unique_ptr<TgfxOnScreenAdapter>(new TgfxOnScreenAdapter());
    adapter->impl_->view = (__bridge MTKView *)mtkView;
    adapter->impl_->window = tgfx::MetalWindow::MakeFrom(adapter->impl_->view);
    if (!adapter->impl_->window) {
        return nullptr;
    }
    adapter->device_ = adapter->impl_->window->getDevice();
    if (!adapter->device_) {
        return nullptr;
    }
    return adapter;
}

bool TgfxOnScreenAdapter::acquireTarget(int /*width*/, int /*height*/) {
    // The drawable size is authoritative on screen; the requested size only
    // matters for the offscreen adapter.
    auto *context = device_->lockContext();
    if (context == nullptr) {
        return false;
    }
    surface_ = tgfx::Surface::MakeFrom(context, impl_->window);
    if (!surface_) {
        device_->unlock();
        return false;
    }
    impl_->context = context;
    return true;
}

void TgfxOnScreenAdapter::onFrameReady(int sceneWidth, int sceneHeight) {
    if (sceneWidth <= 0 || sceneHeight <= 0 || !surface_) {
        return;
    }
    const float targetWidth = float(surface_->width());
    const float targetHeight = float(surface_->height());
    const float scale = std::min(targetWidth / float(sceneWidth),
                                 targetHeight / float(sceneHeight));
    if (scale <= 0.0f) {
        return;
    }
    const float offsetX = (targetWidth - float(sceneWidth) * scale) * 0.5f;
    const float offsetY = (targetHeight - float(sceneHeight) * scale) * 0.5f;
    tgfx::Matrix fit;
    fit.setTranslate(offsetX, offsetY);
    fit.preScale(scale, scale);
    surface_->getCanvas()->concat(fit);
}

void TgfxOnScreenAdapter::presentTarget() {
    if (impl_->context != nullptr) {
        // flush + submit triggers MetalWindow::onPresent, which presents the
        // current drawable.
        impl_->context->flushAndSubmit();
        impl_->context = nullptr;
    }
    surface_.reset();
    device_->unlock();
}

}  // namespace motion
