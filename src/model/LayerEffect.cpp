#include "MotionStudio/model/LayerEffect.h"

namespace motion {

LayerEffect::LayerEffect(LayerEffectType type)
    : type_(type) {
}

LayerEffectType LayerEffect::type() const {
    return type_;
}

BrightnessContrastEffect::BrightnessContrastEffect()
    : LayerEffect(LayerEffectType::BrightnessContrast) {
}

std::shared_ptr<const LayerEffect> BrightnessContrastEffect::snapshot(PreviewTime time) const {
    if (!enabled) {
        return nullptr;
    }
    const float bakedBrightness = brightness.evaluatePreview(time);
    const float bakedContrast = contrast.evaluatePreview(time);
    if (bakedBrightness == 0.0f && bakedContrast == 0.0f) {
        return nullptr;
    }
    auto baked = std::make_shared<BrightnessContrastEffect>();
    baked->id = id;
    baked->enabled = true;
    baked->brightness.setStaticValue(bakedBrightness);
    baked->contrast.setStaticValue(bakedContrast);
    return baked;
}

GaussianBlurEffect::GaussianBlurEffect()
    : LayerEffect(LayerEffectType::GaussianBlur) {
}

std::shared_ptr<const LayerEffect> GaussianBlurEffect::snapshot(PreviewTime time) const {
    if (!enabled) {
        return nullptr;
    }
    const float bakedBlurriness = blurriness.evaluatePreview(time);
    if (bakedBlurriness <= 0.0f) {
        return nullptr;
    }
    auto baked = std::make_shared<GaussianBlurEffect>();
    baked->id = id;
    baked->enabled = true;
    baked->blurriness.setStaticValue(bakedBlurriness);
    baked->repeatEdgePixels = repeatEdgePixels;
    return baked;
}

}  // namespace motion
