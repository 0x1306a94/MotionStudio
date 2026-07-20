#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

// A free-form bezier path shape element.
class ShapePath : public ShapeElement {
public:
    ShapePath();
    ~ShapePath() override;

    // The entire path as a single animatable value.
    Animatable<BezierPath> path;
};

}  // namespace motion
