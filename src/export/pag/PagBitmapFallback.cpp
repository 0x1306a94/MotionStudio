#include "PagBitmapFallback.h"

#include <cmath>
#include <vector>

#include "PagExportErrorUtil.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Pixmap.h"

namespace motion {
namespace pag_export {
namespace {

pag::Transform2D *MakeIdentityTransform() {
    auto *transform = new pag::Transform2D();
    transform->anchorPoint = new pag::Property<pag::Point>(pag::Point::Zero());
    transform->position = new pag::Property<pag::Point>(pag::Point::Zero());
    transform->scale = new pag::Property<pag::Point>(pag::Point::Make(1, 1));
    transform->rotation = new pag::Property<float>(0);
    transform->opacity = new pag::Property<pag::Opacity>(pag::Opaque);
    return transform;
}

void PushWarning(std::vector<PagExportWarning> *warnings, EntityId entityId, const char *code,
                 const char *message) {
    if (warnings == nullptr) {
        return;
    }
    PagExportWarning warning;
    warning.entityId = entityId;
    warning.code = code;
    warning.message = message;
    warnings->push_back(std::move(warning));
}

std::unique_ptr<pag::ByteData> EncodeFrameWebP(const BitmapFrame &frame) {
    if (frame.rgba == nullptr || frame.width <= 0 || frame.height <= 0 || frame.rowBytes == 0) {
        return nullptr;
    }
    const tgfx::ImageInfo info = tgfx::ImageInfo::Make(
        frame.width, frame.height, tgfx::ColorType::RGBA_8888,
        frame.premultiplied ? tgfx::AlphaType::Premultiplied : tgfx::AlphaType::Unpremultiplied,
        frame.rowBytes);
    const tgfx::Pixmap pixmap(info, frame.rgba);
    std::shared_ptr<tgfx::Data> encoded =
        tgfx::ImageCodec::Encode(pixmap, tgfx::EncodedFormat::WEBP, 80);
    if (encoded == nullptr || encoded->size() == 0) {
        return nullptr;
    }
    return pag::ByteData::MakeCopy(encoded->data(), encoded->size());
}

}  // namespace

Expected<BitmapFallbackResult, PagExportError> PagBitmapFallback::Build(
    const Document &document, const Composition &hostComposition, const Layer &rootLayer,
    float bitmapScale, BitmapFrameSource *frameSource, pag::ID compositionId, pag::ID layerId,
    std::vector<PagExportWarning> *warnings) {
    if (frameSource == nullptr || bitmapScale <= 0.0f) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "", "invalid PAG export options"));
    }
    const FrameTime duration = rootLayer.outPoint - rootLayer.inPoint;
    if (duration <= 0) {
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
    }

    TimeRange visibleRange;
    visibleRange.start = rootLayer.inPoint;
    visibleRange.end = rootLayer.outPoint;
    Expected<void, std::string> prepared = frameSource->prepare(
        document, hostComposition.id, rootLayer.id, visibleRange, bitmapScale);
    if (!prepared.hasValue()) {
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
    }

    const int width =
        static_cast<int>(std::lround(static_cast<double>(hostComposition.width) * bitmapScale));
    const int height =
        static_cast<int>(std::lround(static_cast<double>(hostComposition.height) * bitmapScale));
    if (width <= 0 || height <= 0) {
        frameSource->finish();
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "", "invalid PAG export options"));
    }

    auto *bitmapComposition = new pag::BitmapComposition();
    bitmapComposition->id = compositionId;
    bitmapComposition->width = width;
    bitmapComposition->height = height;
    bitmapComposition->duration = duration;
    bitmapComposition->frameRate = static_cast<float>(hostComposition.frameRate.num) /
        static_cast<float>(hostComposition.frameRate.den);

    auto *sequence = new pag::BitmapSequence();
    sequence->composition = bitmapComposition;
    sequence->width = width;
    sequence->height = height;
    sequence->frameRate = bitmapComposition->frameRate;
    bitmapComposition->sequences.push_back(sequence);

    bool encodeFailed = false;
    for (FrameTime time = rootLayer.inPoint; time < rootLayer.outPoint; ++time) {
        Expected<BitmapFrame, std::string> rendered = frameSource->renderFrame(time);
        if (!rendered.hasValue()) {
            encodeFailed = true;
            break;
        }
        const BitmapFrame &frame = rendered.value();
        if (frame.width != width || frame.height != height) {
            encodeFailed = true;
            break;
        }
        std::unique_ptr<pag::ByteData> webp = EncodeFrameWebP(frame);
        if (webp == nullptr) {
            encodeFailed = true;
            break;
        }
        auto *pagFrame = new pag::BitmapFrame();
        pagFrame->isKeyframe = true;
        auto *rect = new pag::BitmapRect();
        rect->x = 0;
        rect->y = 0;
        rect->fileBytes = webp.release();
        pagFrame->bitmaps.push_back(rect);
        sequence->frames.push_back(pagFrame);
    }
    frameSource->finish();

    if (encodeFailed || sequence->frames.empty()) {
        delete bitmapComposition;
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "", "PAG encode failed"));
    }

    auto *pagLayer = new pag::PreComposeLayer();
    pagLayer->id = layerId;
    pagLayer->name = rootLayer.name;
    pagLayer->startTime = rootLayer.inPoint;
    pagLayer->duration = duration;
    pagLayer->isActive = rootLayer.visible;
    pagLayer->transform = MakeIdentityTransform();
    pagLayer->composition = bitmapComposition;
    pagLayer->compositionStartTime = 0;

    if (rootLayer.followPath.enabled) {
        PushWarning(warnings, rootLayer.id, "UnsupportedFollowPath",
                    "FollowPath rasterized into BitmapComposition");
    }
    PushWarning(warnings, rootLayer.id, "BitmapFallback",
                "Layer exported as bitmap PreComposeLayer");
    if (rootLayer.type() == LayerType::Group) {
        PushWarning(warnings, rootLayer.id, "GroupSubtreeRasterized",
                    "Group subtree rasterized into one BitmapComposition");
    }

    BitmapFallbackResult result;
    result.layer = pagLayer;
    result.composition = bitmapComposition;
    return result;
}

}  // namespace pag_export
}  // namespace motion
