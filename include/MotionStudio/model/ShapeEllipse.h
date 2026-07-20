#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

class ShapeEllipse : public ShapeElement {
public:
    ShapeEllipse();
    ~ShapeEllipse() override;

    Animatable<Vec2> position{Vec2{0, 0}};
    Animatable<Vec2> size{Vec2{0, 0}};
};

}  // namespace motion
