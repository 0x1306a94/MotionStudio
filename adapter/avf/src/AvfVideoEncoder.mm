#include "AvfVideoEncoder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>

#include <filesystem>

namespace motion {
namespace {

NSString *ProfileLevel(H264Profile profile) {
    switch (profile) {
        case H264Profile::Baseline:
            return AVVideoProfileLevelH264BaselineAutoLevel;
        case H264Profile::Main:
            return AVVideoProfileLevelH264MainAutoLevel;
        case H264Profile::High:
            return AVVideoProfileLevelH264HighAutoLevel;
    }
    return AVVideoProfileLevelH264HighAutoLevel;
}

bool WaitReady(AVAssetWriterInput *input, NSError **error, CFTimeInterval timeoutSeconds = 60.0) {
    const CFTimeInterval deadline = CACurrentMediaTime() + timeoutSeconds;
    while (!input.readyForMoreMediaData) {
        if (CACurrentMediaTime() > deadline) {
            if (error != nil) {
                *error = [NSError errorWithDomain:@"motion.AvfVideoEncoder" code:1 userInfo:@{NSLocalizedDescriptionKey: @"writer input timed out"}];
            }
            return false;
        }
        // AVAssetWriter encoding may need the run loop to make progress on this thread.
        [NSThread sleepForTimeInterval:0.1];
    }
    return true;
}

CVPixelBufferRef CreateBgraBuffer(int width, int height, NSError **error) {
    NSDictionary *attributes = @{
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    CVPixelBufferRef buffer = nullptr;
    const CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, static_cast<size_t>(width), static_cast<size_t>(height), kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)attributes, &buffer);
    if (status != kCVReturnSuccess || buffer == nullptr) {
        if (error != nil) {
            *error = [NSError errorWithDomain:@"motion.AvfVideoEncoder" code:status userInfo:@{NSLocalizedDescriptionKey: @"CVPixelBufferCreate failed"}];
        }
        return nullptr;
    }
    return buffer;
}

bool CopyRgbaToBgra(const VideoFrame &frame, CVPixelBufferRef buffer, NSError **error) {
    if (CVPixelBufferLockBaseAddress(buffer, 0) != kCVReturnSuccess) {
        if (error != nil) {
            *error = [NSError errorWithDomain:@"motion.AvfVideoEncoder" code:2 userInfo:@{NSLocalizedDescriptionKey: @"lock pixel buffer failed"}];
        }
        return false;
    }
    uint8_t *dst = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(buffer));
    const size_t dstStride = CVPixelBufferGetBytesPerRow(buffer);
    const size_t width = static_cast<size_t>(frame.width);
    const size_t height = static_cast<size_t>(frame.height);
    for (size_t y = 0; y < height; ++y) {
        const uint8_t *srcRow = frame.rgba + y * frame.rowBytes;
        uint8_t *dstRow = dst + y * dstStride;
        for (size_t x = 0; x < width; ++x) {
            const uint8_t r = srcRow[x * 4 + 0];
            const uint8_t g = srcRow[x * 4 + 1];
            const uint8_t b = srcRow[x * 4 + 2];
            const uint8_t a = srcRow[x * 4 + 3];
            dstRow[x * 4 + 0] = b;
            dstRow[x * 4 + 1] = g;
            dstRow[x * 4 + 2] = r;
            dstRow[x * 4 + 3] = a;
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    return true;
}

std::string NsErrorMessage(NSError *error, const char *fallback) {
    if (error == nil) {
        return fallback;
    }
    NSString *text = error.localizedDescription;
    if (text == nil || text.length == 0) {
        return fallback;
    }
    return std::string([text UTF8String]);
}

}  // namespace

struct AvfVideoEncoder::Impl {
    VideoExportOptions options;
    AVAssetWriter *writer = nil;
    AVAssetWriterInput *input = nil;
    AVAssetWriterInputPixelBufferAdaptor *adaptor = nil;
    bool sessionStarted = false;
    bool finished = false;
};

AvfVideoEncoder::AvfVideoEncoder()
    : impl_(std::make_unique<Impl>()) {
}

AvfVideoEncoder::~AvfVideoEncoder() {
    if (impl_->writer != nil && !impl_->finished) {
        abort();
    }
}

Expected<void, std::string> AvfVideoEncoder::begin(const VideoExportOptions &options) {
    abort();
    impl_->options = options;
    impl_->finished = false;

    std::error_code ec;
    std::filesystem::remove(options.outputPath, ec);

    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:options.outputPath.c_str()]];
    NSError *error = nil;
    AVAssetWriter *writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&error];
    if (writer == nil) {
        return Unexpected<std::string>(NsErrorMessage(error, "failed to create AVAssetWriter"));
    }
    // Keep false for export memory: optimizing for network rewrites the file and can
    // retain a large intermediate working set on long clips.
    writer.shouldOptimizeForNetworkUse = NO;

    NSDictionary *compression = @{
        AVVideoAverageBitRateKey: @(options.bitrateBps),
        AVVideoMaxKeyFrameIntervalKey: @(options.keyframeInterval),
        AVVideoProfileLevelKey: ProfileLevel(options.profile),
    };
    NSDictionary *settings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(options.width),
        AVVideoHeightKey: @(options.height),
        AVVideoCompressionPropertiesKey: compression,
    };

    AVAssetWriterInput *input = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo outputSettings:settings];
    // YES so readyForMoreMediaData actually backpressures. With NO it often stays YES
    // and AVAssetWriter retains an unbounded queue of frame copies.
    input.expectsMediaDataInRealTime = YES;
    if (![writer canAddInput:input]) {
        return Unexpected<std::string>("cannot add video input to AVAssetWriter");
    }
    [writer addInput:input];

    // nil source attributes: do not create an adaptor-owned CVPixelBufferPool.
    // FrameSource owns the Metal-compatible pool; sharing adaptor.pixelBufferPool
    // has crashed inside CVPixelBufferPoolCreatePixelBuffer mid-export.
    AVAssetWriterInputPixelBufferAdaptor *adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc] initWithAssetWriterInput:input sourcePixelBufferAttributes:nil];

    if (![writer startWriting]) {
        return Unexpected<std::string>(NsErrorMessage(writer.error, "startWriting failed"));
    }
    [writer startSessionAtSourceTime:kCMTimeZero];

    impl_->writer = writer;
    impl_->input = input;
    impl_->adaptor = adaptor;
    impl_->sessionStarted = true;
    return Expected<void, std::string>();
}

