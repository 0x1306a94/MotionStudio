#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

class ShapeFill : public ShapeElement {
public:
    ShapeFill();
    ~ShapeFill() override;

    Animatable<Color> color{Color{0, 0, 0, 1}};
    Animatable<float> opacity{1.0f};
    FillRule fillRule = FillRule::NonZero;
};

}  // namespace motion
