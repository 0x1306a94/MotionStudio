#include "PagFileBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <set>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeType.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/ImageScaleLayout.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "PagAnimatableConvert.h"
#include "PagBitmapFallback.h"
#include "codec/utils/WebpDecoder.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Pixmap.h"

namespace motion {
namespace pag_export {
namespace {

bool TrimIsDefault(const StrokeStyle &stroke) {
    if (stroke.trimStart.isAnimated() || stroke.trimEnd.isAnimated() ||
        stroke.trimOffset.isAnimated()) {
        return false;
    }
    return stroke.trimStart.staticValue() == 0.0f && stroke.trimEnd.staticValue() == 1.0f &&
        stroke.trimOffset.staticValue() == 0.0f;
}

pag::LineCap MapLineCap(LineCap cap) {
    switch (cap) {
        case LineCap::Butt:
            return pag::LineCap::Butt;
        case LineCap::Round:
            return pag::LineCap::Round;
        case LineCap::Square:
            return pag::LineCap::Square;
    }
    return pag::LineCap::Butt;
}

pag::LineJoin MapLineJoin(LineJoin join) {
    switch (join) {
        case LineJoin::Miter:
            return pag::LineJoin::Miter;
        case LineJoin::Round:
            return pag::LineJoin::Round;
        case LineJoin::Bevel:
            return pag::LineJoin::Bevel;
    }
    return pag::LineJoin::Miter;
}

pag::FillRule MapFillRule(FillRule rule) {
    switch (rule) {
        case FillRule::NonZero:
            return pag::FillRule::NonZeroWinding;
        case FillRule::EvenOdd:
            return pag::FillRule::EvenOdd;
    }
    return pag::FillRule::NonZeroWinding;
}

pag::MaskMode MapMaskMode(MaskMode mode) {
    switch (mode) {
        case MaskMode::Add:
            return pag::MaskMode::Add;
        case MaskMode::Subtract:
            return pag::MaskMode::Subtract;
        case MaskMode::Intersect:
            return pag::MaskMode::Intersect;
    }
    return pag::MaskMode::Add;
}

pag::TrackMatteType MapTrackMatte(TrackMatteType type) {
    switch (type) {
        case TrackMatteType::None:
            return pag::TrackMatteType::None;
        case TrackMatteType::Alpha:
            return pag::TrackMatteType::Alpha;
        case TrackMatteType::AlphaInverted:
            return pag::TrackMatteType::AlphaInverted;
        case TrackMatteType::Luma:
            return pag::TrackMatteType::Luma;
        case TrackMatteType::LumaInverted:
            return pag::TrackMatteType::LumaInverted;
    }
    return pag::TrackMatteType::None;
}

pag::PAGScaleMode MapScaleMode(ImageScaleMode mode) {
    switch (mode) {
        case ImageScaleMode::None:
            return pag::PAGScaleMode::None;
        case ImageScaleMode::Stretch:
            return pag::PAGScaleMode::Stretch;
        case ImageScaleMode::LetterBox:
            return pag::PAGScaleMode::LetterBox;
        case ImageScaleMode::Zoom:
            return pag::PAGScaleMode::Zoom;
    }
    return pag::PAGScaleMode::LetterBox;
}

pag::ParagraphJustification MapAlign(TextAlign align) {
    switch (align) {
        case TextAlign::Left:
            return pag::ParagraphJustification::LeftJustify;
        case TextAlign::Center:
            return pag::ParagraphJustification::CenterJustify;
        case TextAlign::Right:
            return pag::ParagraphJustification::RightJustify;
    }
    return pag::ParagraphJustification::LeftJustify;
}

pag::Ratio MakeStretchRatio(double stretch) {
    if (stretch <= 0.0) {
        return pag::DefaultRatio;
    }
    return pag::Ratio{static_cast<int32_t>(std::lround(stretch * 1000.0)), 1000u};
}

void Warn(std::vector<PagExportWarning> *warnings, EntityId entityId, const char *code,
          const char *message) {
    PagExportWarning warning;
    warning.entityId = entityId;
    warning.code = code;
    warning.message = message;
    warnings->push_back(std::move(warning));
}

void CollectTextKeyframeTimes(const Animatable<std::string> &text, std::set<FrameTime> *times) {
    for (const auto &keyframe : text.keyframes()) {
        times->insert(keyframe.time);
    }
}

std::string JoinPath(const std::string &root, const std::string &relative) {
    if (root.empty()) {
        return relative;
    }
    if (root.back() == '/') {
        return root + relative;
    }
    return root + "/" + relative;
}

void MapPointProperty(pag::Property<pag::Point> *property,
                      const std::function<pag::Point(pag::Point)> &map) {
    if (property == nullptr) {
        return;
    }
    if (!property->animatable()) {
        property->value = map(property->value);
        return;
    }
    auto *animated = static_cast<pag::AnimatableProperty<pag::Point> *>(property);
    for (pag::Keyframe<pag::Point> *keyframe : animated->keyframes) {
        keyframe->startValue = map(keyframe->startValue);
        keyframe->endValue = map(keyframe->endValue);
    }
    animated->value = map(animated->value);
}

void MultiplyPointProperty(pag::Property<pag::Point> *property, float scaleX, float scaleY) {
    MapPointProperty(property, [scaleX, scaleY](pag::Point point) {
        return pag::Point::Make(point.x * scaleX, point.y * scaleY);
    });
}

void DeleteCompositions(std::vector<pag::Composition *> *compositions) {
    if (compositions == nullptr) {
        return;
    }
    for (pag::Composition *owned : *compositions) {
        delete owned;
    }
    compositions->clear();
}

// PAG Codec::Encode only writes ImageBytes that WebPGetInfo accepts.
std::unique_ptr<pag::ByteData> LoadImageAsWebP(const std::string &fullPath, int *width,
                                               int *height) {
    std::unique_ptr<pag::ByteData> raw = pag::ByteData::FromPath(fullPath);
    if (raw == nullptr || raw->length() == 0) {
        return nullptr;
    }
    int webpWidth = 0;
    int webpHeight = 0;
    if (pag::WebPGetInfo(raw->data(), raw->length(), &webpWidth, &webpHeight)) {
        *width = webpWidth;
        *height = webpHeight;
        return raw;
    }

    std::shared_ptr<tgfx::ImageCodec> codec = tgfx::ImageCodec::MakeFrom(fullPath);
    if (codec == nullptr || codec->width() <= 0 || codec->height() <= 0) {
        return nullptr;
    }
    const tgfx::ImageInfo info =
        tgfx::ImageInfo::Make(codec->width(), codec->height(), tgfx::ColorType::RGBA_8888,
                              tgfx::AlphaType::Unpremultiplied);
    std::vector<uint8_t> pixels(info.byteSize());
    if (!codec->readPixels(info, pixels.data())) {
        return nullptr;
    }
    const tgfx::Pixmap pixmap(info, pixels.data());
    std::shared_ptr<tgfx::Data> encoded =
        tgfx::ImageCodec::Encode(pixmap, tgfx::EncodedFormat::WEBP, 80);
    if (encoded == nullptr || encoded->size() == 0) {
        return nullptr;
    }
    *width = codec->width();
    *height = codec->height();
    return pag::ByteData::MakeCopy(encoded->data(), encoded->size());
}

}  // namespace

PagFileBuilder::PagFileBuilder(const Document &document, const Composition &composition,
                               const PagExportOptions &options, BitmapFrameSource *frameSource)
    : document_(document)
    , rootComposition_(composition)
    , options_(options)
    , frameSource_(frameSource) {
}

Expected<PagBuildResult, PagExportError> PagFileBuilder::build() {
    if (options_.bitmapScale <= 0.0f) {
        return Unexpected(PagExportError::InvalidOptions);
    }

    Expected<void, PagExportError> collected = collectCompositionOrder(rootComposition_.id);
    if (!collected.hasValue()) {
        return Unexpected(collected.error());
    }

    auto cleanupOwned = [this](std::vector<pag::Composition *> &vectorCompositions) {
        DeleteCompositions(&vectorCompositions);
        DeleteCompositions(&bitmapCompositions_);
        DeleteCompositions(&nestedCompositions_);
        for (pag::ImageBytes *image : imageBytesList_) {
            delete image;
        }
        imageBytesList_.clear();
    };

    std::vector<pag::Composition *> vectorCompositions;
    vectorCompositions.reserve(compositionOrder_.size());
    for (EntityId compositionId : compositionOrder_) {
        const Composition *source = findComposition(compositionId);
        if (source == nullptr) {
            cleanupOwned(vectorCompositions);
            return Unexpected(PagExportError::InvalidComposition);
        }
        Expected<pag::VectorComposition *, PagExportError> built = buildComposition(*source);
        if (!built.hasValue()) {
            cleanupOwned(vectorCompositions);
            return Unexpected(built.error());
        }
        vectorCompositions.push_back(built.value());
    }

    // Nested clip inners first; MS composition roots last (File uses compositions.back() as main).
    std::vector<pag::Composition *> compositions;
    compositions.reserve(bitmapCompositions_.size() + nestedCompositions_.size() +
                         vectorCompositions.size());
    compositions.insert(compositions.end(), bitmapCompositions_.begin(), bitmapCompositions_.end());
    compositions.insert(compositions.end(), nestedCompositions_.begin(), nestedCompositions_.end());
    compositions.insert(compositions.end(), vectorCompositions.begin(), vectorCompositions.end());
    bitmapCompositions_.clear();
    nestedCompositions_.clear();

    std::shared_ptr<pag::File> file = pag::Codec::VerifyAndMake(compositions, imageBytesList_);
    imageBytesList_.clear();
    if (file == nullptr) {
        return Unexpected(PagExportError::EncodeFailed);
    }
    PagBuildResult result;
    result.file = std::move(file);
    result.warnings = std::move(warnings_);
    return result;
}

const Composition *PagFileBuilder::findComposition(EntityId id) const {
    for (const auto &composition : document_.compositions) {
        if (composition && composition->id == id) {
            return composition.get();
        }
    }
    return nullptr;
}

Expected<void, PagExportError> PagFileBuilder::collectCompositionOrder(EntityId compositionId) {
    if (!compositionId.isValid()) {
        return Unexpected(PagExportError::InvalidComposition);
    }
    if (visitedCompositions_.count(compositionId.value) != 0) {
        return Expected<void, PagExportError>();
    }
    visitedCompositions_.insert(compositionId.value);
    const Composition *composition = findComposition(compositionId);
    if (composition == nullptr) {
        return Unexpected(PagExportError::InvalidComposition);
    }
    for (const auto &layerPtr : composition->layers) {
        if (layerPtr == nullptr || layerPtr->type() != LayerType::Precomp) {
            continue;
        }
        const auto &content = static_cast<const PrecompContent &>(*layerPtr->content);
        Expected<void, PagExportError> nested = collectCompositionOrder(content.compositionId);
        if (!nested.hasValue()) {
            return Unexpected(nested.error());
        }
    }
    compositionOrder_.push_back(compositionId);
    return Expected<void, PagExportError>();
}

bool PagFileBuilder::needsBitmapFallback(const Layer &layer) const {
    return layer.followPath.enabled;
}

void PagFileBuilder::collectDescendants(EntityId rootLayerId,
                                        std::unordered_set<uint64_t> *out) const {
    if (currentHostComposition_ == nullptr || out == nullptr) {
        return;
    }
    bool added = true;
    while (added) {
        added = false;
        for (const auto &layerPtr : currentHostComposition_->layers) {
            if (layerPtr == nullptr || !layerPtr->parentId.isValid()) {
                continue;
            }
            if (out->count(layerPtr->id.value) != 0) {
                continue;
            }
            const uint64_t parentValue = layerPtr->parentId.value;
            if (parentValue == rootLayerId.value || out->count(parentValue) != 0) {
                out->insert(layerPtr->id.value);
                added = true;
            }
        }
    }
}

void PagFileBuilder::skipLayerWithWarning(const Layer &layer, const char *code, const char *message,
                                          std::unordered_set<uint64_t> *skippedDescendants) {
    Warn(&warnings_, layer.id, code, message);
    if (layer.type() == LayerType::Group && skippedDescendants != nullptr) {
        collectDescendants(layer.id, skippedDescendants);
    }
}

pag::VectorComposition *PagFileBuilder::wrapCompositionWithCornerClip(
    pag::VectorComposition *inner, const Composition &composition) {
    auto *root = new pag::VectorComposition();
    root->id = nextCompositionId_++;
    root->width = inner->width;
    root->height = inner->height;
    root->duration = inner->duration;
    root->frameRate = inner->frameRate;
    root->backgroundColor = inner->backgroundColor;

    auto *wrap = new pag::PreComposeLayer();
    wrap->id = nextLayerId_++;
    wrap->name = "CompositionClip";
    wrap->startTime = 0;
    wrap->duration = inner->duration;
    wrap->isActive = true;
    wrap->composition = inner;
    wrap->compositionStartTime = 0;
    wrap->containingComposition = root;
    wrap->transform = new pag::Transform2D();
    wrap->transform->anchorPoint = new pag::Property<pag::Point>(pag::Point::Zero());
    wrap->transform->position = new pag::Property<pag::Point>(pag::Point::Zero());
    wrap->transform->scale = new pag::Property<pag::Point>(pag::Point::Make(1, 1));
    wrap->transform->rotation = new pag::Property<float>(0);
    wrap->transform->opacity = new pag::Property<pag::Opacity>(pag::Opaque);

    const float width = static_cast<float>(composition.width);
    const float height = static_cast<float>(composition.height);
    BezierPath maskPath = ShapeGeometryToBezierPath(
        MakeRectGeometry(Vec2{width * 0.5f, height * 0.5f}, Vec2{width, height},
                         composition.cornerRadius));
    Animatable<BezierPath> maskAnimatable;
    maskAnimatable.setStaticValue(maskPath);

    auto *mask = new pag::MaskData();
    mask->id = nextMaskId_++;
    mask->inverted = false;
    mask->maskMode = pag::MaskMode::Add;
    mask->maskPath = ConvertPath(maskAnimatable, &warnings_, composition.id);
    mask->maskOpacity = new pag::Property<pag::Opacity>(pag::Opaque);
    mask->maskExpansion = new pag::Property<float>(0);
    wrap->masks.push_back(mask);

    root->layers.push_back(wrap);
    nestedCompositions_.push_back(inner);
    compositionByEntity_[composition.id.value] = root;
    return root;
}

void PagFileBuilder::applyImageContainerFit(pag::ImageLayer *pagLayer, const Layer &layer,
                                            const ImageContent &content, int intrinsicWidth,
                                            int intrinsicHeight) {
    if (pagLayer == nullptr || pagLayer->transform == nullptr || intrinsicWidth <= 0 ||
        intrinsicHeight <= 0) {
        return;
    }

    if (content.size.isAnimated()) {
        Warn(&warnings_, layer.id, "ImageSizeAnimationBakedAsStatic",
             "Animated image container size baked using in-point value");
    }
    const Vec2 container = content.size.evaluate(layer.inPoint);
    const Vec2 intrinsic{static_cast<float>(intrinsicWidth), static_cast<float>(intrinsicHeight)};
    const ImageRect destination = ComputeImageDestinationRect(container, intrinsic, content.scaleMode);
    if (destination.isEmpty()) {
        return;
    }

    const float fitX = destination.width / intrinsic.x;
    const float fitY = destination.height / intrinsic.y;
    if (fitX <= 0.0f || fitY <= 0.0f) {
        return;
    }

    // AE model: ImageBytes stays at source size; container fit is baked into Transform.
    MultiplyPointProperty(pagLayer->transform->scale, fitX, fitY);
    MapPointProperty(pagLayer->transform->anchorPoint, [&](pag::Point point) {
        return pag::Point::Make((point.x - destination.x) / fitX, (point.y - destination.y) / fitY);
    });

    const bool overflows = destination.x < -0.001f || destination.y < -0.001f ||
        destination.x + destination.width > container.x + 0.001f ||
        destination.y + destination.height > container.y + 0.001f;
    if (!overflows) {
        return;
    }

    const float maskX = -destination.x / fitX;
    const float maskY = -destination.y / fitY;
    const float maskW = container.x / fitX;
    const float maskH = container.y / fitY;
    BezierPath clipPath = ShapeGeometryToBezierPath(
        MakeRectGeometry(Vec2{maskX + maskW * 0.5f, maskY + maskH * 0.5f}, Vec2{maskW, maskH}, 0.0f));
    Animatable<BezierPath> clipAnimatable;
    clipAnimatable.setStaticValue(clipPath);
    auto *mask = new pag::MaskData();
    mask->id = nextMaskId_++;
    mask->inverted = false;
    mask->maskMode = pag::MaskMode::Add;
    mask->maskPath = ConvertPath(clipAnimatable, &warnings_, layer.id);
    mask->maskOpacity = new pag::Property<pag::Opacity>(pag::Opaque);
    mask->maskExpansion = new pag::Property<float>(0);
    pagLayer->masks.push_back(mask);
}

pag::ShapeLayer *PagFileBuilder::buildCompositionBackdrop(const Composition &composition) {
    auto *pagLayer = new pag::ShapeLayer();
    pagLayer->id = nextLayerId_++;
    pagLayer->name = "CompositionBackground";
    pagLayer->startTime = 0;
    pagLayer->duration = composition.duration;
    pagLayer->isActive = true;
    pagLayer->transform = new pag::Transform2D();
    pagLayer->transform->anchorPoint = new pag::Property<pag::Point>(pag::Point::Zero());
    pagLayer->transform->position = new pag::Property<pag::Point>(pag::Point::Zero());
    pagLayer->transform->scale = new pag::Property<pag::Point>(pag::Point::Make(1, 1));
    pagLayer->transform->rotation = new pag::Property<float>(0);
    pagLayer->transform->opacity = new pag::Property<pag::Opacity>(pag::Opaque);

    auto *group = new pag::ShapeGroupElement();
    group->transform = new pag::ShapeTransform();
    group->transform->anchorPoint = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->position = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->scale = new pag::Property<pag::Point>(pag::Point::Make(1, 1));
    group->transform->skew = new pag::Property<float>(0);
    group->transform->skewAxis = new pag::Property<float>(0);
    group->transform->rotation = new pag::Property<float>(0);
    group->transform->opacity = new pag::Property<pag::Opacity>(pag::Opaque);

    auto *rect = new pag::RectangleElement();
    const float width = static_cast<float>(composition.width);
    const float height = static_cast<float>(composition.height);
    rect->position = new pag::Property<pag::Point>(pag::Point::Make(width * 0.5f, height * 0.5f));
    rect->size = new pag::Property<pag::Point>(pag::Point::Make(width, height));
    rect->roundness = new pag::Property<float>(composition.cornerRadius);

    auto *fill = new pag::FillElement();
    fill->blendMode = pag::BlendMode::Normal;
    fill->fillRule = pag::FillRule::NonZeroWinding;
    fill->color = new pag::Property<pag::Color>(ToPagColor(composition.backgroundColor));
    fill->opacity = new pag::Property<pag::Opacity>(pag::Opaque);

    group->elements.push_back(rect);
    group->elements.push_back(fill);
    pagLayer->contents.push_back(group);
    return pagLayer;
}

Expected<pag::Layer *, PagExportError> PagFileBuilder::buildFallbackLayer(
    const Layer &layer, const Composition &hostComposition) {
    if (!options_.allowBitmapFallback) {
        return Unexpected(PagExportError::MappingFailed);
    }
    if (frameSource_ == nullptr) {
        return Unexpected(PagExportError::MappingFailed);
    }
    Expected<BitmapFallbackResult, PagExportError> built = PagBitmapFallback::Build(
        document_, hostComposition, layer, options_.bitmapScale, frameSource_, nextCompositionId_++,
        nextLayerId_++, &warnings_);
    if (!built.hasValue()) {
        return Unexpected(built.error());
    }
    bitmapCompositions_.push_back(built.value().composition);
    return built.value().layer;
}

Expected<pag::VectorComposition *, PagExportError> PagFileBuilder::buildComposition(
    const Composition &composition) {
    if (composition.width <= 0 || composition.height <= 0 || composition.duration <= 0) {
        return Unexpected(PagExportError::InvalidOptions);
    }
    if (composition.frameRate.den == 0) {
        return Unexpected(PagExportError::InvalidOptions);
    }

    layerByEntity_.clear();
    currentHostComposition_ = &composition;

    auto *pagComposition = new pag::VectorComposition();
    pagComposition->id = nextCompositionId_++;
    pagComposition->width = composition.width;
    pagComposition->height = composition.height;
    pagComposition->duration = composition.duration;
    pagComposition->frameRate = static_cast<float>(composition.frameRate.num) /
        static_cast<float>(composition.frameRate.den);
    pagComposition->backgroundColor = ToPagColor(composition.backgroundColor);
    compositionByEntity_[composition.id.value] = pagComposition;

    // Build in MS order (bottom→top) so Group fallback can skip descendants before they emit.
    // Then reverse into PAG File order (index 0 = topmost; CompositionRenderer draws back→front).
    std::unordered_set<uint64_t> skippedDescendants;
    std::vector<pag::Layer *> contentLayers;
    contentLayers.reserve(composition.layers.size());
    for (const auto &layerPtr : composition.layers) {
        if (layerPtr == nullptr || skippedDescendants.count(layerPtr->id.value) != 0) {
            continue;
        }

        Expected<pag::Layer *, PagExportError> layerResult =
            Expected<pag::Layer *, PagExportError>(Unexpected(PagExportError::MappingFailed));
        if (needsBitmapFallback(*layerPtr)) {
            layerResult = buildFallbackLayer(*layerPtr, composition);
            if (layerResult.hasValue() && layerPtr->type() == LayerType::Group) {
                collectDescendants(layerPtr->id, &skippedDescendants);
            }
        } else {
            layerResult = buildLayer(*layerPtr);
        }
        if (!layerResult.hasValue()) {
            // Soft-fail MappingFailed (and fallback unavailable): skip layer, keep exporting.
            if (layerResult.error() == PagExportError::MappingFailed) {
                const char *code = "LayerSkipped";
                const char *message = "Layer skipped due to mapping failure";
                if (needsBitmapFallback(*layerPtr)) {
                    if (options_.allowBitmapFallback && frameSource_ == nullptr) {
                        code = "BitmapFallbackUnavailable";
                        message = "Bitmap fallback is not available yet";
                    } else if (layerPtr->followPath.enabled) {
                        code = "UnsupportedFollowPath";
                        message = "FollowPath layer skipped";
                    }
                }
                skipLayerWithWarning(*layerPtr, code, message, &skippedDescendants);
                continue;
            }
            for (pag::Layer *owned : contentLayers) {
                delete owned;
            }
            delete pagComposition;
            return Unexpected(layerResult.error());
        }
        pag::Layer *pagLayer = layerResult.value();
        pagLayer->containingComposition = pagComposition;
        contentLayers.push_back(pagLayer);
        layerByEntity_[layerPtr->id.value] = pagLayer;
    }

    for (const auto &layerPtr : composition.layers) {
        auto childIt = layerByEntity_.find(layerPtr->id.value);
        if (childIt == layerByEntity_.end()) {
            continue;
        }
        if (layerPtr->parentId.isValid()) {
            auto parentIt = layerByEntity_.find(layerPtr->parentId.value);
            if (parentIt == layerByEntity_.end()) {
                Warn(&warnings_, layerPtr->id, "SkippedParent",
                     "Parent layer was skipped; clearing parent link");
            } else {
                childIt->second->parent = parentIt->second;
            }
        }
        if (layerPtr->trackMatteType != TrackMatteType::None) {
            if (!layerPtr->trackMatteLayerId.isValid()) {
                Warn(&warnings_, layerPtr->id, "SkippedTrackMatte",
                     "Track matte source missing; clearing track matte");
                continue;
            }
            auto matteIt = layerByEntity_.find(layerPtr->trackMatteLayerId.value);
            if (matteIt == layerByEntity_.end()) {
                Warn(&warnings_, layerPtr->id, "SkippedTrackMatte",
                     "Track matte source was skipped; clearing track matte");
                continue;
            }
            childIt->second->trackMatteType = MapTrackMatte(layerPtr->trackMatteType);
            childIt->second->trackMatteLayer = matteIt->second;
        }
    }

    std::reverse(contentLayers.begin(), contentLayers.end());

    // PAG decode rebinds track matte to layers[index-1]; place matte immediately above target.
    for (size_t index = 0; index < contentLayers.size(); ++index) {
        pag::Layer *pagLayer = contentLayers[index];
        if (pagLayer->trackMatteLayer == nullptr ||
            pagLayer->trackMatteType == pag::TrackMatteType::None) {
            continue;
        }
        pag::Layer *matteLayer = pagLayer->trackMatteLayer;
        size_t matteIndex = contentLayers.size();
        for (size_t probe = 0; probe < contentLayers.size(); ++probe) {
            if (contentLayers[probe] == matteLayer) {
                matteIndex = probe;
                break;
            }
        }
        if (matteIndex >= contentLayers.size()) {
            continue;
        }
        // Already immediately above this layer (PAG decode uses layers[index - 1]).
        if (matteIndex + 1 == index) {
            continue;
        }
        contentLayers.erase(contentLayers.begin() + static_cast<std::ptrdiff_t>(matteIndex));
        if (matteIndex < index) {
            --index;
        }
        contentLayers.insert(contentLayers.begin() + static_cast<std::ptrdiff_t>(index), matteLayer);
        pagLayer->trackMatteLayer = matteLayer;
    }

    // AE/PAG: track-matte sources are not drawn as normal layers (CompositionRenderer skips
    // !isActive); they are only sampled via trackMatteLayer. Matches MS usedAsMatteOnly.
    for (pag::Layer *pagLayer : contentLayers) {
        if (pagLayer->trackMatteLayer != nullptr &&
            pagLayer->trackMatteType != pag::TrackMatteType::None) {
            pagLayer->trackMatteLayer->isActive = false;
        }
    }

    pagComposition->layers = std::move(contentLayers);

    // Many PAG viewers ignore Composition.backgroundColor and only draw layers; always emit a
    // bottom backdrop so the MS composition background is visible.
    pag::ShapeLayer *backdrop = buildCompositionBackdrop(composition);
    backdrop->containingComposition = pagComposition;
    pagComposition->layers.push_back(backdrop);

    // PAG has no composition cornerRadius: rounded fill already on backdrop + Precomp mask clip.
    if (composition.cornerRadius > 0.0f) {
        pag::VectorComposition *clipped = wrapCompositionWithCornerClip(pagComposition, composition);
        Warn(&warnings_, composition.id, "CompositionCornerRadiusApproximated",
             "Composition corner radius exported as rounded fill and clip mask");
        currentHostComposition_ = nullptr;
        return clipped;
    }

    currentHostComposition_ = nullptr;
    return pagComposition;
}

Expected<pag::Layer *, PagExportError> PagFileBuilder::buildLayer(const Layer &layer) {
    switch (layer.type()) {
        case LayerType::Shape: {
            Expected<pag::ShapeLayer *, PagExportError> shape = buildShapeLayer(layer);
            if (!shape.hasValue()) {
                return Unexpected(shape.error());
            }
            return shape.value();
        }
        case LayerType::Group: {
            Expected<pag::NullLayer *, PagExportError> nullLayer = buildNullLayer(layer);
            if (!nullLayer.hasValue()) {
                return Unexpected(nullLayer.error());
            }
            return nullLayer.value();
        }
        case LayerType::Text: {
            Expected<pag::TextLayer *, PagExportError> text = buildTextLayer(layer);
            if (!text.hasValue()) {
                return Unexpected(text.error());
            }
            return text.value();
        }
        case LayerType::Image: {
            Expected<pag::ImageLayer *, PagExportError> image = buildImageLayer(layer);
            if (!image.hasValue()) {
                return Unexpected(image.error());
            }
            return image.value();
        }
        case LayerType::Precomp: {
            Expected<pag::PreComposeLayer *, PagExportError> precomp = buildPrecompLayer(layer);
            if (!precomp.hasValue()) {
                return Unexpected(precomp.error());
            }
            return precomp.value();
        }
    }
    return Unexpected(PagExportError::MappingFailed);
}

Expected<pag::NullLayer *, PagExportError> PagFileBuilder::buildNullLayer(const Layer &layer) {
    auto *pagLayer = new pag::NullLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pagLayer;
        return Unexpected(filled.error());
    }
    return pagLayer;
}

Expected<pag::TextLayer *, PagExportError> PagFileBuilder::buildTextLayer(const Layer &layer) {
    const auto &content = static_cast<const TextContent &>(*layer.content);
    auto *pagLayer = new pag::TextLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pagLayer;
        return Unexpected(filled.error());
    }
    pagLayer->sourceText = buildSourceText(layer, content);
    return pagLayer;
}

