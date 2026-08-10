#include "PagFileBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/BezierPathTransform.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Time.h"
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
#include "MotionStudio/model/TextPath.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/FollowPathEval.h"
#include "MotionStudio/render/ImageScaleLayout.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "PagAnimatableConvert.h"
#include "PagBitmapFallback.h"
#include "PagBmpSuffix.h"
#include "PagExportErrorUtil.h"
#include "PagStrokeOutline.h"
#include "codec/utils/WebpDecoder.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Pixmap.h"

namespace motion {
namespace pag_export {
namespace {

// MS lays out point text from the box top (first baseline ≈ ascent).
// PAG/AE point text places the layer origin on the first baseline.
// Fallback when PagExportOptions::textAscentResolver is unset / returns <= 0.
constexpr float kTextAscentFactor = 0.8f;

float ResolveTextAscent(const PagExportOptions &options, const std::string &fontFamily,
                        const std::string &fontStyle, float fontSize) {
    if (options.textAscentResolver) {
        const float ascent = options.textAscentResolver(fontFamily, fontStyle, fontSize);
        if (ascent > 0.0f) {
            return ascent;
        }
    }
    return fontSize * kTextAscentFactor;
}

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

template <typename T>
void CollectAnimatableTimes(const Animatable<T> &value, std::set<FrameTime> *times) {
    for (const auto &keyframe : value.keyframes()) {
        times->insert(keyframe.time);
    }
}

void CollectTransformTimes(const Transform &transform, std::set<FrameTime> *times) {
    CollectAnimatableTimes(transform.anchorPoint, times);
    CollectAnimatableTimes(transform.position, times);
    CollectAnimatableTimes(transform.scale, times);
    CollectAnimatableTimes(transform.rotation, times);
}

void CollectPathGeometryTimes(const Layer &pathLayer, std::set<FrameTime> *times) {
    if (pathLayer.content == nullptr || pathLayer.content->type() != LayerType::Shape) {
        return;
    }
    const auto &shapeContent = static_cast<const ShapeContent &>(*pathLayer.content);
    if (shapeContent.geometry == nullptr) {
        return;
    }
    const ShapeElement &element = *shapeContent.geometry;
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            CollectAnimatableTimes(shape.path, times);
            break;
        }
        case ShapeType::Rect: {
            const auto &shape = static_cast<const ShapeRect &>(element);
            CollectAnimatableTimes(shape.position, times);
            CollectAnimatableTimes(shape.size, times);
            CollectAnimatableTimes(shape.cornerRadius, times);
            break;
        }
        case ShapeType::Ellipse: {
            const auto &shape = static_cast<const ShapeEllipse &>(element);
            CollectAnimatableTimes(shape.position, times);
            CollectAnimatableTimes(shape.size, times);
            break;
        }
        case ShapeType::TrimPath:
            break;
    }
}

std::optional<BezierPath> ResolveTextPathLocal(const Document &document, const Layer &textLayer,
                                               const TextPath &textPath, FrameTime time) {
    if (!textPath.enabled || !textPath.pathLayerId.isValid() ||
        textPath.pathLayerId == textLayer.id) {
        return std::nullopt;
    }
    const Layer *pathLayer = document.entityIndex().findLayer(textPath.pathLayerId);
    if (pathLayer == nullptr) {
        return std::nullopt;
    }
    const PreviewTime preview = static_cast<PreviewTime>(time);
    const std::optional<BezierPath> optPath = EvaluateLayerPath(*pathLayer, preview);
    if (!optPath) {
        return std::nullopt;
    }
    std::vector<EntityId> pathParentVisiting;
    std::vector<EntityId> pathFollowVisiting;
    const Mat3 pathWorld =
        FollowAwareWorldTransform(document, *pathLayer, preview, Mat3::Identity(),
                                  pathParentVisiting, pathFollowVisiting);
    std::vector<EntityId> textParentVisiting;
    std::vector<EntityId> textFollowVisiting;
    const Mat3 textWorld =
        FollowAwareWorldTransform(document, textLayer, preview, Mat3::Identity(),
                                  textParentVisiting, textFollowVisiting);
    Mat3 textInverse = Mat3::Identity();
    if (!textWorld.tryInvert(textInverse)) {
        return std::nullopt;
    }
    return TransformBezierPath(*optPath, textInverse * pathWorld);
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
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "",
                                             "invalid PAG export options"));
    }

    Expected<void, PagExportError> collected = collectCompositionOrder(rootComposition_.id);
    if (!collected.hasValue()) {
        return Unexpected(collected.error());
    }
    collectBitmapForcedCompositions();

    std::vector<pag::Composition *> orderedCompositions;
    orderedCompositions.reserve(compositionOrder_.size());
    for (EntityId compositionId : compositionOrder_) {
        const Composition *source = findComposition(compositionId);
        if (source == nullptr) {
            DeleteCompositions(&orderedCompositions);
            DeleteCompositions(&bitmapCompositions_);
            DeleteCompositions(&nestedCompositions_);
            return Unexpected(MakePagExportError(PagExportErrorKind::InvalidComposition, {}, "", "",
                                                 "composition not found"));
        }
        Expected<pag::Composition *, PagExportError> built = buildOneComposition(*source);
        if (!built.hasValue()) {
            DeleteCompositions(&orderedCompositions);
            DeleteCompositions(&bitmapCompositions_);
            DeleteCompositions(&nestedCompositions_);
            return Unexpected(built.error());
        }
        orderedCompositions.push_back(built.value());
    }

    // Layer-level bitmap comps first; nested clip inners; MS composition roots last (main = back).
    std::vector<pag::Composition *> compositions;
    compositions.reserve(bitmapCompositions_.size() + nestedCompositions_.size() +
                         orderedCompositions.size());
    compositions.insert(compositions.end(), bitmapCompositions_.begin(), bitmapCompositions_.end());
    compositions.insert(compositions.end(), nestedCompositions_.begin(), nestedCompositions_.end());
    compositions.insert(compositions.end(), orderedCompositions.begin(), orderedCompositions.end());
    bitmapCompositions_.clear();
    nestedCompositions_.clear();

    std::shared_ptr<pag::File> file = pag::Codec::VerifyAndMake(compositions, imageBytesList_);
    imageBytesList_.clear();
    if (file == nullptr) {
        return Unexpected(MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "",
                                             "PAG encode failed"));
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
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidComposition, {}, "", "", "composition not found"));
    }
    if (visitedCompositions_.count(compositionId.value) != 0) {
        return Expected<void, PagExportError>();
    }
    visitedCompositions_.insert(compositionId.value);
    const Composition *composition = findComposition(compositionId);
    if (composition == nullptr) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidComposition, {}, "", "", "composition not found"));
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

