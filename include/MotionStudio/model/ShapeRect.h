#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

// Parametric rectangle with animatable position, size, and corner radius.
class ShapeRect : public ShapeElement {
  public:
    ShapeRect();
    ~ShapeRect() override;

    Animatable<Vec2> position{Vec2{0, 0}};
    Animatable<Vec2> size{Vec2{0, 0}};
    Animatable<float> cornerRadius{0.0f};
};

}  // namespace motion
