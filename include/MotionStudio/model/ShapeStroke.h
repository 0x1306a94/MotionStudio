#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

// Strokes the outline of preceding paths with an animatable color and width.
class ShapeStroke : public ShapeElement {
  public:
    ShapeStroke();
    ~ShapeStroke() override;

    Animatable<Color> color{Color{0, 0, 0, 1}};
    Animatable<float> width{2.0f};
    Animatable<float> opacity{1.0f};
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.0f;
};

}  // namespace motion
