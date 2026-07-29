#include "TgfxOnScreenAdapter.h"

#import <MetalKit/MetalKit.h>

#include <algorithm>
#include <cmath>

#include "OnScreenTransform.h"
#include "TgfxProfileTiming.h"

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

void TgfxOnScreenAdapter::setViewTransform(float zoom, float panXPoints, float panYPoints) {
    viewZoom_ = zoom;
    viewPanX_ = panXPoints;
    viewPanY_ = panYPoints;
}

float TgfxOnScreenAdapter::sceneUnitsPerViewPoint(int sceneWidth, int sceneHeight) const {
    if (sceneWidth <= 0 || sceneHeight <= 0 || impl_->view == nil) {
        return 1.0f;
    }
    const CGSize drawableSize = impl_->view.drawableSize;
    const float contentsScale = static_cast<float>(impl_->view.layer.contentsScale);
    const ScreenTransform screen = MakeOnScreenTransform(sceneWidth, sceneHeight,
                                                         static_cast<float>(drawableSize.width),
                                                         static_cast<float>(drawableSize.height),
                                                         viewZoom_, viewPanX_, viewPanY_, contentsScale);
    const float scale = std::min(std::abs(screen.scaleX), std::abs(screen.scaleY));
    const float pointScale = contentsScale > 0.0f ? contentsScale : 1.0f;
    return pointScale / std::max(scale, 0.001f);
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
    const int drawableWidth = static_cast<int>(drawableSize.width);
    const int drawableHeight = static_cast<int>(drawableSize.height);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        device_->unlock();
        return false;
    }
    const bool needsRecreate = !surface_ || surface_->width() != drawableWidth ||
        surface_->height() != drawableHeight;
    if (needsRecreate) {
        // Drop PathRef / mask caches before recreating so GPU proxies can purge.
        releaseGpuCaches(context);
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
    int tileSize = 16 * static_cast<int>(impl_->view.layer.contentsScale);
    if (tileSize <= 0) {
        tileSize = 16;
    }
    for (int y = 0; y < height; y += tileSize) {
        bool draw = (y / tileSize) % 2 == 1;
        for (int x = 0; x < width; x += tileSize) {
            if (draw) {
                canvas->drawRect(tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y), static_cast<float>(tileSize), static_cast<float>(tileSize)), tilePaint);
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
    const float targetWidth = static_cast<float>(surface_->width());
    const float targetHeight = static_cast<float>(surface_->height());
    const float contentsScale = static_cast<float>(impl_->view.layer.contentsScale);
    const ScreenTransform screen = MakeOnScreenTransform(sceneWidth, sceneHeight, targetWidth, targetHeight,
                                                         viewZoom_, viewPanX_, viewPanY_, contentsScale);
    tgfx::Matrix matrix;
    matrix.setAll(screen.scaleX, screen.skewX, screen.translateX,
                  screen.skewY, screen.scaleY, screen.translateY,
                  0.0f, 0.0f, 1.0f);
    surface_->getCanvas()->concat(matrix);
    TgfxCanvasAdapter::onFrameReady(sceneWidth, sceneHeight, backgroundColor, cornerRadius);
}

void TgfxOnScreenAdapter::presentTarget() {
    if (impl_->context != nullptr) {
        // flush + submit triggers MetalWindow::onPresent, which presents the
        // current drawable and releases it; the Surface/proxy is kept for reuse.
        const auto flushStart = TgfxProfileClock::now();
        impl_->context->flushAndSubmit();
        const auto flushEnd = TgfxProfileClock::now();
        endFrameProfile_.flushSubmitMs = Milliseconds(flushStart, flushEnd);
        impl_->context = nullptr;
    }
    const auto unlockStart = TgfxProfileClock::now();
    device_->unlock();
    const auto unlockEnd = TgfxProfileClock::now();
    endFrameProfile_.deviceUnlockMs = Milliseconds(unlockStart, unlockEnd);
}

}  // namespace motion
