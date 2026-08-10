#include "PagVideoSequenceEncode.h"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

#include "PagExportErrorUtil.h"
#include "PagRgbAlphaPack.h"

#if defined(__APPLE__)
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#endif

namespace motion {
namespace pag_export {
namespace {

#if defined(__APPLE__)

constexpr uint8_t kAnnexBStartCode[4] = {0, 0, 0, 1};

struct EncodedSample {
    int64_t index = 0;
    bool keyframe = false;
    std::vector<uint8_t> annexB;
};

struct CompressionState {
    std::mutex mutex;
    std::condition_variable cv;
    int pending = 0;
    bool failed = false;
    bool headersReady = false;
    std::vector<std::vector<uint8_t>> headers;
    std::vector<EncodedSample> samples;
};

int EvenAlign(int value) {
    return value + (value & 1);
}

int ClampQuality(int imageQuality) {
    if (imageQuality < 1) {
        return 1;
    }
    if (imageQuality > 100) {
        return 100;
    }
    return imageQuality;
}

int64_t EstimateBitrate(int videoWidth, int videoHeight, float frameRate, int quality) {
    const double fps = frameRate > 1.0f ? static_cast<double>(frameRate) : 30.0;
    // Side-by-side alpha already doubles encode area; keep bpp low so default quality≈80
    // lands near ~8–10 Mbps for 1280×720@30 packed.
    const double bytesPerPixel = 0.025 * (static_cast<double>(quality) / 100.0);
    const double bps =
        static_cast<double>(videoWidth) * static_cast<double>(videoHeight) * fps * bytesPerPixel * 8.0;
    return std::max<int64_t>(150000, static_cast<int64_t>(bps));
}

bool AppendAnnexBFromAvcc(const uint8_t *data, size_t length, std::vector<uint8_t> *out) {
    if (data == nullptr || out == nullptr) {
        return false;
    }
    size_t pos = 0;
    while (pos + 4 <= length) {
        const uint32_t nalLength = (static_cast<uint32_t>(data[pos]) << 24) |
            (static_cast<uint32_t>(data[pos + 1]) << 16) |
            (static_cast<uint32_t>(data[pos + 2]) << 8) | static_cast<uint32_t>(data[pos + 3]);
        pos += 4;
        if (nalLength == 0 || pos + nalLength > length) {
            return false;
        }
        out->insert(out->end(), kAnnexBStartCode, kAnnexBStartCode + 4);
        out->insert(out->end(), data + pos, data + pos + nalLength);
        pos += nalLength;
    }
    return pos == length && !out->empty();
}

bool ExtractHeaders(CMFormatDescriptionRef format, std::vector<std::vector<uint8_t>> *headers) {
    if (format == nullptr || headers == nullptr) {
        return false;
    }
    size_t parameterSetCount = 0;
    int nalUnitHeaderLength = 0;
    OSStatus status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        format, 0, nullptr, nullptr, &parameterSetCount, &nalUnitHeaderLength);
    if (status != noErr || parameterSetCount < 2) {
        return false;
    }
    headers->clear();
    for (size_t index = 0; index < parameterSetCount; ++index) {
        const uint8_t *parameterSet = nullptr;
        size_t parameterSetSize = 0;
        status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            format, index, &parameterSet, &parameterSetSize, nullptr, nullptr);
        if (status != noErr || parameterSet == nullptr || parameterSetSize == 0) {
            return false;
        }
        std::vector<uint8_t> header;
        header.insert(header.end(), kAnnexBStartCode, kAnnexBStartCode + 4);
        header.insert(header.end(), parameterSet, parameterSet + parameterSetSize);
        headers->push_back(std::move(header));
    }
    return headers->size() >= 2;
}

bool CopyPackedRgbaToBgra(const uint8_t *rgba, int width, int height, size_t rowBytes,
                          CVPixelBufferRef buffer) {
    if (CVPixelBufferLockBaseAddress(buffer, 0) != kCVReturnSuccess) {
        return false;
    }
    uint8_t *dst = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(buffer));
    const size_t dstStride = CVPixelBufferGetBytesPerRow(buffer);
    for (int y = 0; y < height; ++y) {
        const uint8_t *srcRow = rgba + static_cast<size_t>(y) * rowBytes;
        uint8_t *dstRow = dst + static_cast<size_t>(y) * dstStride;
        for (int x = 0; x < width; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    return true;
}

void CompressionOutputCallback(void *outputCallbackRefCon, void *sourceFrameRefCon,
                               OSStatus status, VTEncodeInfoFlags infoFlags,
                               CMSampleBufferRef sampleBuffer) {
    (void)infoFlags;
    auto *state = static_cast<CompressionState *>(outputCallbackRefCon);
    const int64_t frameIndex = reinterpret_cast<intptr_t>(sourceFrameRefCon);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (status != noErr || sampleBuffer == nullptr) {
        state->failed = true;
        --state->pending;
        state->cv.notify_all();
        return;
    }

    if (!state->headersReady) {
        CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sampleBuffer);
        if (!ExtractHeaders(format, &state->headers)) {
            state->failed = true;
            --state->pending;
            state->cv.notify_all();
            return;
        }
        state->headersReady = true;
    }

    bool isKeyframe = true;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, true);
    if (attachments != nullptr && CFArrayGetCount(attachments) > 0) {
        auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
        if (dict != nullptr) {
            const void *value = CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync);
            if (value == kCFBooleanTrue) {
                isKeyframe = false;
            }
        }
    }

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (block == nullptr) {
        state->failed = true;
        --state->pending;
        state->cv.notify_all();
        return;
    }
    size_t lengthAtOffset = 0;
    size_t totalLength = 0;
    char *dataPointer = nullptr;
    if (CMBlockBufferGetDataPointer(block, 0, &lengthAtOffset, &totalLength, &dataPointer) !=
            kCMBlockBufferNoErr ||
        dataPointer == nullptr || totalLength == 0) {
        state->failed = true;
        --state->pending;
        state->cv.notify_all();
        return;
    }

    EncodedSample sample;
    sample.index = frameIndex;
    sample.keyframe = isKeyframe;
    if (!AppendAnnexBFromAvcc(reinterpret_cast<const uint8_t *>(dataPointer), totalLength,
                              &sample.annexB)) {
        state->failed = true;
        --state->pending;
        state->cv.notify_all();
        return;
    }
    state->samples.push_back(std::move(sample));
    --state->pending;
    state->cv.notify_all();
}

