#pragma once

#include <memory>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/StrokePosition.h"

namespace motion {

enum class LayerFxType {
    DropShadow,
    OuterGlow,
    Stroke
};

enum class LayerFxDrawPosition {
    Behind,
    Above
};

class LayerFx {
  public:
    // Constructs a layer style tagged with the given type.
    // type: concrete style variant.
    explicit LayerFx(LayerFxType type);
    virtual ~LayerFx() = default;

    // Returns the concrete style variant tag.
    LayerFxType type() const;

    // Returns whether this style draws behind or above the layer image.
    virtual LayerFxDrawPosition drawPosition() const;

    EntityId id = EntityId::Generate();
    bool enabled = true;

    // Bakes animatable fields to static values at time.
    // Returns nullptr when disabled or when parameters are identity.
    // time: preview evaluation time in frames.
    virtual std::shared_ptr<const LayerFx> snapshot(PreviewTime time) const = 0;

  private:
    LayerFxType type_ = {};
};

class DropShadowStyle : public LayerFx {
  public:
    DropShadowStyle();
    ~DropShadowStyle() override = default;

    BlendMode blendMode = BlendMode::Multiply;
    Animatable<Color> color{Color{0.0f, 0.0f, 0.0f, 1.0f}};
    Animatable<float> opacity{0.75f};
    Animatable<float> angle{135.0f};
    Animatable<float> distance{5.0f};
    Animatable<float> size{5.0f};
    Animatable<float> spread{0.0f};

    // Bakes shadow fields. Identity (zero distance/size/spread) or disabled returns nullptr.
    // time: preview evaluation time in frames.
    std::shared_ptr<const LayerFx> snapshot(PreviewTime time) const override;
};

class OuterGlowStyle : public LayerFx {
  public:
    OuterGlowStyle();
    ~OuterGlowStyle() override = default;

    BlendMode blendMode = BlendMode::Screen;
    Animatable<Color> color{Color{1.0f, 1.0f, 0.745f, 1.0f}};
    Animatable<float> opacity{0.75f};
    Animatable<float> size{5.0f};
    Animatable<float> spread{0.0f};
    Animatable<float> range{1.0f};

    // Bakes glow fields. Identity (size <= 0) or disabled returns nullptr.
    // time: preview evaluation time in frames.
    std::shared_ptr<const LayerFx> snapshot(PreviewTime time) const override;
};

class LayerStrokeStyle : public LayerFx {
  public:
    LayerStrokeStyle();
    ~LayerStrokeStyle() override = default;

    // Stroke draws above the layer image.
    LayerFxDrawPosition drawPosition() const override;

    BlendMode blendMode = BlendMode::Normal;
    Animatable<Color> color{Color{1.0f, 0.0f, 0.0f, 1.0f}};
    Animatable<float> opacity{1.0f};
    Animatable<float> size{3.0f};
    StrokePosition position = StrokePosition::Outside;

    // Bakes stroke fields. Identity (size <= 0) or disabled returns nullptr.
    // time: preview evaluation time in frames.
    std::shared_ptr<const LayerFx> snapshot(PreviewTime time) const override;
};

}  // namespace motion