pag::TextDocumentHandle PagFileBuilder::makeTextDocument(const Layer &layer,
                                                         const TextContent &content,
                                                         FrameTime time) {
    auto document = std::make_shared<pag::TextDocument>();
    document->text = content.text.evaluate(time);
    document->fontFamily = content.fontFamily;
    document->fontStyle = content.fontStyle;
    const float fontSize = content.fontSize;
    document->fontSize = fontSize;
    document->justification = MapAlign(content.align);

    // boxTextMode maps directly to PAG/AE paragraph (box) text.
    // Without font metrics at export time, approximate first baseline as ~ascent from box top
    // so PAG does not treat firstBaseLine==0 as vertically-centered box text.
    constexpr float kAscentFactor = 0.8f;
    if (content.boxTextMode) {
        document->boxText = true;
        document->boxTextPos = pag::Point::Zero();
        document->boxTextSize = pag::Point::Make(content.size.x, content.size.y);
        document->firstBaseLine = document->boxTextPos.y + fontSize * kAscentFactor;
    } else {
        document->boxText = false;
        document->boxTextPos = pag::Point::Zero();
        document->boxTextSize = pag::Point::Zero();
        document->firstBaseLine = 0.0f;
    }

    // PAG TextDocument supports one fill + one stroke; MS draws styles in order (fill then stroke).
    document->applyFill = false;
    document->applyStroke = false;
    document->strokeOverFill = true;
    bool sawFill = false;
    bool sawStroke = false;
    for (const auto &stylePtr : layer.styles) {
        if (stylePtr == nullptr) {
            continue;
        }
        if (stylePtr->type() == LayerStyleType::Fill) {
            if (sawFill) {
                Warn(&warnings_, layer.id, "TextStyleApproximated",
                     "Multiple text fills collapsed to the first FillStyle");
                continue;
            }
            const auto &fill = static_cast<const FillStyle &>(*stylePtr);
            document->applyFill = true;
            document->fillColor = ToPagColor(fill.color.evaluate(time));
            sawFill = true;
            continue;
        }
        if (stylePtr->type() == LayerStyleType::Stroke) {
            if (sawStroke) {
                Warn(&warnings_, layer.id, "TextStyleApproximated",
                     "Multiple text strokes collapsed to the first StrokeStyle");
                continue;
            }
            const auto &stroke = static_cast<const StrokeStyle &>(*stylePtr);
            const float width = stroke.width.evaluate(time);
            if (width <= 0.0f) {
                continue;
            }
            document->applyStroke = true;
            document->strokeColor = ToPagColor(stroke.color.evaluate(time));
            document->strokeWidth = width;
            sawStroke = true;
        }
    }
    if (!sawFill) {
        document->applyFill = true;
        document->fillColor = pag::Black;
    }
    return document;
}

