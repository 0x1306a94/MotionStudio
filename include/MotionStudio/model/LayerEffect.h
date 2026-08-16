#pragma once

#include <memory>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"

namespace motion {

enum class LayerEffectType {
    BrightnessContrast,
    GaussianBlur
};

class LayerEffect {
  public:
    // Constructs an effect tagged with the given type.
    // type: concrete effect variant.
    explicit LayerEffect(LayerEffectType type);
    virtual ~LayerEffect() = default;

    // Returns the concrete effect variant tag.
    LayerEffectType type() const;

    EntityId id = EntityId::Generate();
    bool enabled = true;

    // Bakes animatable fields to static values at time.
    // Returns nullptr when disabled or when parameters are identity.
    // time: preview evaluation time in frames.
    virtual std::shared_ptr<const LayerEffect> snapshot(PreviewTime time) const = 0;

  private:
    LayerEffectType type_;
};

class BrightnessContrastEffect : public LayerEffect {
  public:
    BrightnessContrastEffect();
    ~BrightnessContrastEffect() override = default;

    Animatable<float> brightness{0};
    Animatable<float> contrast{0};

    // Bakes brightness and contrast. Identity (0, 0) or disabled returns nullptr.
    // time: preview evaluation time in frames.
    std::shared_ptr<const LayerEffect> snapshot(PreviewTime time) const override;
};

class GaussianBlurEffect : public LayerEffect {
  public:
    GaussianBlurEffect();
    ~GaussianBlurEffect() override = default;

    Animatable<float> blurriness{0};
    bool repeatEdgePixels = false;

    // Bakes blurriness. Identity (blurriness <= 0) or disabled returns nullptr.
    // time: preview evaluation time in frames.
    std::shared_ptr<const LayerEffect> snapshot(PreviewTime time) const override;
};

}  // namespace motion
