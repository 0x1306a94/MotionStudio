#include "TgfxRenderAdapter.h"

#import <Metal/Metal.h>

#include <cmath>

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/Paint.h>
#include <tgfx/core/Path.h>
#include <tgfx/core/PathTypes.h>
#include <tgfx/core/Stroke.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Backend.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/ImageOrigin.h>
#include <tgfx/gpu/metal/MetalDevice.h>
#include <tgfx/gpu/metal/MetalTypes.h>

namespace motion {

struct TgfxRenderAdapter::Impl {
    id<MTLDevice> mtlDevice;
    std::shared_ptr<tgfx::MetalDevice> device;
    id<MTLTexture> texture;
    std::shared_ptr<tgfx::Surface> surface;
    int width = 0;
    int height = 0;
    float opacity = 1;
    BlendMode blendMode = BlendMode::Normal;
    std::vector<float> opacityStack;
    std::vector<BlendMode> blendStack;
};

namespace {

uint8_t ToByte(float value) {
    const float clamped = std::min(std::max(value, 0.0f), 1.0f);
    return uint8_t(std::lround(clamped * 255.0f));
}

tgfx::Color ToTgfxColor(const Color &color) {
    return tgfx::Color::FromRGBA(ToByte(color.r), ToByte(color.g), ToByte(color.b),
                                 ToByte(color.a));
}

tgfx::BlendMode ToTgfxBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: {
            return tgfx::BlendMode::SrcOver;
        }
        case BlendMode::Multiply: {
            return tgfx::BlendMode::Multiply;
        }
        case BlendMode::Screen: {
            return tgfx::BlendMode::Screen;
        }
        case BlendMode::Add: {
            return tgfx::BlendMode::PlusLighter;
        }
    }
    return tgfx::BlendMode::SrcOver;
}

tgfx::LineCap ToTgfxLineCap(LineCap cap) {
    switch (cap) {
        case LineCap::Butt: {
            return tgfx::LineCap::Butt;
        }
        case LineCap::Round: {
            return tgfx::LineCap::Round;
        }
        case LineCap::Square: {
            return tgfx::LineCap::Square;
        }
    }
    return tgfx::LineCap::Butt;
}

tgfx::LineJoin ToTgfxLineJoin(LineJoin join) {
    switch (join) {
        case LineJoin::Miter: {
            return tgfx::LineJoin::Miter;
        }
        case LineJoin::Round: {
            return tgfx::LineJoin::Round;
        }
        case LineJoin::Bevel: {
            return tgfx::LineJoin::Bevel;
        }
    }
    return tgfx::LineJoin::Miter;
}

// Converts a BezierPath (relative tangents) into a tgfx path with absolute
// control points.
tgfx::Path ToTgfxPath(const BezierPath &path, FillRule fillRule) {
    tgfx::Path result;
    if (path.vertices.empty()) {
        result.setFillType(fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd
                                                         : tgfx::PathFillType::Winding);
        return result;
    }
    const BezierPath::Vertex &first = path.vertices.front();
    result.moveTo(first.point.x, first.point.y);
    for (size_t i = 1; i < path.vertices.size(); ++i) {
        const BezierPath::Vertex &previous = path.vertices[i - 1];
        const BezierPath::Vertex &current = path.vertices[i];
        result.cubicTo(previous.point.x + previous.outTangent.x,
                       previous.point.y + previous.outTangent.y,
                       current.point.x + current.inTangent.x,
                       current.point.y + current.inTangent.y, current.point.x,
                       current.point.y);
    }
    if (path.closed && path.vertices.size() > 1) {
        const BezierPath::Vertex &last = path.vertices.back();
        result.cubicTo(last.point.x + last.outTangent.x, last.point.y + last.outTangent.y,
                       first.point.x + first.inTangent.x, first.point.y + first.inTangent.y,
                       first.point.x, first.point.y);
        result.close();
    }
    result.setFillType(fillRule == FillRule::EvenOdd ? tgfx::PathFillType::EvenOdd
                                                     : tgfx::PathFillType::Winding);
    return result;
}

tgfx::Matrix ToTgfxMatrix(const Mat3 &matrix) {
    tgfx::Matrix result;
    result.setAll(matrix.values[0], matrix.values[1], matrix.values[2], matrix.values[3],
                  matrix.values[4], matrix.values[5]);
    return result;
}

}  // namespace

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
    adapter->impl_->device =
        tgfx::MetalDevice::MakeFrom((__bridge void *)adapter->impl_->mtlDevice);
    if (!adapter->impl_->device) {
        return nullptr;
    }
    if (!adapter->RecreateTarget(width, height)) {
        return nullptr;
    }
    return adapter;
}