pag::Property<pag::TextDocumentHandle> *PagFileBuilder::buildSourceText(const Layer &layer,
                                                                        const TextContent &content) {
    if (!content.text.isAnimated()) {
        return new pag::Property<pag::TextDocumentHandle>(makeTextDocument(layer, content, 0));
    }
    std::set<FrameTime> times;
    CollectTextKeyframeTimes(content.text, &times);
    if (times.size() < 2) {
        const FrameTime time = times.empty() ? 0 : *times.begin();
        return new pag::Property<pag::TextDocumentHandle>(makeTextDocument(layer, content, time));
    }
    std::vector<FrameTime> ordered(times.begin(), times.end());
    std::vector<pag::Keyframe<pag::TextDocumentHandle> *> keyframes;
    for (size_t index = 0; index + 1 < ordered.size(); ++index) {
        // DiscreteProperty / Hold: base Keyframe returns startValue (no Interpolate).
        auto *keyframe = new pag::Keyframe<pag::TextDocumentHandle>();
        keyframe->startTime = ordered[index];
        keyframe->endTime = ordered[index + 1];
        keyframe->startValue = makeTextDocument(layer, content, ordered[index]);
        keyframe->endValue = makeTextDocument(layer, content, ordered[index + 1]);
        keyframe->interpolationType = pag::KeyframeInterpolationType::Hold;
        keyframes.push_back(keyframe);
    }
    return new pag::AnimatableProperty<pag::TextDocumentHandle>(keyframes);
}