#endif  // __APPLE__

}  // namespace

Expected<void, PagExportError> EncodeVideoSequence(BitmapFrameSource *frameSource,
                                                   pag::VideoSequence *sequence, FrameTime start,
                                                   FrameTime end, int width, int height,
                                                   int keyFrameInterval, int imageQuality) {
    if (frameSource == nullptr || sequence == nullptr || width <= 0 || height <= 0 || end <= start) {
        if (frameSource != nullptr) {
            frameSource->finish();
        }
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "",
                                             "invalid PAG video sequence options"));
    }

#if !defined(__APPLE__)
    frameSource->finish();
    return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "",
                                         "PAG video sequence encode requires Apple VideoToolbox"));
#else

    const int quality = ClampQuality(imageQuality);
    const int interval = keyFrameInterval > 0 ? keyFrameInterval : 60;
    const float frameRate = sequence->frameRate > 0.0f ? sequence->frameRate : 30.0f;

    CompressionState state;
    VTCompressionSessionRef session = nullptr;
    CVPixelBufferRef pixelBuffer = nullptr;
    bool encodeFailed = false;

    auto cleanup = [&]() {
        if (pixelBuffer != nullptr) {
            CVPixelBufferRelease(pixelBuffer);
            pixelBuffer = nullptr;
        }
        if (session != nullptr) {
            VTCompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
        frameSource->finish();
    };

    // Probe first frame size via pack dimensions from logical size.
    const int videoWidth = EvenAlign(width * 2);
    const int videoHeight = EvenAlign(height);

    CFMutableDictionaryRef encoderSpec = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(encoderSpec, kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
                         kCFBooleanTrue);

    CFMutableDictionaryRef sourceAttrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    OSType pixelFormat = kCVPixelFormatType_32BGRA;
    CFNumberRef pixelFormatNumber =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixelFormat);
    CFDictionarySetValue(sourceAttrs, kCVPixelBufferPixelFormatTypeKey, pixelFormatNumber);
    CFRelease(pixelFormatNumber);
    CFDictionaryRef emptyIOSurface = CFDictionaryCreate(
        kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(sourceAttrs, kCVPixelBufferIOSurfacePropertiesKey, emptyIOSurface);
    CFRelease(emptyIOSurface);

    OSStatus status = VTCompressionSessionCreate(
        kCFAllocatorDefault, videoWidth, videoHeight, kCMVideoCodecType_H264, encoderSpec,
        sourceAttrs, nullptr, CompressionOutputCallback, &state, &session);
    CFRelease(encoderSpec);
    CFRelease(sourceAttrs);
    if (status != noErr || session == nullptr) {
        cleanup();
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "",
                                             "failed to create VideoToolbox compression session"));
    }

    VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel,
                         kVTProfileLevel_H264_Main_AutoLevel);
    CFNumberRef maxKeyFrameInterval =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &interval);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, maxKeyFrameInterval);
    CFRelease(maxKeyFrameInterval);

    const int64_t bitrate = EstimateBitrate(videoWidth, videoHeight, frameRate, quality);
    CFNumberRef bitrateNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &bitrate);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, bitrateNumber);
    CFRelease(bitrateNumber);

    status = VTCompressionSessionPrepareToEncodeFrames(session);
    if (status != noErr) {
        cleanup();
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "",
                                             "VideoToolbox prepare failed"));
    }

    CFMutableDictionaryRef pixelBufferAttrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef emptyIOSurfaceProps = CFDictionaryCreate(
        kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(pixelBufferAttrs, kCVPixelBufferIOSurfacePropertiesKey,
                         emptyIOSurfaceProps);
    CFRelease(emptyIOSurfaceProps);
    const CVReturn bufferStatus = CVPixelBufferCreate(
        kCFAllocatorDefault, static_cast<size_t>(videoWidth), static_cast<size_t>(videoHeight),
        kCVPixelFormatType_32BGRA, pixelBufferAttrs, &pixelBuffer);
    CFRelease(pixelBufferAttrs);
    if (bufferStatus != kCVReturnSuccess || pixelBuffer == nullptr) {
        cleanup();
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "",
                                             "failed to create encode pixel buffer"));
    }

    const int64_t frameDuration = static_cast<int64_t>(1000.0f / frameRate);
    const CMTime duration = CMTimeMake(std::max<int64_t>(1, frameDuration), 1000);

    for (FrameTime time = start; time < end; ++time) {
        Expected<BitmapFrame, std::string> rendered = frameSource->renderFrame(time);
        if (!rendered.hasValue()) {
            encodeFailed = true;
            break;
        }
        const BitmapFrame &frame = rendered.value();
        if (frame.width != width || frame.height != height || frame.rgba == nullptr) {
            encodeFailed = true;
            break;
        }

        std::vector<uint8_t> packed;
        int packedWidth = 0;
        int packedHeight = 0;
        if (!PackRgbAlphaSideBySide(frame.rgba, width, height, frame.rowBytes, &packed, &packedWidth,
                                    &packedHeight) ||
            packedWidth != videoWidth || packedHeight != videoHeight) {
            encodeFailed = true;
            break;
        }
        if (!CopyPackedRgbaToBgra(packed.data(), packedWidth, packedHeight,
                                  static_cast<size_t>(packedWidth) * 4u, pixelBuffer)) {
            encodeFailed = true;
            break;
        }

        const int64_t localIndex = static_cast<int64_t>(time - start);
        const CMTime pts = CMTimeMake(localIndex * std::max<int64_t>(1, frameDuration), 1000);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            ++state.pending;
        }
        status = VTCompressionSessionEncodeFrame(
            session, pixelBuffer, pts, duration, nullptr,
            reinterpret_cast<void *>(static_cast<intptr_t>(localIndex)), nullptr);
        if (status != noErr) {
            std::lock_guard<std::mutex> lock(state.mutex);
            --state.pending;
            encodeFailed = true;
            break;
        }
    }

    if (!encodeFailed) {
        status = VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
        if (status != noErr) {
            encodeFailed = true;
        }
    }

    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.cv.wait(lock, [&]() {
            return state.pending == 0;
        });
        if (state.failed) {
            encodeFailed = true;
        }
    }

    if (encodeFailed || state.samples.empty() || !state.headersReady || state.headers.size() < 2) {
        cleanup();
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "",
                                             "PAG video frame encode failed"));
    }

    std::sort(state.samples.begin(), state.samples.end(),
              [](const EncodedSample &a, const EncodedSample &b) {
                  return a.index < b.index;
              });

    for (const std::vector<uint8_t> &header : state.headers) {
        sequence->headers.push_back(pag::ByteData::MakeCopy(header.data(), header.size()).release());
    }
    for (const EncodedSample &sample : state.samples) {
        auto *videoFrame = new pag::VideoFrame();
        videoFrame->frame = static_cast<pag::Frame>(sample.index);
        videoFrame->isKeyframe = sample.keyframe;
        videoFrame->fileBytes =
            pag::ByteData::MakeCopy(sample.annexB.data(), sample.annexB.size()).release();
        sequence->frames.push_back(videoFrame);
    }

    sequence->width = width;
    sequence->height = height;
    sequence->alphaStartX = width;
    sequence->alphaStartY = 0;

    cleanup();
    if (sequence->frames.size() != static_cast<size_t>(end - start)) {
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "",
                                             "PAG video frame count mismatch"));
    }
    return Expected<void, PagExportError>();
#endif
}

}  // namespace pag_export
}  // namespace motion