void PagFileBuilder::collectBitmapForcedCompositions() {
    bitmapForcedCompositionIds_.clear();
    for (EntityId compositionId : compositionOrder_) {
        const Composition *composition = findComposition(compositionId);
        if (composition == nullptr) {
            continue;
        }
        if (HasBmpSuffix(composition->name)) {
            bitmapForcedCompositionIds_.insert(compositionId.value);
        }
        for (const auto &layerPtr : composition->layers) {
            if (layerPtr == nullptr || layerPtr->type() != LayerType::Precomp) {
                continue;
            }
            if (!HasBmpSuffix(layerPtr->name)) {
                continue;
            }
            const auto &content = static_cast<const PrecompContent &>(*layerPtr->content);
            if (content.compositionId.isValid()) {
                bitmapForcedCompositionIds_.insert(content.compositionId.value);
            }
        }
    }
}

bool PagFileBuilder::isBitmapForcedComposition(EntityId compositionId) const {
    return bitmapForcedCompositionIds_.count(compositionId.value) != 0;
}

bool PagFileBuilder::needsBitmapFallback(const Layer &layer) const {
    return layer.followPath.enabled;
}

PagExportError PagFileBuilder::unsupportedWithoutBmpError(const Layer &layer) const {
    const std::string name = layer.name.empty() ? "(unnamed layer)" : layer.name;
    if (layer.followPath.enabled) {
        return MakePagExportError(
            PagExportErrorKind::MappingFailed, layer.id, name, "UnsupportedFollowPath",
            "Layer \"" + name +
                "\": FollowPath is not supported in vector PAG export; add \"_bmp\" "
                "suffix to the layer or composition name, or disable FollowPath.");
    }
    return MakePagExportError(
        PagExportErrorKind::MappingFailed, layer.id, name, "LayerSkipped",
        "Layer \"" + name +
            "\": content cannot be mapped to vector PAG; add \"_bmp\" suffix to export as "
            "bitmap.");
}