Expected<pag::ImageBytes *, PagExportError> PagFileBuilder::imageBytesForAsset(EntityId assetId,
                                                                               EntityId layerId) {
    if (!assetId.isValid()) {
        Warn(&warnings_, layerId, "ImageAssetMissing", "Image layer has no asset");
        return Unexpected(PagExportError::MappingFailed);
    }
    auto existing = imageBytesByAsset_.find(assetId.value);
    if (existing != imageBytesByAsset_.end()) {
        return existing->second;
    }
    const Asset *asset = nullptr;
    for (const auto &candidate : document_.assets) {
        if (candidate.id == assetId) {
            asset = &candidate;
            break;
        }
    }
    if (asset == nullptr || asset->path.empty() || asset->width <= 0 || asset->height <= 0) {
        Warn(&warnings_, layerId, "ImageAssetMissing", "Image asset missing or invalid");
        return Unexpected(PagExportError::MappingFailed);
    }
    const std::string fullPath = JoinPath(document_.projectRoot, asset->path);
    int encodedWidth = 0;
    int encodedHeight = 0;
    std::unique_ptr<pag::ByteData> bytes = LoadImageAsWebP(fullPath, &encodedWidth, &encodedHeight);
    if (bytes == nullptr || bytes->length() == 0 || encodedWidth <= 0 || encodedHeight <= 0) {
        Warn(&warnings_, layerId, "ImageAssetMissing", "Failed to read or encode image as WebP");
        return Unexpected(PagExportError::MappingFailed);
    }
    auto *imageBytes = new pag::ImageBytes();
    imageBytes->id = nextImageId_++;
    // WriteImages requires file pixel size == width * scaleFactor (scaleFactor defaults to 1).
    imageBytes->width = encodedWidth;
    imageBytes->height = encodedHeight;
    imageBytes->fileBytes = bytes.release();
    imageBytesByAsset_[assetId.value] = imageBytes;
    imageBytesList_.push_back(imageBytes);
    return imageBytes;
}

