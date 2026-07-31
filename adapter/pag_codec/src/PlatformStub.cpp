/////////////////////////////////////////////////////////////////////////////////////////////////
//
// MotionStudio stub for libpag Platform — codec/export only.
// Avoids linking NativePlatform (GPU / hardware decoder) and full rendering.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "platform/Platform.h"
#include "rendering/video/VideoDecoderFactory.h"

namespace pag {

const VideoDecoderFactory *VideoDecoderFactory::ExternalDecoderFactory() {
    return nullptr;
}

const VideoDecoderFactory *VideoDecoderFactory::SoftwareAVCDecoderFactory() {
    return nullptr;
}

bool VideoDecoderFactory::HasExternalSoftwareDecoder() {
    return false;
}

const Platform *Platform::Current() {
    static const Platform platform;
    return &platform;
}

}  // namespace pag
