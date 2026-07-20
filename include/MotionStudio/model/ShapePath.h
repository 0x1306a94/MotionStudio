#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

class ShapePath : public ShapeElement {
public:
    ShapePath();
    ~ShapePath() override;

    Animatable<BezierPath> path;  // 整条路径作为可动画值
};

}  // namespace motion