Expected<pag::ImageLayer *, PagExportError> PagFileBuilder::buildImageLayer(const Layer &layer) {
    const auto &content = static_cast<const ImageContent &>(*layer.content);
    Expected<pag::ImageBytes *, PagExportError> imageBytes =
        imageBytesForAsset(content.assetId, layer.id);
    if (!imageBytes.hasValue()) {
        return Unexpected(imageBytes.error());
    }
    auto *pagLayer = new pag::ImageLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pagLayer;
        return Unexpected(filled.error());
    }
    pagLayer->imageBytes = imageBytes.value();
    // Keep scaleMode for runtime image replacement; design-time container is baked into transform.
    pagLayer->imageFillRule = new pag::ImageFillRule();
    pagLayer->imageFillRule->scaleMode = MapScaleMode(content.scaleMode);
    applyImageContainerFit(pagLayer, layer, content, imageBytes.value()->width,
                           imageBytes.value()->height);
    return pagLayer;
}

Expected<pag::PreComposeLayer *, PagExportError> PagFileBuilder::buildPrecompLayer(
    const Layer &layer) {
    const auto &content = static_cast<const PrecompContent &>(*layer.content);
    auto compositionIt = compositionByEntity_.find(content.compositionId.value);
    if (compositionIt == compositionByEntity_.end()) {
        return Unexpected(PagExportError::MappingFailed);
    }
    auto *pagLayer = new pag::PreComposeLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pagLayer;
        return Unexpected(filled.error());
    }
    pagLayer->composition = compositionIt->second;
    pagLayer->compositionStartTime = layer.startTime;
    pagLayer->stretch = MakeStretchRatio(layer.timeStretch);
    return pagLayer;
}

