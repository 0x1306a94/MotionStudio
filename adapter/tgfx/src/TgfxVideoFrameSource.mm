#include "TgfxVideoFrameSource.h"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>

#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Backend.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/ImageOrigin.h>
#include <tgfx/gpu/metal/MetalDevice.h>
#include <tgfx/gpu/metal/MetalTypes.h>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/RenderAdapter.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "TgfxCanvasAdapter.h"

namespace motion {
namespace {

void RetainCVPixelBuffer(void *handle) {
    if (handle != nullptr) {
        CFRetain(handle);
    }
}

void ReleaseCVPixelBuffer(void *handle) {
    if (handle != nullptr) {
        CFRelease(handle);
    }
}

class CVPixelBufferCanvasAdapter : public TgfxCanvasAdapter {
  public:
    bool Initialize() {
        mtlDevice_ = MTLCreateSystemDefaultDevice();
        if (mtlDevice_ == nil) {
            return false;
        }
        device_ = tgfx::MetalDevice::MakeFrom((__bridge void *)mtlDevice_);
        if (!device_) {
            return false;
        }
        CVReturn status = CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, mtlDevice_, nil, &textureCache_);
        return status == kCVReturnSuccess && textureCache_ != nullptr;
    }

    void SetPixelBuffer(CVPixelBufferRef buffer) {
        pixelBuffer_ = buffer;
    }

    bool lastAcquireSucceeded() const {
        return lastAcquireOk_;
    }

    ~CVPixelBufferCanvasAdapter() override {
        metalTexture_ = nil;
        if (cvTexture_ != nullptr) {
            CFRelease(cvTexture_);
            cvTexture_ = nullptr;
        }
        if (textureCache_ != nullptr) {
            CFRelease(textureCache_);
            textureCache_ = nullptr;
        }
    }

  protected:
    bool acquireTarget(int width, int height) override {
        lastAcquireOk_ = false;
        context_ = nullptr;
        if (pixelBuffer_ == nullptr || device_ == nullptr || textureCache_ == nullptr) {
            return false;
        }
        if (CVPixelBufferGetWidth(pixelBuffer_) != static_cast<size_t>(width) ||
            CVPixelBufferGetHeight(pixelBuffer_) != static_cast<size_t>(height)) {
            return false;
        }
        auto *context = device_->lockContext();
        if (context == nullptr) {
            return false;
        }
        // Do not call releaseGpuCaches() here: clearing image/path caches every frame
        // re-uploads all bitmaps and spikes IOSurface memory into the multi-GB range.
        // Only rebind the per-frame CVPixelBuffer render target.
        surface_.reset();
        metalTexture_ = nil;
        if (cvTexture_ != nullptr) {
            CFRelease(cvTexture_);
            cvTexture_ = nullptr;
        }

        CVReturn status = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache_, pixelBuffer_, nil, MTLPixelFormatBGRA8Unorm, static_cast<size_t>(width), static_cast<size_t>(height), 0, &cvTexture_);
        if (status != kCVReturnSuccess || cvTexture_ == nullptr) {
            device_->unlock();
            return false;
        }
        metalTexture_ = CVMetalTextureGetTexture(cvTexture_);
        if (metalTexture_ == nil) {
            device_->unlock();
            return false;
        }

        tgfx::MetalTextureInfo textureInfo;
        textureInfo.texture = (__bridge const void *)metalTexture_;
        textureInfo.format = MTLPixelFormatBGRA8Unorm;
        tgfx::BackendTexture backendTexture(textureInfo, width, height);
        surface_ = tgfx::Surface::MakeFrom(context, backendTexture, tgfx::ImageOrigin::TopLeft);
        if (!surface_) {
            device_->unlock();
            return false;
        }
        context_ = context;
        lastAcquireOk_ = true;
        return true;
    }

    void presentTarget() override {
        if (context_ != nullptr) {
            context_->flushAndSubmit(true);
            context_ = nullptr;
        }
        if (textureCache_ != nullptr) {
            CVMetalTextureCacheFlush(textureCache_, 0);
        }
        metalTexture_ = nil;
        if (cvTexture_ != nullptr) {
            CFRelease(cvTexture_);
            cvTexture_ = nullptr;
        }
        device_->unlock();
    }

  private:
    id<MTLDevice> mtlDevice_ = nil;
    CVMetalTextureCacheRef textureCache_ = nullptr;
    CVPixelBufferRef pixelBuffer_ = nullptr;
    CVMetalTextureRef cvTexture_ = nullptr;
    id<MTLTexture> metalTexture_ = nil;
    tgfx::Context *context_ = nullptr;
    bool lastAcquireOk_ = false;
};

constexpr size_t kExportPixelBufferPoolSize = 3;

CVPixelBufferPoolRef MakeExportPixelBufferPool(int width, int height) {
    NSDictionary *pixelAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey: @(width),
        (id)kCVPixelBufferHeightKey: @(height),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    // Prefer keeping a few recycled buffers warm; hard cap is enforced on acquire
    // via kCVPixelBufferPoolAllocationThresholdKey (MaximumBufferCount was removed).
    NSDictionary *poolAttributes = @{
        (id)kCVPixelBufferPoolMinimumBufferCountKey: @(kExportPixelBufferPoolSize),
    };
    CVPixelBufferPoolRef pool = nullptr;
    const CVReturn status = CVPixelBufferPoolCreate(kCFAllocatorDefault, (__bridge CFDictionaryRef)poolAttributes, (__bridge CFDictionaryRef)pixelAttributes, &pool);
    if (status != kCVReturnSuccess) {
        return nullptr;
    }
    return pool;
}

