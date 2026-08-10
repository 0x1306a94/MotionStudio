#include "PagBitmapFallback.h"

#include <string>

#include "PagAnimatableConvert.h"
#include "PagBitmapSequenceEncode.h"
#include "PagExportErrorUtil.h"

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

}  // namespace

Expected<BitmapFallbackResult, PagExportError> PagBitmapFallback::Build(
    const Document &document, const Composition &hostComposition, const Layer &rootLayer,
    const PagExportOptions &options, BitmapFrameSource *frameSource, pag::ID compositionId,
    pag::ID layerId, std::vector<PagExportWarning> *warnings) {
    if (frameSource == nullptr || options.bitmapScale <= 0.0f) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "",
                                             "invalid PAG bitmap export options"));
    }
    const FrameTime duration = rootLayer.outPoint - rootLayer.inPoint;
    if (duration <= 0) {
        const std::string name = rootLayer.name.empty() ? "(unnamed layer)" : rootLayer.name;
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, rootLayer.id, name, "BitmapForcedByLayerName",
            "Layer \"" + name + "\": visible range is empty; cannot export as bitmap"));
    }

    const BitmapSize size = ComputeBitmapSize(hostComposition.width, hostComposition.height,
                                              options.bitmapScale, options.bitmapMaxResolution);
    if (size.width <= 0 || size.height <= 0) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "",
                                             "invalid PAG bitmap export size"));
    }

    TimeRange visibleRange;
    visibleRange.start = rootLayer.inPoint;
    visibleRange.end = rootLayer.outPoint;
    Expected<void, std::string> prepared = frameSource->prepare(
        document, hostComposition.id, rootLayer.id, visibleRange, size.width, size.height);
    if (!prepared.hasValue()) {
        const std::string name = rootLayer.name.empty() ? "(unnamed layer)" : rootLayer.name;
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, rootLayer.id, name, "BitmapForcedByLayerName",
            "Layer \"" + name + "\": bitmap FrameSource prepare failed: " + prepared.error()));
    }

    auto *bitmapComposition = new pag::BitmapComposition();
    bitmapComposition->id = compositionId;
    bitmapComposition->width = size.width;
    bitmapComposition->height = size.height;
    bitmapComposition->duration = duration;
    bitmapComposition->frameRate = static_cast<float>(hostComposition.frameRate.num) /
        static_cast<float>(hostComposition.frameRate.den);

    auto *sequence = new pag::BitmapSequence();
    sequence->composition = bitmapComposition;
    sequence->width = size.width;
    sequence->height = size.height;
    sequence->frameRate = bitmapComposition->frameRate;
    bitmapComposition->sequences.push_back(sequence);

    Expected<void, PagExportError> filled = EncodeBitmapSequence(
        frameSource, sequence, rootLayer.inPoint, rootLayer.outPoint, size.width, size.height,
        options.bitmapKeyFrameInterval, options.bitmapImageQuality, options.cancelFlag);
    if (!filled.hasValue()) {
        delete bitmapComposition;
        return Unexpected(filled.error());
    }

    auto *pagLayer = new pag::PreComposeLayer();
    pagLayer->id = layerId;
    pagLayer->name = rootLayer.name;
    pagLayer->startTime = rootLayer.inPoint;
    pagLayer->duration = duration;
    pagLayer->isActive = rootLayer.visible;
    // Content is already baked in host space; keep origin at (0,0). When
    // maxResolution shrinks the bitmap below host size, scale the PreCompose
    // layer back up so it still fills the host composition.
    pagLayer->transform = MakeIdentityTransform();
    const float scaleX =
        static_cast<float>(hostComposition.width) / static_cast<float>(size.width);
    const float scaleY =
        static_cast<float>(hostComposition.height) / static_cast<float>(size.height);
    if (scaleX != 1.0f || scaleY != 1.0f) {
        delete pagLayer->transform->scale;
        pagLayer->transform->scale =
            new pag::Property<pag::Point>(pag::Point::Make(scaleX, scaleY));
    }
    pagLayer->composition = bitmapComposition;
    pagLayer->compositionStartTime = 0;

    PushWarning(warnings, rootLayer.id, "BitmapForcedByLayerName",
                "Layer name ends with _bmp; exported as BitmapComposition");
    if (rootLayer.type() == LayerType::Group) {
        PushWarning(warnings, rootLayer.id, "GroupSubtreeRasterized",
                    "Group subtree rasterized into one BitmapComposition");
    }

    BitmapFallbackResult result;
    result.layer = pagLayer;
    result.composition = bitmapComposition;
    return result;
}

Expected<pag::BitmapComposition *, PagExportError> PagBitmapFallback::BuildComposition(
    const Document &document, const Composition &composition, const PagExportOptions &options,
    BitmapFrameSource *frameSource, pag::ID compositionId,
    std::vector<PagExportWarning> *warnings) {
    if (frameSource == nullptr || options.bitmapScale <= 0.0f) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "",
                                             "invalid PAG bitmap export options"));
    }
    if (composition.duration <= 0) {
        const std::string name =
            composition.name.empty() ? "(unnamed composition)" : composition.name;
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, composition.id, name, "BitmapForcedByCompositionName",
            "Composition \"" + name + "\": duration is empty; cannot export as bitmap"));
    }

    const BitmapSize size = ComputeBitmapSize(composition.width, composition.height,
                                              options.bitmapScale, options.bitmapMaxResolution);
    if (size.width <= 0 || size.height <= 0) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "",
                                             "invalid PAG bitmap export size"));
    }

    TimeRange visibleRange;
    visibleRange.start = 0;
    visibleRange.end = composition.duration;
    Expected<void, std::string> prepared = frameSource->prepareComposition(
        document, composition.id, visibleRange, size.width, size.height);
    if (!prepared.hasValue()) {
        const std::string name =
            composition.name.empty() ? "(unnamed composition)" : composition.name;
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, composition.id, name, "BitmapForcedByCompositionName",
            "Composition \"" + name +
                "\": bitmap FrameSource prepareComposition failed: " + prepared.error()));
    }

    auto *bitmapComposition = new pag::BitmapComposition();
    bitmapComposition->id = compositionId;
    bitmapComposition->width = size.width;
    bitmapComposition->height = size.height;
    bitmapComposition->duration = composition.duration;
    bitmapComposition->frameRate = static_cast<float>(composition.frameRate.num) /
        static_cast<float>(composition.frameRate.den);
    bitmapComposition->backgroundColor = ToPagColor(composition.backgroundColor);

    auto *sequence = new pag::BitmapSequence();
    sequence->composition = bitmapComposition;
    sequence->width = size.width;
    sequence->height = size.height;
    sequence->frameRate = bitmapComposition->frameRate;
    bitmapComposition->sequences.push_back(sequence);

    Expected<void, PagExportError> filled = EncodeBitmapSequence(
        frameSource, sequence, 0, composition.duration, size.width, size.height,
        options.bitmapKeyFrameInterval, options.bitmapImageQuality, options.cancelFlag);
    if (!filled.hasValue()) {
        delete bitmapComposition;
        return Unexpected(filled.error());
    }

    PushWarning(warnings, composition.id, "BitmapForcedByCompositionName",
                "Composition name ends with _bmp or was forced by a Precomp layer name; "
                "exported as BitmapComposition");

    return bitmapComposition;
}

}  // namespace pag_export
}  // namespace motion