Expected<pag::ShapeLayer *, PagExportError> PagFileBuilder::buildShapeLayer(const Layer &layer) {
    const auto &content = static_cast<const ShapeContent &>(*layer.content);
    if (content.geometry == nullptr) {
        return Unexpected(PagExportError::MappingFailed);
    }

    auto *pagLayer = new pag::ShapeLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pagLayer;
        return Unexpected(filled.error());
    }

    auto *group = new pag::ShapeGroupElement();
    group->transform = new pag::ShapeTransform();
    group->transform->anchorPoint = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->position = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->scale = new pag::Property<pag::Point>(pag::Point::Make(1, 1));
    group->transform->skew = new pag::Property<float>(0);
    group->transform->skewAxis = new pag::Property<float>(0);
    group->transform->rotation = new pag::Property<float>(0);
    group->transform->opacity = new pag::Property<pag::Opacity>(pag::Opaque);

    Expected<pag::ShapeElement *, PagExportError> geometry =
        buildGeometry(*content.geometry, layer.id);
    if (!geometry.hasValue()) {
        delete group;
        delete pagLayer;
        return Unexpected(geometry.error());
    }
    group->elements.push_back(geometry.value());

    Expected<void, PagExportError> styles = appendStyles(&group->elements, layer);
    if (!styles.hasValue()) {
        delete group;
        delete pagLayer;
        return Unexpected(styles.error());
    }

    pagLayer->contents.push_back(group);
    return pagLayer;
}