Expected<pag::Composition *, PagExportError> PagFileBuilder::buildOneComposition(
    const Composition &composition) {
    if (isBitmapForcedComposition(composition.id)) {
        Expected<pag::BitmapComposition *, PagExportError> built =
            buildBitmapComposition(composition);
        if (!built.hasValue()) {
            return Unexpected(built.error());
        }
        return built.value();
    }
    Expected<pag::VectorComposition *, PagExportError> built = buildComposition(composition);
    if (!built.hasValue()) {
        return Unexpected(built.error());
    }
    return built.value();
}

Expected<pag::BitmapComposition *, PagExportError> PagFileBuilder::buildBitmapComposition(
    const Composition &composition) {
    if (!options_.allowBitmapExport) {
        const std::string name =
            composition.name.empty() ? "(unnamed composition)" : composition.name;
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, composition.id, name, "BitmapForcedByCompositionName",
            "Composition \"" + name +
                "\": bitmap export is disabled (allowBitmapExport=false) but _bmp was requested."));
    }
    if (frameSource_ == nullptr) {
        const std::string name =
            composition.name.empty() ? "(unnamed composition)" : composition.name;
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, composition.id, name, "BitmapForcedByCompositionName",
            "Composition \"" + name +
                "\": bitmap export requires a BitmapFrameSource."));
    }
    Expected<pag::BitmapComposition *, PagExportError> built = PagBitmapFallback::BuildComposition(
        document_, composition, options_.bitmapScale, frameSource_, nextCompositionId_++,
        &warnings_);
    if (!built.hasValue()) {
        return Unexpected(built.error());
    }
    compositionByEntity_[composition.id.value] = built.value();
    return built.value();
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
    mask->maskPath = ConvertBezierPath(maskAnimatable, &warnings_, composition.id);
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
    mask->maskPath = ConvertBezierPath(clipAnimatable, &warnings_, layer.id);
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
    fill->opacity = new pag::Property<pag::Opacity>(ToPagOpacity(composition.backgroundColor.a));

    group->elements.push_back(rect);
    group->elements.push_back(fill);
    pagLayer->contents.push_back(group);
    return pagLayer;
}

Expected<pag::Layer *, PagExportError> PagFileBuilder::buildFallbackLayer(
    const Layer &layer, const Composition &hostComposition) {
    const std::string name = layer.name.empty() ? "(unnamed layer)" : layer.name;
    if (!options_.allowBitmapExport) {
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, layer.id, name, "BitmapForcedByLayerName",
            "Layer \"" + name +
                "\": bitmap export is disabled (allowBitmapExport=false) but _bmp was requested."));
    }
    if (frameSource_ == nullptr) {
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, layer.id, name, "BitmapForcedByLayerName",
            "Layer \"" + name + "\": bitmap export requires a BitmapFrameSource."));
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
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "", "invalid PAG export options"));
    }
    if (composition.frameRate.den == 0) {
        return Unexpected(MakePagExportError(PagExportErrorKind::InvalidOptions, {}, "", "", "invalid PAG export options"));
    }

    layerByEntity_.clear();
    strokeSiblingsByEntity_.clear();
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

    // Build in MS order (bottom→top) so Group _bmp can skip descendants before they emit.
    // Then reverse into PAG File order (index 0 = topmost; CompositionRenderer draws back→front).
    std::unordered_set<uint64_t> skippedDescendants;
    std::vector<pag::Layer *> contentLayers;
    contentLayers.reserve(composition.layers.size());
    for (const auto &layerPtr : composition.layers) {
        if (layerPtr == nullptr || skippedDescendants.count(layerPtr->id.value) != 0) {
            continue;
        }

        Expected<std::vector<pag::Layer *>, PagExportError> layerResult =
            Expected<std::vector<pag::Layer *>, PagExportError>(Unexpected(
                MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "",
                                   "PAG export mapping failed")));
        const bool forceLayerBitmap =
            HasBmpSuffix(layerPtr->name) && layerPtr->type() != LayerType::Precomp;
        if (forceLayerBitmap) {
            Expected<pag::Layer *, PagExportError> fallback =
                buildFallbackLayer(*layerPtr, composition);
            if (fallback.hasValue()) {
                if (layerPtr->type() == LayerType::Group) {
                    collectDescendants(layerPtr->id, &skippedDescendants);
                }
                layerResult = std::vector<pag::Layer *>{fallback.value()};
            } else {
                layerResult = Unexpected(fallback.error());
            }
        } else if (needsBitmapFallback(*layerPtr)) {
            layerResult = Unexpected(unsupportedWithoutBmpError(*layerPtr));
        } else {
            layerResult = buildLayers(*layerPtr);
        }
        if (!layerResult.hasValue()) {
            for (pag::Layer *owned : contentLayers) {
                delete owned;
            }
            delete pagComposition;
            return Unexpected(layerResult.error());
        }
        std::vector<pag::Layer *> built = std::move(layerResult.value());
        if (built.empty()) {
            continue;
        }
        for (pag::Layer *pagLayer : built) {
            pagLayer->containingComposition = pagComposition;
            contentLayers.push_back(pagLayer);
        }
        layerByEntity_[layerPtr->id.value] = built.front();
        if (built.size() > 1) {
            strokeSiblingsByEntity_[layerPtr->id.value] =
                std::vector<pag::Layer *>(built.begin() + 1, built.end());
        }
    }

    auto applyToEntityLayers = [this](EntityId entityId, auto &&apply) {
        auto primaryIt = layerByEntity_.find(entityId.value);
        if (primaryIt == layerByEntity_.end()) {
            return;
        }
        apply(primaryIt->second);
        auto siblingIt = strokeSiblingsByEntity_.find(entityId.value);
        if (siblingIt == strokeSiblingsByEntity_.end()) {
            return;
        }
        for (pag::Layer *sibling : siblingIt->second) {
            apply(sibling);
        }
    };

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
                pag::Layer *parent = parentIt->second;
                applyToEntityLayers(layerPtr->id, [parent](pag::Layer *pagLayer) {
                    pagLayer->parent = parent;
                });
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
            const pag::TrackMatteType matteType = MapTrackMatte(layerPtr->trackMatteType);
            pag::Layer *matteLayer = matteIt->second;
            applyToEntityLayers(layerPtr->id, [matteType, matteLayer](pag::Layer *pagLayer) {
                pagLayer->trackMatteType = matteType;
                pagLayer->trackMatteLayer = matteLayer;
            });
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

