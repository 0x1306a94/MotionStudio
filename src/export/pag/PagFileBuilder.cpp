#include "PagFileBuilder.h"

#include <cmath>
#include <set>
#include <vector>

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
#include "PagAnimatableConvert.h"
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

void CollectKeyframeTimes(const Animatable<std::string> &text, const Animatable<float> &fontSize,
                          const Animatable<Vec2> &size, std::set<FrameTime> *times) {
    for (const auto &keyframe : text.keyframes()) {
        times->insert(keyframe.time);
    }
    for (const auto &keyframe : fontSize.keyframes()) {
        times->insert(keyframe.time);
    }
    for (const auto &keyframe : size.keyframes()) {
        times->insert(keyframe.time);
    }
}

std::string JoinPath(const std::string &root, const std::string &relative) {
    if (root.empty()) {
        return relative;
    }
    if (!root.empty() && root.back() == '/') {
        return root + relative;
    }
    return root + "/" + relative;
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

PagFileBuilder::PagFileBuilder(const Document &document, const Composition &composition)
    : document_(document)
    , rootComposition_(composition) {
}

Expected<PagBuildResult, PagExportError> PagFileBuilder::build() {
    Expected<void, PagExportError> collected = collectCompositionOrder(rootComposition_.id);
    if (!collected.hasValue()) {
        return Unexpected(collected.error());
    }

    std::vector<pag::Composition *> compositions;
    compositions.reserve(compositionOrder_.size());
    for (EntityId compositionId : compositionOrder_) {
        const Composition *source = findComposition(compositionId);
        if (source == nullptr) {
            for (pag::Composition *owned : compositions) {
                delete owned;
            }
            for (pag::ImageBytes *image : imageBytesList_) {
                delete image;
            }
            return Unexpected(PagExportError::InvalidComposition);
        }
        Expected<pag::VectorComposition *, PagExportError> built = buildComposition(*source);
        if (!built.hasValue()) {
            for (pag::Composition *owned : compositions) {
                delete owned;
            }
            for (pag::ImageBytes *image : imageBytesList_) {
                delete image;
            }
            return Unexpected(built.error());
        }
        compositions.push_back(built.value());
    }

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

Expected<pag::VectorComposition *, PagExportError> PagFileBuilder::buildComposition(
    const Composition &composition) {
    if (composition.width <= 0 || composition.height <= 0 || composition.duration <= 0) {
        return Unexpected(PagExportError::InvalidOptions);
    }
    if (composition.frameRate.den == 0) {
        return Unexpected(PagExportError::InvalidOptions);
    }

    layerByEntity_.clear();

    auto *pagComposition = new pag::VectorComposition();
    pagComposition->id = nextCompositionId_++;
    pagComposition->width = composition.width;
    pagComposition->height = composition.height;
    pagComposition->duration = composition.duration;
    pagComposition->frameRate = static_cast<float>(composition.frameRate.num) /
        static_cast<float>(composition.frameRate.den);
    pagComposition->backgroundColor = ToPagColor(composition.backgroundColor);
    compositionByEntity_[composition.id.value] = pagComposition;

    for (const auto &layerPtr : composition.layers) {
        Expected<pag::Layer *, PagExportError> layerResult = buildLayer(*layerPtr);
        if (!layerResult.hasValue()) {
            delete pagComposition;
            return Unexpected(layerResult.error());
        }
        pag::Layer *pagLayer = layerResult.value();
        pagLayer->containingComposition = pagComposition;
        pagComposition->layers.push_back(pagLayer);
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
                delete pagComposition;
                return Unexpected(PagExportError::MappingFailed);
            }
            childIt->second->parent = parentIt->second;
        }
        if (layerPtr->trackMatteType != TrackMatteType::None) {
            if (!layerPtr->trackMatteLayerId.isValid()) {
                delete pagComposition;
                return Unexpected(PagExportError::MappingFailed);
            }
            auto matteIt = layerByEntity_.find(layerPtr->trackMatteLayerId.value);
            if (matteIt == layerByEntity_.end()) {
                delete pagComposition;
                return Unexpected(PagExportError::MappingFailed);
            }
            childIt->second->trackMatteType = MapTrackMatte(layerPtr->trackMatteType);
            childIt->second->trackMatteLayer = matteIt->second;
        }
    }

    return pagComposition;
}

Expected<void, PagExportError> PagFileBuilder::rejectUnsupported(const Layer &layer) {
    if (layer.followPath.enabled) {
        return Unexpected(PagExportError::MappingFailed);
    }
    switch (layer.type()) {
        case LayerType::Shape:
        case LayerType::Group:
        case LayerType::Text:
        case LayerType::Image:
        case LayerType::Precomp:
            return Expected<void, PagExportError>();
    }
    return Unexpected(PagExportError::MappingFailed);
}

Expected<pag::Layer *, PagExportError> PagFileBuilder::buildLayer(const Layer &layer) {
    Expected<void, PagExportError> unsupported = rejectUnsupported(layer);
    if (!unsupported.hasValue()) {
        return Unexpected(unsupported.error());
    }
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
    if (content.boxTextMode) {
        Warn(&warnings_, layer.id, "TextFeatureApproximated",
             "boxTextMode shrink-to-fit is not fully represented in PAG TextDocument");
    }
    pagLayer->sourceText = buildSourceText(content, layer.id);
    return pagLayer;
}

pag::TextDocumentHandle PagFileBuilder::makeTextDocument(const TextContent &content,
                                                         FrameTime time) const {
    auto document = std::make_shared<pag::TextDocument>();
    document->text = content.text.evaluate(time);
    document->fontFamily = content.fontFamily;
    document->fontStyle = content.fontStyle;
    document->fontSize = content.fontSize.evaluate(time);
    document->justification = MapAlign(content.align);
    document->applyFill = true;
    document->boxText = true;
    const Vec2 box = content.size.evaluate(time);
    document->boxTextPos = pag::Point::Zero();
    document->boxTextSize = pag::Point::Make(box.x, box.y);
    return document;
}

pag::Property<pag::TextDocumentHandle> *PagFileBuilder::buildSourceText(const TextContent &content,
                                                                        EntityId layerId) {
    (void)layerId;
    if (!content.text.isAnimated() && !content.fontSize.isAnimated() && !content.size.isAnimated()) {
        return new pag::Property<pag::TextDocumentHandle>(makeTextDocument(content, 0));
    }
    std::set<FrameTime> times;
    CollectKeyframeTimes(content.text, content.fontSize, content.size, &times);
    if (times.size() < 2) {
        const FrameTime time = times.empty() ? 0 : *times.begin();
        return new pag::Property<pag::TextDocumentHandle>(makeTextDocument(content, time));
    }
    std::vector<FrameTime> ordered(times.begin(), times.end());
    std::vector<pag::Keyframe<pag::TextDocumentHandle> *> keyframes;
    for (size_t index = 0; index + 1 < ordered.size(); ++index) {
        // DiscreteProperty / Hold: base Keyframe returns startValue (no Interpolate).
        auto *keyframe = new pag::Keyframe<pag::TextDocumentHandle>();
        keyframe->startTime = ordered[index];
        keyframe->endTime = ordered[index + 1];
        keyframe->startValue = makeTextDocument(content, ordered[index]);
        keyframe->endValue = makeTextDocument(content, ordered[index + 1]);
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
    pagLayer->imageFillRule = new pag::ImageFillRule();
    pagLayer->imageFillRule->scaleMode = MapScaleMode(content.scaleMode);
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