Expected<pag::ShapeElement *, PagExportError> PagFileBuilder::buildGeometry(
    const ShapeElement &element, EntityId layerId) {
    switch (element.type()) {
        case ShapeType::Rect: {
            const auto &rect = static_cast<const ShapeRect &>(element);
            auto *pagRect = new pag::RectangleElement();
            pagRect->position = ConvertPoint(rect.position, &warnings_, layerId);
            pagRect->size = ConvertPoint(rect.size, &warnings_, layerId);
            pagRect->roundness = ConvertFloat(rect.cornerRadius, &warnings_, layerId);
            return pagRect;
        }
        case ShapeType::Ellipse: {
            const auto &ellipse = static_cast<const ShapeEllipse &>(element);
            auto *pagEllipse = new pag::EllipseElement();
            pagEllipse->position = ConvertPoint(ellipse.position, &warnings_, layerId);
            pagEllipse->size = ConvertPoint(ellipse.size, &warnings_, layerId);
            return pagEllipse;
        }
        case ShapeType::Path: {
            const auto &path = static_cast<const ShapePath &>(element);
            auto *pagPath = new pag::ShapePathElement();
            pagPath->shapePath = ConvertPath(path.path, &warnings_, layerId);
            return pagPath;
        }
        case ShapeType::TrimPath:
            return Unexpected(PagExportError::MappingFailed);
    }
    return Unexpected(PagExportError::MappingFailed);
}

Expected<void, PagExportError> PagFileBuilder::appendStyles(
    std::vector<pag::ShapeElement *> *contents, const Layer &layer) {
    for (const auto &stylePtr : layer.styles) {
        if (stylePtr->type() == LayerStyleType::Fill) {
            const auto &fill = static_cast<const FillStyle &>(*stylePtr);
            auto *pagFill = new pag::FillElement();
            bool blendOk = true;
            pagFill->blendMode = mapBlendMode(fill.blendMode, layer.id, &blendOk);
            if (!blendOk) {
                delete pagFill;
                return Unexpected(PagExportError::MappingFailed);
            }
            pagFill->fillRule = MapFillRule(fill.fillRule);
            pagFill->color = ConvertColor(fill.color, &warnings_, layer.id);
            pagFill->opacity = new pag::Property<pag::Opacity>(pag::Opaque);
            contents->push_back(pagFill);
            continue;
        }
        if (stylePtr->type() == LayerStyleType::Stroke) {
            const auto &stroke = static_cast<const StrokeStyle &>(*stylePtr);
            if (stroke.position != StrokePosition::Center) {
                Warn(&warnings_, layer.id, "UnsupportedStrokePosition",
                     "Stroke position approximated as Center");
            }
            if (!TrimIsDefault(stroke)) {
                auto *trim = new pag::TrimPathsElement();
                trim->start = ConvertPercent(stroke.trimStart, &warnings_, layer.id);
                trim->end = ConvertPercent(stroke.trimEnd, &warnings_, layer.id);
                trim->offset = ConvertFloat(stroke.trimOffset, &warnings_, layer.id);
                contents->push_back(trim);
            }
            auto *pagStroke = new pag::StrokeElement();
            bool blendOk = true;
            pagStroke->blendMode = mapBlendMode(stroke.blendMode, layer.id, &blendOk);
            if (!blendOk) {
                delete pagStroke;
                return Unexpected(PagExportError::MappingFailed);
            }
            pagStroke->color = ConvertColor(stroke.color, &warnings_, layer.id);
            pagStroke->opacity = new pag::Property<pag::Opacity>(pag::Opaque);
            pagStroke->strokeWidth = ConvertFloat(stroke.width, &warnings_, layer.id);
            pagStroke->lineCap = MapLineCap(stroke.cap);
            pagStroke->lineJoin = MapLineJoin(stroke.join);
            pagStroke->miterLimit = new pag::Property<float>(stroke.miterLimit);
            contents->push_back(pagStroke);
            continue;
        }
        return Unexpected(PagExportError::MappingFailed);
    }
    return Expected<void, PagExportError>();
}