Expected<std::vector<pag::Layer *>, PagExportError> PagFileBuilder::buildLayers(
    const Layer &layer) {
    switch (layer.type()) {
        case LayerType::Shape:
            return buildShapeLayers(layer);
        case LayerType::Group: {
            Expected<pag::NullLayer *, PagExportError> nullLayer = buildNullLayer(layer);
            if (!nullLayer.hasValue()) {
                return Unexpected(nullLayer.error());
            }
            return std::vector<pag::Layer *>{nullLayer.value()};
        }
        case LayerType::Text: {
            Expected<pag::TextLayer *, PagExportError> text = buildTextLayer(layer);
            if (!text.hasValue()) {
                return Unexpected(text.error());
            }
            return std::vector<pag::Layer *>{text.value()};
        }
        case LayerType::Image: {
            Expected<pag::ImageLayer *, PagExportError> image = buildImageLayer(layer);
            if (!image.hasValue()) {
                return Unexpected(image.error());
            }
            return std::vector<pag::Layer *>{image.value()};
        }
        case LayerType::Precomp: {
            Expected<pag::PreComposeLayer *, PagExportError> precomp = buildPrecompLayer(layer);
            if (!precomp.hasValue()) {
                return Unexpected(precomp.error());
            }
            return std::vector<pag::Layer *>{precomp.value()};
        }
    }
    return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
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

std::optional<Animatable<BezierPath>> PagFileBuilder::buildTextPathLocalAnimatable(
    const Layer &layer, const TextContent &content) {
    const TextPath &textPath = content.textPath;
    if (!textPath.enabled) {
        return std::nullopt;
    }
    const std::optional<BezierPath> atInPoint =
        ResolveTextPathLocal(document_, layer, textPath, layer.inPoint);
    if (!atInPoint) {
        return std::nullopt;
    }

    const Layer *pathLayer = document_.entityIndex().findLayer(textPath.pathLayerId);
    std::set<FrameTime> times;
    times.insert(layer.inPoint);
    CollectTransformTimes(layer.transform, &times);
    if (pathLayer != nullptr) {
        CollectTransformTimes(pathLayer->transform, &times);
        CollectPathGeometryTimes(*pathLayer, &times);
    }

    Animatable<BezierPath> localPath;
    if (times.size() <= 1) {
        localPath.setStaticValue(*atInPoint);
        return localPath;
    }

    bool allResolved = true;
    for (FrameTime time : times) {
        const std::optional<BezierPath> sample =
            ResolveTextPathLocal(document_, layer, textPath, time);
        if (!sample) {
            allResolved = false;
            break;
        }
        Keyframe<BezierPath> keyframe;
        keyframe.time = time;
        keyframe.value = *sample;
        localPath.addKeyframe(std::move(keyframe));
    }
    if (!allResolved || !localPath.isAnimated()) {
        localPath.clearKeyframes();
        localPath.setStaticValue(*atInPoint);
    }
    return localPath;
}

Expected<pag::TextLayer *, PagExportError> PagFileBuilder::buildTextLayer(const Layer &layer) {
    const auto &content = static_cast<const TextContent &>(*layer.content);
    auto *pagLayer = new pag::TextLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pagLayer;
        return Unexpected(filled.error());
    }

    bool forcePointText = !content.boxTextMode;
    if (content.textPath.enabled) {
        std::optional<Animatable<BezierPath>> localPath =
            buildTextPathLocalAnimatable(layer, content);
        // Spec: unresolved textPath falls back to ordinary point text (no pathOption).
        forcePointText = true;
        if (!localPath) {
            Warn(&warnings_, layer.id, "TextPathUnresolved",
                 "Text path could not be resolved; exported as ordinary point text");
        } else {
            // PAG codec stores pathOption->path as a Mask ID reference into layer.masks.
            // MaskMode::None keeps the geometry available without clipping the layer.
            auto *mask = new pag::MaskData();
            mask->id = nextMaskId_++;
            mask->inverted = false;
            mask->maskMode = pag::MaskMode::None;
            mask->maskPath = ConvertBezierPath(*localPath, &warnings_, layer.id);
            mask->maskOpacity = new pag::Property<pag::Opacity>(pag::Opaque);
            mask->maskExpansion = new pag::Property<float>(0);
            pagLayer->masks.push_back(mask);

            auto *pathOption = new pag::TextPathOptions();
            pathOption->path = mask;
            pathOption->reversedPath = new pag::Property<bool>(content.textPath.reversed);
            pathOption->perpendicularToPath =
                new pag::Property<bool>(content.textPath.perpendicular);
            pathOption->forceAlignment =
                new pag::Property<bool>(content.textPath.forceAlignment);
            pathOption->firstMargin =
                ConvertFloat(content.textPath.firstMargin, &warnings_, layer.id);
            pathOption->lastMargin =
                ConvertFloat(content.textPath.lastMargin, &warnings_, layer.id);
            pagLayer->pathOption = pathOption;
        }
    }

    // Point text: MS position is the content top; PAG position is the first baseline.
    // Valid textPath also exports as point text (boxTextMode ignored for layout).
    if (forcePointText && pagLayer->transform != nullptr &&
        pagLayer->transform->position != nullptr) {
        const float ascent = ResolveTextAscent(options_, content.fontFamily, content.fontStyle,
                                               content.fontSize);
        MapPointProperty(pagLayer->transform->position, [ascent](pag::Point point) {
            return pag::Point::Make(point.x, point.y + ascent);
        });
    }
    pagLayer->sourceText = buildSourceText(layer, content, forcePointText);
    return pagLayer;
}

