#pragma once

#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/GradientType.h"

namespace motion {

struct GradientStop {
    Animatable<Color> color{Color{0, 0, 0, 1}};
    Animatable<float> position{0.f};
};

// Gradient paint data embedded on FillStyle / StrokeStyle.
// start/end are stored in layer-local AABB top-left space: (0,0) = local
// bounds min. SceneEvaluator adds aabb.min before draw (shape-path space).
// Radial/Diamond radius is Distance(start, end) after that conversion.
// Conic uses start as center plus startAngle/endAngle.
struct GradientPaint {
    GradientType type = GradientType::Linear;
    Animatable<Vec2> start{Vec2{0, 0}};
    Animatable<Vec2> end{Vec2{100, 0}};
    Animatable<float> startAngle{0.f};
    Animatable<float> endAngle{360.f};
    std::vector<GradientStop> stops;
};

}  // namespace motion