Expected<void, PagExportError> PagFileBuilder::appendMasks(pag::Layer *pagLayer,
                                                           const Layer &layer) {
    for (const Mask &mask : layer.masks) {
        auto *pagMask = new pag::MaskData();
        pagMask->id = nextMaskId_++;
        pagMask->inverted = mask.inverted;
        pagMask->maskMode = MapMaskMode(mask.mode);
        pagMask->maskPath = ConvertPath(mask.path, &warnings_, layer.id);
        // PAG excludeVaryingRanges always touches opacity/expansion; keep them non-null.
        pagMask->maskOpacity = ConvertOpacity(mask.opacity, &warnings_, layer.id);
        pagMask->maskExpansion = ConvertFloat(mask.expansion, &warnings_, layer.id);
        if (mask.feather.isAnimated() || mask.feather.staticValue() != 0.0f) {
            Animatable<Vec2> featherPoint;
            if (mask.feather.isAnimated()) {
                for (const auto &keyframe : mask.feather.keyframes()) {
                    Keyframe<Vec2> pointKeyframe;
                    pointKeyframe.time = keyframe.time;
                    pointKeyframe.value = Vec2{keyframe.value, keyframe.value};
                    pointKeyframe.easing = keyframe.easing;
                    featherPoint.addKeyframe(pointKeyframe);
                }
            } else {
                const float value = mask.feather.staticValue();
                featherPoint.setStaticValue(Vec2{value, value});
            }
            pagMask->maskFeather = ConvertPoint(featherPoint, &warnings_, layer.id);
        }
        pagLayer->masks.push_back(pagMask);
    }
    return Expected<void, PagExportError>();
}

pag::Transform2D *PagFileBuilder::buildTransform(const Transform &transform, EntityId layerId) {
    auto *pagTransform = new pag::Transform2D();
    pagTransform->anchorPoint = ConvertPoint(transform.anchorPoint, &warnings_, layerId);
    pagTransform->position = ConvertPoint(transform.position, &warnings_, layerId);
    pagTransform->scale = ConvertPoint(transform.scale, &warnings_, layerId);
    pagTransform->rotation = ConvertFloat(transform.rotation, &warnings_, layerId);
    pagTransform->opacity = ConvertOpacity(transform.opacity, &warnings_, layerId);
    return pagTransform;
}

pag::BlendMode PagFileBuilder::mapBlendMode(BlendMode mode, EntityId layerId, bool *ok) {
    *ok = true;
    switch (mode) {
        case BlendMode::Normal:
            return pag::BlendMode::Normal;
        case BlendMode::Multiply:
            return pag::BlendMode::Multiply;
        case BlendMode::Screen:
            return pag::BlendMode::Screen;
        case BlendMode::Overlay:
            return pag::BlendMode::Overlay;
        case BlendMode::Darken:
            return pag::BlendMode::Darken;
        case BlendMode::Lighten:
            return pag::BlendMode::Lighten;
        case BlendMode::ColorDodge:
            return pag::BlendMode::ColorDodge;
        case BlendMode::ColorBurn:
            return pag::BlendMode::ColorBurn;
        case BlendMode::HardLight:
            return pag::BlendMode::HardLight;
        case BlendMode::SoftLight:
            return pag::BlendMode::SoftLight;
        case BlendMode::Difference:
            return pag::BlendMode::Difference;
        case BlendMode::Exclusion:
            return pag::BlendMode::Exclusion;
        case BlendMode::Hue:
            return pag::BlendMode::Hue;
        case BlendMode::Saturation:
            return pag::BlendMode::Saturation;
        case BlendMode::Color:
            return pag::BlendMode::Color;
        case BlendMode::Luminosity:
            return pag::BlendMode::Luminosity;
        case BlendMode::Add:
            return pag::BlendMode::Add;
    }
    *ok = false;
    Warn(&warnings_, layerId, "UnsupportedBlendMode", "Blend mode has no PAG mapping");
    return pag::BlendMode::Normal;
}

Expected<void, PagExportError> PagFileBuilder::fillCommonLayer(pag::Layer *pagLayer,
                                                               const Layer &layer) {
    bool blendOk = true;
    pagLayer->blendMode = mapBlendMode(layer.blendMode, layer.id, &blendOk);
    if (!blendOk) {
        return Unexpected(PagExportError::MappingFailed);
    }

    const FrameTime duration = layer.outPoint - layer.inPoint;
    if (duration <= 0) {
        return Unexpected(PagExportError::MappingFailed);
    }

    pagLayer->id = nextLayerId_++;
    pagLayer->name = layer.name;
    pagLayer->startTime = layer.inPoint;
    pagLayer->duration = duration;
    pagLayer->isActive = layer.visible;
    pagLayer->stretch = MakeStretchRatio(layer.timeStretch);
    pagLayer->transform = buildTransform(layer.transform, layer.id);

    Expected<void, PagExportError> masks = appendMasks(pagLayer, layer);
    if (!masks.hasValue()) {
        return Unexpected(masks.error());
    }
    return Expected<void, PagExportError>();
}

}  // namespace pag_export
}  // namespace motion