pag::TextDocumentHandle PagFileBuilder::makeTextDocument(const Layer &layer,
                                                         const TextContent &content,
                                                         FrameTime time, bool forcePointText) {
    auto document = std::make_shared<pag::TextDocument>();
    document->text = content.text.evaluate(time);
    document->fontFamily = content.fontFamily;
    document->fontStyle = content.fontStyle;
    const float fontSize = content.fontSize;
    document->fontSize = fontSize;
    document->justification = MapAlign(content.align);

    // boxTextMode maps directly to PAG/AE paragraph (box) text.
    // firstBaseLine must be non-zero for box text or PAG vertically centers the block.
    // Valid textPath forces point text regardless of stored boxTextMode.
    if (content.boxTextMode && !forcePointText) {
        document->boxText = true;
        document->boxTextPos = pag::Point::Zero();
        document->boxTextSize = pag::Point::Make(content.size.x, content.size.y);
        document->firstBaseLine =
            document->boxTextPos.y +
            ResolveTextAscent(options_, content.fontFamily, content.fontStyle, fontSize);
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
                                                                        const TextContent &content,
                                                                        bool forcePointText) {
    if (!content.text.isAnimated()) {
        return new pag::Property<pag::TextDocumentHandle>(
            makeTextDocument(layer, content, 0, forcePointText));
    }
    std::set<FrameTime> times;
    CollectTextKeyframeTimes(content.text, &times);
    if (times.size() < 2) {
        const FrameTime time = times.empty() ? 0 : *times.begin();
        return new pag::Property<pag::TextDocumentHandle>(
            makeTextDocument(layer, content, time, forcePointText));
    }
    std::vector<FrameTime> ordered(times.begin(), times.end());
    std::vector<pag::Keyframe<pag::TextDocumentHandle> *> keyframes;
    for (size_t index = 0; index + 1 < ordered.size(); ++index) {
        // DiscreteProperty / Hold: base Keyframe returns startValue (no Interpolate).
        auto *keyframe = new pag::Keyframe<pag::TextDocumentHandle>();
        keyframe->startTime = ordered[index];
        keyframe->endTime = ordered[index + 1];
        keyframe->startValue = makeTextDocument(layer, content, ordered[index], forcePointText);
        keyframe->endValue = makeTextDocument(layer, content, ordered[index + 1], forcePointText);
        keyframe->interpolationType = pag::KeyframeInterpolationType::Hold;
        keyframes.push_back(keyframe);
    }
    return new pag::AnimatableProperty<pag::TextDocumentHandle>(keyframes);
}

Expected<pag::ImageBytes *, PagExportError> PagFileBuilder::imageBytesForAsset(
    EntityId assetId, const Layer &layer) {
    const std::string name = layer.name.empty() ? "(unnamed layer)" : layer.name;
    if (!assetId.isValid()) {
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, layer.id, name, "ImageAssetMissing",
            "Layer \"" + name +
                "\": image layer has no asset; assign an asset or add \"_bmp\" to rasterize."));
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
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, layer.id, name, "ImageAssetMissing",
            "Layer \"" + name +
                "\": image asset is missing or invalid; fix the asset or add \"_bmp\" to rasterize."));
    }
    const std::string fullPath = JoinPath(document_.projectRoot, asset->path);
    int encodedWidth = 0;
    int encodedHeight = 0;
    std::unique_ptr<pag::ByteData> bytes = LoadImageAsWebP(fullPath, &encodedWidth, &encodedHeight);
    if (bytes == nullptr || bytes->length() == 0 || encodedWidth <= 0 || encodedHeight <= 0) {
        return Unexpected(MakePagExportError(
            PagExportErrorKind::MappingFailed, layer.id, name, "ImageAssetMissing",
            "Layer \"" + name +
                "\": failed to read or encode image as WebP; fix the asset or add \"_bmp\"."));
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
        imageBytesForAsset(content.assetId, layer);
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
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
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

Expected<std::vector<pag::Layer *>, PagExportError> PagFileBuilder::buildShapeLayers(
    const Layer &layer) {
    const auto &content = static_cast<const ShapeContent &>(*layer.content);
    if (content.geometry == nullptr) {
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
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

    Expected<void, PagExportError> styles = appendMainStyles(&group->elements, layer);
    if (!styles.hasValue()) {
        delete group;
        delete pagLayer;
        return Unexpected(styles.error());
    }

    pagLayer->contents.push_back(group);

    std::vector<pag::Layer *> layers;
    layers.push_back(pagLayer);

    for (const auto &stylePtr : layer.styles) {
        if (stylePtr->type() != LayerStyleType::Stroke) {
            continue;
        }
        const auto &stroke = *static_cast<const StrokeStyle *>(stylePtr.get());
        Expected<pag::ShapeLayer *, PagExportError> sibling =
            Expected<pag::ShapeLayer *, PagExportError>(nullptr);
        if (stroke.position == StrokePosition::Center) {
            if (TrimIsDefault(stroke)) {
                continue;  // stays on the main layer
            }
            sibling = buildCenterTrimStrokeLayer(layer, *content.geometry, stroke);
        } else {
            sibling = buildPositionedStrokeLayer(layer, *content.geometry, stroke);
        }
        if (!sibling.hasValue()) {
            for (pag::Layer *owned : layers) {
                delete owned;
            }
            return Unexpected(sibling.error());
        }
        if (sibling.value() == nullptr) {
            continue;
        }
        layers.push_back(sibling.value());
    }
    return layers;
}

Expected<pag::ShapeLayer *, PagExportError> PagFileBuilder::buildPositionedStrokeLayer(
    const Layer &layer, const ShapeElement &geometry, const StrokeStyle &stroke) {
    std::set<FrameTime> timeSet;
    CollectStrokeOutlineBakeTimes(geometry, stroke, layer.inPoint, &timeSet);
    // Animated trim needs per-frame outlines; KF-only Hold cannot spin trimOffset smoothly.
    if (stroke.trimStart.isAnimated() || stroke.trimEnd.isAnimated() ||
        stroke.trimOffset.isAnimated()) {
        for (FrameTime time = layer.inPoint; time < layer.outPoint; ++time) {
            timeSet.insert(time);
        }
    }
    std::vector<FrameTime> times(timeSet.begin(), timeSet.end());
    pag::Property<pag::PathHandle> *pathProperty =
        MakeStrokeOutlinePathProperty(geometry, stroke, times);
    if (pathProperty == nullptr) {
        Warn(&warnings_, layer.id, "StrokePositionBakeFailed",
             "Failed to bake positioned stroke outline; stroke omitted");
        return nullptr;
    }

    auto *pagLayer = new pag::ShapeLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pathProperty;
        delete pagLayer;
        return Unexpected(filled.error());
    }
    const char *positionLabel =
        stroke.position == StrokePosition::Inside ? "Inside" : "Outside";
    pagLayer->name = layer.name + " / Stroke " + positionLabel;

    auto *group = new pag::ShapeGroupElement();
    group->transform = new pag::ShapeTransform();
    group->transform->anchorPoint = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->position = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->scale = new pag::Property<pag::Point>(pag::Point::Make(1, 1));
    group->transform->skew = new pag::Property<float>(0);
    group->transform->skewAxis = new pag::Property<float>(0);
    group->transform->rotation = new pag::Property<float>(0);
    group->transform->opacity = new pag::Property<pag::Opacity>(pag::Opaque);

    auto *pathElement = new pag::ShapePathElement();
    pathElement->shapePath = pathProperty;
    group->elements.push_back(pathElement);

    auto *fill = new pag::FillElement();
    bool blendOk = true;
    fill->blendMode = mapBlendMode(stroke.blendMode, layer.id, &blendOk);
    if (!blendOk) {
        delete group;
        delete pagLayer;
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
    }
    fill->composite = pag::CompositeOrder::AbovePreviousInSameGroup;
    fill->fillRule = pag::FillRule::NonZeroWinding;
    fill->color = ConvertColor(stroke.color, &warnings_, layer.id);
    fill->opacity = ConvertColorAlpha(stroke.color, &warnings_, layer.id);
    group->elements.push_back(fill);

    pagLayer->contents.push_back(group);
    Warn(&warnings_, layer.id, "StrokePositionBaked",
         "Inside/Outside stroke exported as parallel outline ShapeLayer");
    return pagLayer;
}

Expected<pag::ShapeLayer *, PagExportError> PagFileBuilder::buildCenterTrimStrokeLayer(
    const Layer &layer, const ShapeElement &geometry, const StrokeStyle &stroke) {
    auto *pagLayer = new pag::ShapeLayer();
    Expected<void, PagExportError> filled = fillCommonLayer(pagLayer, layer);
    if (!filled.hasValue()) {
        delete pagLayer;
        return Unexpected(filled.error());
    }
    pagLayer->name = layer.name + " / Stroke";

    auto *group = new pag::ShapeGroupElement();
    group->transform = new pag::ShapeTransform();
    group->transform->anchorPoint = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->position = new pag::Property<pag::Point>(pag::Point::Zero());
    group->transform->scale = new pag::Property<pag::Point>(pag::Point::Make(1, 1));
    group->transform->skew = new pag::Property<float>(0);
    group->transform->skewAxis = new pag::Property<float>(0);
    group->transform->rotation = new pag::Property<float>(0);
    group->transform->opacity = new pag::Property<pag::Opacity>(pag::Opaque);

    Expected<pag::ShapeElement *, PagExportError> pathElement = buildGeometry(geometry, layer.id);
    if (!pathElement.hasValue()) {
        delete group;
        delete pagLayer;
        return Unexpected(pathElement.error());
    }
    group->elements.push_back(pathElement.value());

    auto *trim = new pag::TrimPathsElement();
    trim->start = ConvertPercent(stroke.trimStart, &warnings_, layer.id);
    trim->end = ConvertPercent(stroke.trimEnd, &warnings_, layer.id);
    trim->offset = ConvertFloat(stroke.trimOffset, &warnings_, layer.id);
    group->elements.push_back(trim);

    auto *pagStroke = new pag::StrokeElement();
    bool blendOk = true;
    pagStroke->blendMode = mapBlendMode(stroke.blendMode, layer.id, &blendOk);
    if (!blendOk) {
        delete group;
        delete pagLayer;
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
    }
    pagStroke->composite = pag::CompositeOrder::AbovePreviousInSameGroup;
    pagStroke->color = ConvertColor(stroke.color, &warnings_, layer.id);
    pagStroke->opacity = ConvertColorAlpha(stroke.color, &warnings_, layer.id);
    pagStroke->strokeWidth = ConvertFloat(stroke.width, &warnings_, layer.id);
    pagStroke->lineCap = MapLineCap(stroke.cap);
    pagStroke->lineJoin = MapLineJoin(stroke.join);
    pagStroke->miterLimit = new pag::Property<float>(stroke.miterLimit);
    group->elements.push_back(pagStroke);

    pagLayer->contents.push_back(group);
    Warn(&warnings_, layer.id, "StrokeTrimSeparated",
         "Trimmed Center stroke exported on a parallel ShapeLayer so Fill stays untrimmed");
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
            return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
    }
    return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
}

