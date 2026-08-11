#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/StylePaintMode.h"

namespace motion {

enum class LayerStyleType {
    Fill,
    Stroke
};

class LayerStyle {
  public:
    explicit LayerStyle(LayerStyleType type)
        : type_(type) {
    }
    virtual ~LayerStyle() = default;

    LayerStyleType type() const {
        return type_;
    }

    EntityId id = EntityId::Generate();

  private:
    LayerStyleType type_;
};

class FillStyle : public LayerStyle {
  public:
    FillStyle()
        : LayerStyle(LayerStyleType::Fill) {
    }
    ~FillStyle() override = default;

    StylePaintMode paintMode = StylePaintMode::Color;
    Animatable<Color> color{Color{0, 0, 0, 1}};
    GradientPaint gradient;
    EntityId shaderId{};
    ShaderUniformValues uniformValues;
    FillRule fillRule = FillRule::NonZero;
    BlendMode blendMode = BlendMode::Normal;
};

class StrokeStyle : public LayerStyle {
  public:
    StrokeStyle()
        : LayerStyle(LayerStyleType::Stroke) {
    }
    ~StrokeStyle() override = default;

    StylePaintMode paintMode = StylePaintMode::Color;
    Animatable<Color> color{Color{0, 0, 0, 1}};
    GradientPaint gradient;
    EntityId shaderId{};
    ShaderUniformValues uniformValues;
    Animatable<float> width{2.0f};
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.0f;
    BlendMode blendMode = BlendMode::Normal;
    StrokePosition position = StrokePosition::Center;
    Animatable<float> trimStart{0.0f};   // 0.0 ~ 1.0 fraction of path length
    Animatable<float> trimEnd{1.0f};     // 0.0 ~ 1.0 fraction of path length
    Animatable<float> trimOffset{0.0f};  // degrees, rotates the trim window
};

}  // namespace motion
