#include "PagFileBuilder.h"

#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeType.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "PagAnimatableConvert.h"

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

void Warn(std::vector<PagExportWarning> *warnings, EntityId entityId, const char *code,
          const char *message) {
    PagExportWarning warning;
    warning.entityId = entityId;
    warning.code = code;
    warning.message = message;
    warnings->push_back(std::move(warning));
}

}  // namespace

PagFileBuilder::PagFileBuilder(const Document &document, const Composition &composition)
    : document_(document)
    , composition_(composition) {
    (void)document_;
}

Expected<PagBuildResult, PagExportError> PagFileBuilder::build() {
    Expected<pag::VectorComposition *, PagExportError> compositionResult = buildComposition();
    if (!compositionResult.hasValue()) {
        return Unexpected(compositionResult.error());
    }
    pag::VectorComposition *composition = compositionResult.value();
    std::shared_ptr<pag::File> file = pag::Codec::VerifyAndMake({composition}, {});
    if (file == nullptr) {
        return Unexpected(PagExportError::EncodeFailed);
    }
    PagBuildResult result;
    result.file = std::move(file);
    result.warnings = std::move(warnings_);
    return result;
}

Expected<pag::VectorComposition *, PagExportError> PagFileBuilder::buildComposition() {
    if (composition_.width <= 0 || composition_.height <= 0 || composition_.duration <= 0) {
        return Unexpected(PagExportError::InvalidOptions);
    }
    if (composition_.frameRate.den == 0) {
        return Unexpected(PagExportError::InvalidOptions);
    }

    auto *composition = new pag::VectorComposition();
    composition->id = 1;
    composition->width = composition_.width;
    composition->height = composition_.height;
    composition->duration = composition_.duration;
    composition->frameRate = static_cast<float>(composition_.frameRate.num) /
        static_cast<float>(composition_.frameRate.den);
    composition->backgroundColor = ToPagColor(composition_.backgroundColor);

    for (const auto &layerPtr : composition_.layers) {
        Expected<pag::Layer *, PagExportError> layerResult = buildLayer(*layerPtr);
        if (!layerResult.hasValue()) {
            delete composition;
            return Unexpected(layerResult.error());
        }
        pag::Layer *pagLayer = layerResult.value();
        pagLayer->containingComposition = composition;
        composition->layers.push_back(pagLayer);
        layerByEntity_[layerPtr->id.value] = pagLayer;
    }

    for (const auto &layerPtr : composition_.layers) {
        if (!layerPtr->parentId.isValid()) {
            continue;
        }
        auto parentIt = layerByEntity_.find(layerPtr->parentId.value);
        auto childIt = layerByEntity_.find(layerPtr->id.value);
        if (parentIt == layerByEntity_.end() || childIt == layerByEntity_.end()) {
            delete composition;
            return Unexpected(PagExportError::MappingFailed);
        }
        childIt->second->parent = parentIt->second;
    }

    return composition;
}

Expected<void, PagExportError> PagFileBuilder::rejectUnsupported(const Layer &layer) {
    if (layer.followPath.enabled) {
        return Unexpected(PagExportError::MappingFailed);
    }
    if (!layer.masks.empty()) {
        return Unexpected(PagExportError::MappingFailed);
    }
    if (layer.trackMatteType != TrackMatteType::None) {
        return Unexpected(PagExportError::MappingFailed);
    }
    switch (layer.type()) {
        case LayerType::Shape:
        case LayerType::Group:
            return Expected<void, PagExportError>();
        case LayerType::Text:
        case LayerType::Image:
        case LayerType::Precomp:
            return Unexpected(PagExportError::MappingFailed);
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
        default:
            return Unexpected(PagExportError::MappingFailed);
    }
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
    pagLayer->transform = buildTransform(layer.transform, layer.id);
    return Expected<void, PagExportError>();
}

}  // namespace pag_export
}  // namespace motion