Expected<void, PagExportError> PagFileBuilder::appendMainStyles(
    std::vector<pag::ShapeElement *> *contents, const Layer &layer) {
    auto appendFill = [&](const FillStyle &fill) -> Expected<void, PagExportError> {
        auto *pagFill = new pag::FillElement();
        bool blendOk = true;
        pagFill->blendMode = mapBlendMode(fill.blendMode, layer.id, &blendOk);
        if (!blendOk) {
            delete pagFill;
            return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
        }
        pagFill->composite = pag::CompositeOrder::AbovePreviousInSameGroup;
        pagFill->fillRule = MapFillRule(fill.fillRule);
        pagFill->color = ConvertColor(fill.color, &warnings_, layer.id);
        pagFill->opacity = ConvertColorAlpha(fill.color, &warnings_, layer.id);
        contents->push_back(pagFill);
        return Expected<void, PagExportError>();
    };

    auto appendCenterStroke = [&](const StrokeStyle &stroke) -> Expected<void, PagExportError> {
        auto *pagStroke = new pag::StrokeElement();
        bool blendOk = true;
        pagStroke->blendMode = mapBlendMode(stroke.blendMode, layer.id, &blendOk);
        if (!blendOk) {
            delete pagStroke;
            return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
        }
        pagStroke->composite = pag::CompositeOrder::AbovePreviousInSameGroup;
        pagStroke->color = ConvertColor(stroke.color, &warnings_, layer.id);
        pagStroke->opacity = ConvertColorAlpha(stroke.color, &warnings_, layer.id);
        pagStroke->strokeWidth = ConvertFloat(stroke.width, &warnings_, layer.id);
        pagStroke->lineCap = MapLineCap(stroke.cap);
        pagStroke->lineJoin = MapLineJoin(stroke.join);
        pagStroke->miterLimit = new pag::Property<float>(stroke.miterLimit);
        contents->push_back(pagStroke);
        return Expected<void, PagExportError>();
    };

    for (const auto &stylePtr : layer.styles) {
        if (stylePtr->type() == LayerStyleType::Fill) {
            Expected<void, PagExportError> appended =
                appendFill(*static_cast<const FillStyle *>(stylePtr.get()));
            if (!appended.hasValue()) {
                return Unexpected(appended.error());
            }
            continue;
        }
        if (stylePtr->type() != LayerStyleType::Stroke) {
            return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
        }
        const auto &stroke = *static_cast<const StrokeStyle *>(stylePtr.get());
        // Inside/Outside and any trimmed stroke are parallel layers (TrimPaths would clip Fill).
        if (stroke.position != StrokePosition::Center || !TrimIsDefault(stroke)) {
            continue;
        }
        Expected<void, PagExportError> appended = appendCenterStroke(stroke);
        if (!appended.hasValue()) {
            return Unexpected(appended.error());
        }
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
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
    }

    const FrameTime duration = layer.outPoint - layer.inPoint;
    if (duration <= 0) {
        return Unexpected(MakePagExportError(PagExportErrorKind::MappingFailed, {}, "", "", "PAG export mapping failed"));
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
