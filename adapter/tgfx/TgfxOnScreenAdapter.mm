#include "TgfxOnScreenAdapter.h"

#import <MetalKit/MetalKit.h>

#include <algorithm>
#include <cmath>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Rect.h>
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

void TgfxOnScreenAdapter::setPreviewBackdrop(PreviewBackdrop backdrop) {
    previewBackdrop_ = backdrop;
}

PreviewBackdrop TgfxOnScreenAdapter::previewBackdrop() const {
    return previewBackdrop_;
}

bool TgfxOnScreenAdapter::compositionBackgroundSrcOver() const {
    return previewBackdrop() == PreviewBackdrop::Transparent;
}

bool TgfxOnScreenAdapter::acquireTarget(int /*width*/, int /*height*/) {
    // The drawable size is authoritative on screen; the requested size only
    // matters for the offscreen adapter. Reuse the surface across frames and
    // recreate only when the drawable size changes (matches tgfx Hello2D).
    auto *context = device_->lockContext();
    if (context == nullptr) {
        return false;
    }
    const CGSize drawableSize = impl_->view.drawableSize;
    const int drawableWidth = int(drawableSize.width);
    const int drawableHeight = int(drawableSize.height);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        device_->unlock();
        return false;
    }
    const bool needsRecreate = !surface_ || surface_->width() != drawableWidth || surface_->height() != drawableHeight;
    if (needsRecreate) {
        surface_ = tgfx::Surface::MakeFrom(context, impl_->window);
        if (!surface_) {
            device_->unlock();
            return false;
        }
    }
    impl_->context = context;
    return true;
}

void TgfxOnScreenAdapter::drawPreviewBackdrop() {
    if (!surface_) {
        return;
    }
    tgfx::Canvas *canvas = surface_->getCanvas();
    if (previewBackdrop() == PreviewBackdrop::Black) {
        canvas->clear(tgfx::Color::Black());
        return;
    }

    // Checkerboard (tgfx hello2d GridBackground style), scaled by content scale.
    const int width = surface_->width();
    const int height = surface_->height();
    canvas->clear(tgfx::Color::White());
    tgfx::Paint tilePaint;
    tilePaint.setStyle(tgfx::PaintStyle::Fill);
    tilePaint.setColor(tgfx::Color{0.8f, 0.8f, 0.8f, 1.f});
    int tileSize = 16 * int(impl_->view.layer.contentsScale);
    if (tileSize <= 0) {
        tileSize = 16;
    }
    for (int y = 0; y < height; y += tileSize) {
        bool draw = (y / tileSize) % 2 == 1;
        for (int x = 0; x < width; x += tileSize) {
            if (draw) {
                canvas->drawRect(tgfx::Rect::MakeXYWH(float(x), float(y), float(tileSize), float(tileSize)), tilePaint);
            }
            draw = !draw;
        }
    }
}

void TgfxOnScreenAdapter::onFrameReady(int sceneWidth, int sceneHeight, Color backgroundColor,
                                       float cornerRadius) {
    if (sceneWidth <= 0 || sceneHeight <= 0 || !surface_) {
        return;
    }
    const float targetWidth = float(surface_->width());
    const float targetHeight = float(surface_->height());
    // Fit Up to 100% (AE Composition panel default): scale down to fit when the
    // drawable is smaller than the composition; never scale above 1:1.
    const float fitScale = std::min(1.0f, std::min(targetWidth / float(sceneWidth), targetHeight / float(sceneHeight)));
    if (fitScale <= 0.0f) {
        return;
    }
    // Map onto a whole-pixel destination rect so AA edges don't sit on half
    // pixels (which previously produced dark left/top fringes with Src).
    const int destWidth = std::max(1, int(std::floor(float(sceneWidth) * fitScale + 1e-6f)));
    const int destHeight = std::max(1, int(std::floor(float(sceneHeight) * fitScale + 1e-6f)));
    const float offsetX = std::floor((targetWidth - float(destWidth)) * 0.5f);
    const float offsetY = std::floor((targetHeight - float(destHeight)) * 0.5f);
    const float scaleX = float(destWidth) / float(sceneWidth);
    const float scaleY = float(destHeight) / float(sceneHeight);
    tgfx::Matrix fit;
    fit.setTranslate(offsetX, offsetY);
    fit.preScale(scaleX, scaleY);
    surface_->getCanvas()->concat(fit);
    TgfxCanvasAdapter::onFrameReady(sceneWidth, sceneHeight, backgroundColor, cornerRadius);
}

void TgfxOnScreenAdapter::presentTarget() {
    if (impl_->context != nullptr) {
        // flush + submit triggers MetalWindow::onPresent, which presents the
        // current drawable and releases it; the Surface/proxy is kept for reuse.
        impl_->context->flushAndSubmit();
        impl_->context = nullptr;
    }
    device_->unlock();
}

}  // namespace motion