// Own pool + AllocationThreshold. When the writer still holds buffers, pump the
// run loop so VT/AVAssetWriter can release them (sleep alone can deadlock).
CVPixelBufferRef AcquirePooledPixelBuffer(CVPixelBufferPoolRef pool) {
    if (pool == nullptr) {
        return nullptr;
    }
    NSDictionary *auxAttributes = @{
        (id)kCVPixelBufferPoolAllocationThresholdKey: @(kExportPixelBufferPoolSize),
    };
    const CFTimeInterval deadline = CACurrentMediaTime() + 60.0;
    while (CACurrentMediaTime() < deadline) {
        CVPixelBufferRef buffer = nullptr;
        const CVReturn status = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(kCFAllocatorDefault, pool, (__bridge CFDictionaryRef)auxAttributes, &buffer);
        if (status == kCVReturnSuccess && buffer != nullptr) {
            return buffer;
        }
        [NSThread sleepForTimeInterval:0.1];
    }
    return nullptr;
}

}  // namespace

struct TgfxVideoFrameSource::Impl {
    const Document *document = nullptr;
    EntityId compositionId;
    VideoExportOptions options;
    std::unique_ptr<CVPixelBufferCanvasAdapter> adapter;
    CVPixelBufferPoolRef pixelBufferPool = nullptr;
};

TgfxVideoFrameSource::TgfxVideoFrameSource()
    : impl_(std::make_unique<Impl>()) {
}

TgfxVideoFrameSource::~TgfxVideoFrameSource() {
    finish();
}

Expected<void, std::string> TgfxVideoFrameSource::prepare(const Document &document,
                                                          EntityId compositionId,
                                                          const VideoExportOptions &options) {
    finish();
    auto adapter = std::make_unique<CVPixelBufferCanvasAdapter>();
    if (!adapter->Initialize()) {
        return Unexpected<std::string>("Metal unavailable for video frame source");
    }
    CVPixelBufferPoolRef pool = MakeExportPixelBufferPool(options.width, options.height);
    if (pool == nullptr) {
        return Unexpected<std::string>("failed to create CVPixelBufferPool");
    }
    impl_->document = &document;
    impl_->compositionId = compositionId;
    impl_->options = options;
    impl_->adapter = std::move(adapter);
    impl_->pixelBufferPool = pool;
    return Expected<void, std::string>();
}

Expected<VideoFrame, std::string> TgfxVideoFrameSource::renderFrame(FrameTime time) {
    if (impl_->document == nullptr || impl_->adapter == nullptr ||
        impl_->pixelBufferPool == nullptr) {
        return Unexpected<std::string>("frame source not prepared");
    }

    auto state = SceneEvaluator::Evaluate(*impl_->document, impl_->compositionId, time);
    if (!state.hasValue()) {
        return Unexpected<std::string>(state.error());
    }

    CVPixelBufferRef buffer = AcquirePooledPixelBuffer(impl_->pixelBufferPool);
    if (buffer == nullptr) {
        return Unexpected<std::string>("failed to acquire pooled CVPixelBuffer");
    }

    Color background = state->backgroundColor;
    background.a = 1.0f;
    impl_->adapter->SetPixelBuffer(buffer);
    impl_->adapter->beginFrame(impl_->options.width, impl_->options.height, background, 0.0f);
    if (!impl_->adapter->lastAcquireSucceeded()) {
        CVPixelBufferRelease(buffer);
        return Unexpected<std::string>("failed to acquire CVPixelBuffer render target");
    }

    const float scaleX = static_cast<float>(impl_->options.width) / static_cast<float>(std::max(state->viewportWidth, 1));
    const float scaleY = static_cast<float>(impl_->options.height) / static_cast<float>(std::max(state->viewportHeight, 1));
    const bool needsScale = scaleX != 1.0f || scaleY != 1.0f;
    if (needsScale) {
        impl_->adapter->save();
        impl_->adapter->concatTransform(Mat3::Scale(Vec2{scaleX, scaleY}));
    }
    PlayCommands(BuildCommands(*state), *impl_->adapter);
    if (needsScale) {
        impl_->adapter->restore();
    }
    impl_->adapter->endFrame();

    VideoFrame frame;
    frame.width = impl_->options.width;
    frame.height = impl_->options.height;
    frame.storage = VideoFrameStorage::PlatformShared;
    frame.platformHandle = buffer;
    frame.retainHandle = RetainCVPixelBuffer;
    frame.releaseHandle = ReleaseCVPixelBuffer;
    // CreatePixelBuffer already returned +1; VideoFrame owns that retain via releaseHandle.
    return frame;
}

void TgfxVideoFrameSource::finish() {
    impl_->adapter.reset();
    if (impl_->pixelBufferPool != nullptr) {
        CVPixelBufferPoolRelease(impl_->pixelBufferPool);
        impl_->pixelBufferPool = nullptr;
    }
    impl_->document = nullptr;
}

}  // namespace motion