Expected<void, std::string> AvfVideoEncoder::waitUntilReadyForMoreFrames() {
    if (impl_->input == nil) {
        return Unexpected<std::string>("encoder not started");
    }
    NSError *error = nil;
    if (!WaitReady(impl_->input, &error)) {
        return Unexpected<std::string>(NsErrorMessage(error, "writer input not ready"));
    }
    return Expected<void, std::string>();
}

Expected<void, std::string> AvfVideoEncoder::appendFrame(const VideoFrame &frame,
                                                         FrameTime presentationIndex) {
    if (impl_->writer == nil || impl_->adaptor == nil || impl_->input == nil) {
        return Unexpected<std::string>("encoder not started");
    }
    if (frame.width != impl_->options.width || frame.height != impl_->options.height) {
        return Unexpected<std::string>("frame size mismatch");
    }

    NSError *error = nil;
    if (!WaitReady(impl_->input, &error)) {
        return Unexpected<std::string>(NsErrorMessage(error, "writer input not ready"));
    }

    CVPixelBufferRef buffer = nullptr;
    bool owned = false;
    if (frame.storage == VideoFrameStorage::PlatformShared) {
        if (frame.platformHandle == nullptr) {
            return Unexpected<std::string>("null platform frame handle");
        }
        buffer = static_cast<CVPixelBufferRef>(frame.platformHandle);
        if (frame.retainHandle != nullptr) {
            frame.retainHandle(frame.platformHandle);
            owned = true;
        }
    } else {
        if (frame.rgba == nullptr || frame.rowBytes == 0) {
            return Unexpected<std::string>("cpu frame missing rgba bytes");
        }
        buffer = CreateBgraBuffer(frame.width, frame.height, &error);
        if (buffer == nullptr) {
            return Unexpected<std::string>(NsErrorMessage(error, "create pixel buffer failed"));
        }
        owned = true;
        if (!CopyRgbaToBgra(frame, buffer, &error)) {
            CVPixelBufferRelease(buffer);
            return Unexpected<std::string>(NsErrorMessage(error, "rgba copy failed"));
        }
    }

    const int32_t timescale = static_cast<int32_t>(impl_->options.frameRate.num);
    const int64_t value = presentationIndex * static_cast<int64_t>(impl_->options.frameRate.den);
    const CMTime pts = CMTimeMake(value, timescale);
    const BOOL ok = [impl_->adaptor appendPixelBuffer:buffer withPresentationTime:pts];
    if (owned) {
        CVPixelBufferRelease(buffer);
    }
    if (!ok) {
        return Unexpected<std::string>(
            NsErrorMessage(impl_->writer.error, "appendPixelBuffer failed"));
    }
    return Expected<void, std::string>();
}

Expected<void, std::string> AvfVideoEncoder::end() {
    if (impl_->writer == nil || impl_->input == nil) {
        return Unexpected<std::string>("encoder not started");
    }
    if (impl_->finished) {
        return Expected<void, std::string>();
    }

    [impl_->input markAsFinished];
    __block BOOL finishedOk = NO;
    __block NSError *finishError = nil;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    [impl_->writer finishWritingWithCompletionHandler:^{
        finishedOk = (impl_->writer.status == AVAssetWriterStatusCompleted);
        finishError = impl_->writer.error;
        dispatch_semaphore_signal(semaphore);
    }];
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    impl_->finished = true;
    impl_->sessionStarted = false;
    impl_->writer = nil;
    impl_->input = nil;
    impl_->adaptor = nil;
    if (!finishedOk) {
        std::error_code ec;
        std::filesystem::remove(impl_->options.outputPath, ec);
        return Unexpected<std::string>(NsErrorMessage(finishError, "finishWriting failed"));
    }
    return Expected<void, std::string>();
}

void AvfVideoEncoder::abort() {
    if (impl_->writer != nil && impl_->writer.status == AVAssetWriterStatusWriting) {
        [impl_->writer cancelWriting];
    }
    impl_->writer = nil;
    impl_->input = nil;
    impl_->adaptor = nil;
    impl_->sessionStarted = false;
    impl_->finished = false;
    if (!impl_->options.outputPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(impl_->options.outputPath, ec);
    }
}

}  // namespace motion
