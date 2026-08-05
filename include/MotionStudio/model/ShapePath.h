#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

// A free-form path shape element (authoring model is VectorNetwork).
class ShapePath : public ShapeElement {
  public:
    ShapePath();
    ~ShapePath() override;

    // The entire path as a single animatable value.
    Animatable<VectorNetwork> path;
};

}  // namespace motion
