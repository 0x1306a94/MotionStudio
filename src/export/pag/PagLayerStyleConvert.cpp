#include "PagLayerStyleConvert.h"

#include "PagAnimatableConvert.h"

namespace motion {
namespace pag_export {
namespace {

pag::StrokePosition ToPagLayerStrokePosition(StrokePosition position) {
    switch (position) {
        case StrokePosition::Center:
            return pag::StrokePosition::Center;
        case StrokePosition::Inside:
            return pag::StrokePosition::Inside;
        case StrokePosition::Outside:
            return pag::StrokePosition::Outside;
    }
    return pag::StrokePosition::Outside;
}

}  // namespace

BlendMode LayerFxBlendMode(const LayerFx &style) {
    switch (style.type()) {
        case LayerFxType::DropShadow:
            return static_cast<const DropShadowStyle &>(style).blendMode;
        case LayerFxType::OuterGlow:
            return static_cast<const OuterGlowStyle &>(style).blendMode;
        case LayerFxType::Stroke:
            return static_cast<const LayerStrokeStyle &>(style).blendMode;
    }
    return BlendMode::Normal;
}

pag::LayerStyle *ToPagLayerStyle(const LayerFx &style, pag::BlendMode blendMode,
                                 std::vector<PagExportWarning> *warnings, EntityId entityId) {
    switch (style.type()) {
        case LayerFxType::DropShadow: {
            const auto &source = static_cast<const DropShadowStyle &>(style);
            auto *converted = new pag::DropShadowStyle();
            converted->blendMode = new pag::Property<pag::BlendMode>(blendMode);
            converted->color = ConvertColor(source.color, warnings, entityId);
            converted->opacity = ConvertOpacity(source.opacity, warnings, entityId);
            converted->angle = ConvertFloat(source.angle, warnings, entityId);
            converted->distance = ConvertFloat(source.distance, warnings, entityId);
            converted->size = ConvertFloat(source.size, warnings, entityId);
            converted->spread = ConvertPercent(source.spread, warnings, entityId);
            return converted;
        }
        case LayerFxType::OuterGlow: {
            const auto &source = static_cast<const OuterGlowStyle &>(style);
            auto *converted = new pag::OuterGlowStyle();
            converted->blendMode = new pag::Property<pag::BlendMode>(blendMode);
            converted->opacity = ConvertOpacity(source.opacity, warnings, entityId);
            converted->noise = new pag::Property<pag::Percent>(0.0f);
            converted->colorType =
                new pag::Property<pag::GlowColorType>(pag::GlowColorType::SingleColor);
            converted->color = ConvertColor(source.color, warnings, entityId);
            converted->colors = new pag::Property<pag::GradientColorHandle>(
                pag::GradientColorHandle(new pag::GradientColor()));
            converted->gradientSmoothness = new pag::Property<pag::Percent>(1.0f);
            converted->technique =
                new pag::Property<pag::GlowTechniqueType>(pag::GlowTechniqueType::Softer);
            converted->spread = ConvertPercent(source.spread, warnings, entityId);
            converted->size = ConvertFloat(source.size, warnings, entityId);
            converted->range = ConvertPercent(source.range, warnings, entityId);
            converted->jitter = new pag::Property<pag::Percent>(0.0f);
            return converted;
        }
        case LayerFxType::Stroke: {
            const auto &source = static_cast<const LayerStrokeStyle &>(style);
            auto *converted = new pag::StrokeStyle();
            converted->blendMode = new pag::Property<pag::BlendMode>(blendMode);
            converted->color = ConvertColor(source.color, warnings, entityId);
            converted->size = ConvertFloat(source.size, warnings, entityId);
            converted->opacity = ConvertOpacity(source.opacity, warnings, entityId);
            converted->position = new pag::Property<pag::StrokePosition>(
                ToPagLayerStrokePosition(source.position));
            return converted;
        }
    }
    return nullptr;
}

}  // namespace pag_export
}  // namespace motion