bool TgfxRenderAdapter::RecreateTarget(int width, int height) {
    auto *context = impl_->device->lockContext();
    if (!context) {
        return false;
    }
    impl_->surface.reset();
    impl_->texture = nil;

    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:size_t(width)
                                    height:size_t(height)
                                 mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
    impl_->texture = [impl_->mtlDevice newTextureWithDescriptor:descriptor];

    // The texture field holds the id<MTLTexture> itself (tgfx bridges it back).
    tgfx::MetalTextureInfo textureInfo;
    textureInfo.texture = (__bridge const void *)impl_->texture;
    textureInfo.format = MTLPixelFormatRGBA8Unorm;
    tgfx::BackendTexture backendTexture(textureInfo, width, height);
    impl_->surface =
        tgfx::Surface::MakeFrom(context, backendTexture, tgfx::ImageOrigin::TopLeft);
    impl_->width = width;
    impl_->height = height;
    impl_->device->unlock();
    return impl_->surface != nullptr;
}

void TgfxRenderAdapter::beginFrame(int width, int height, Color clearColor) {
    if (width != impl_->width || height != impl_->height || !impl_->surface) {
        if (!RecreateTarget(width, height)) {
            return;
        }
    }
    auto *context = impl_->device->lockContext();
    if (!context) {
        return;
    }
    impl_->surface->getCanvas()->clear(ToTgfxColor(clearColor));
    impl_->opacity = 1;
    impl_->blendMode = BlendMode::Normal;
    impl_->opacityStack.clear();
    impl_->blendStack.clear();
}

void TgfxRenderAdapter::endFrame() {
    impl_->device->unlock();
}

void TgfxRenderAdapter::save() {
    impl_->surface->getCanvas()->save();
    impl_->opacityStack.push_back(impl_->opacity);
    impl_->blendStack.push_back(impl_->blendMode);
}

void TgfxRenderAdapter::restore() {
    if (impl_->opacityStack.empty()) {
        return;
    }
    impl_->surface->getCanvas()->restore();
    impl_->opacity = impl_->opacityStack.back();
    impl_->opacityStack.pop_back();
    impl_->blendMode = impl_->blendStack.back();
    impl_->blendStack.pop_back();
}

void TgfxRenderAdapter::concatTransform(const Mat3 &matrix) {
    impl_->surface->getCanvas()->concat(ToTgfxMatrix(matrix));
}

void TgfxRenderAdapter::setOpacity(float opacity) {
    impl_->opacity = opacity;
}

void TgfxRenderAdapter::setBlendMode(BlendMode mode) {
    impl_->blendMode = mode;
}

void TgfxRenderAdapter::drawPath(const BezierPath &path, const Paint &paint) {
    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setStyle(tgfx::PaintStyle::Fill);
    Color color = paint.color;
    color.a *= impl_->opacity;
    tgfxPaint.setColor(ToTgfxColor(color));
    tgfxPaint.setBlendMode(ToTgfxBlendMode(impl_->blendMode));
    impl_->surface->getCanvas()->drawPath(ToTgfxPath(path, paint.fillRule), tgfxPaint);
}

void TgfxRenderAdapter::strokePath(const BezierPath &path, const Paint &paint, float width,
                                   LineCap cap, LineJoin join) {
    tgfx::Paint tgfxPaint;
    tgfxPaint.setAntiAlias(true);
    tgfxPaint.setStyle(tgfx::PaintStyle::Stroke);
    tgfxPaint.setStrokeWidth(width);
    tgfxPaint.setLineCap(ToTgfxLineCap(cap));
    tgfxPaint.setLineJoin(ToTgfxLineJoin(join));
    Color color = paint.color;
    color.a *= impl_->opacity;
    tgfxPaint.setColor(ToTgfxColor(color));
    tgfxPaint.setBlendMode(ToTgfxBlendMode(impl_->blendMode));
    impl_->surface->getCanvas()->drawPath(ToTgfxPath(path, paint.fillRule), tgfxPaint);
}

void TgfxRenderAdapter::clipPath(const BezierPath &path, FillRule rule) {
    impl_->surface->getCanvas()->clipPath(ToTgfxPath(path, rule));
}

bool TgfxRenderAdapter::ReadPixels(std::vector<uint8_t> &pixels) {
    if (!impl_->surface) {
        return false;
    }
    auto *context = impl_->device->lockContext();
    if (!context) {
        return false;
    }
    tgfx::ImageInfo info = tgfx::ImageInfo::Make(impl_->width, impl_->height,
                                                 tgfx::ColorType::RGBA_8888,
                                                 tgfx::AlphaType::Premultiplied);
    pixels.resize(info.rowBytes() * size_t(info.height()));
    const bool ok = impl_->surface->readPixels(info, pixels.data());
    impl_->device->unlock();
    return ok;
}

int TgfxRenderAdapter::width() const {
    return impl_->width;
}

int TgfxRenderAdapter::height() const {
    return impl_->height;
}

}  // namespace motion
