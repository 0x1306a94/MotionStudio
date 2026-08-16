#include "MotionStudio/model/LayerFx.h"

#include <algorithm>

namespace motion {
namespace {

float clampSpread(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float clampRange(float value) {
    return std::clamp(value, 0.01f, 1.0f);
}

float nonNegative(float value) {
    return std::max(value, 0.0f);
}

}  // namespace

LayerFx::LayerFx(LayerFxType type)
    : type_(type) {
}

LayerFxType LayerFx::type() const {
    return type_;
}

LayerFxDrawPosition LayerFx::drawPosition() const {
    return LayerFxDrawPosition::Behind;
}

DropShadowStyle::DropShadowStyle()
    : LayerFx(LayerFxType::DropShadow) {
}

std::shared_ptr<const LayerFx> DropShadowStyle::snapshot(PreviewTime time) const {
    if (!enabled) {
        return nullptr;
    }
    const float bakedOpacity = opacity.evaluatePreview(time);
    if (bakedOpacity <= 0.0f) {
        return nullptr;
    }
    const float bakedDistance = nonNegative(distance.evaluatePreview(time));
    const float bakedSize = nonNegative(size.evaluatePreview(time));
    const float bakedSpread = clampSpread(spread.evaluatePreview(time));
    if (bakedDistance <= 0.0f && bakedSize <= 0.0f && bakedSpread <= 0.0f) {
        return nullptr;
    }

    auto baked = std::make_shared<DropShadowStyle>();
    baked->id = id;
    baked->enabled = true;
    baked->blendMode = blendMode;
    baked->color.setStaticValue(color.evaluatePreview(time));
    baked->opacity.setStaticValue(bakedOpacity);
    baked->angle.setStaticValue(angle.evaluatePreview(time));
    baked->distance.setStaticValue(bakedDistance);
    baked->size.setStaticValue(bakedSize);
    baked->spread.setStaticValue(bakedSpread);
    return baked;
}

OuterGlowStyle::OuterGlowStyle()
    : LayerFx(LayerFxType::OuterGlow) {
}

std::shared_ptr<const LayerFx> OuterGlowStyle::snapshot(PreviewTime time) const {
    if (!enabled) {
        return nullptr;
    }
    const float bakedOpacity = opacity.evaluatePreview(time);
    if (bakedOpacity <= 0.0f) {
        return nullptr;
    }
    const float bakedSize = nonNegative(size.evaluatePreview(time));
    if (bakedSize <= 0.0f) {
        return nullptr;
    }

    auto baked = std::make_shared<OuterGlowStyle>();
    baked->id = id;
    baked->enabled = true;
    baked->blendMode = blendMode;
    baked->color.setStaticValue(color.evaluatePreview(time));
    baked->opacity.setStaticValue(bakedOpacity);
    baked->size.setStaticValue(bakedSize);
    baked->spread.setStaticValue(clampSpread(spread.evaluatePreview(time)));
    baked->range.setStaticValue(clampRange(range.evaluatePreview(time)));
    return baked;
}

LayerStrokeStyle::LayerStrokeStyle()
    : LayerFx(LayerFxType::Stroke) {
}

LayerFxDrawPosition LayerStrokeStyle::drawPosition() const {
    return LayerFxDrawPosition::Above;
}

std::shared_ptr<const LayerFx> LayerStrokeStyle::snapshot(PreviewTime time) const {
    if (!enabled) {
        return nullptr;
    }
    const float bakedOpacity = opacity.evaluatePreview(time);
    if (bakedOpacity <= 0.0f) {
        return nullptr;
    }
    const float bakedSize = nonNegative(size.evaluatePreview(time));
    if (bakedSize <= 0.0f) {
        return nullptr;
    }

    auto baked = std::make_shared<LayerStrokeStyle>();
    baked->id = id;
    baked->enabled = true;
    baked->blendMode = blendMode;
    baked->position = position;
    baked->color.setStaticValue(color.evaluatePreview(time));
    baked->opacity.setStaticValue(bakedOpacity);
    baked->size.setStaticValue(bakedSize);
    return baked;
}

}  // namespace motion
