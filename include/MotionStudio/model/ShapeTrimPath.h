#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

class ShapeTrimPath : public ShapeElement {
public:
    ShapeTrimPath();
    ~ShapeTrimPath() override;

    Animatable<float> start{0.0f};   // 0.0 ~ 1.0
    Animatable<float> end{1.0f};     // 0.0 ~ 1.0
    Animatable<float> offset{0.0f};  // 度
};

}  // namespace motion
