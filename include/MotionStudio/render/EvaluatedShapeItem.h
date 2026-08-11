#pragma once

#include "MotionStudio/render/Paint.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "MotionStudio/render/StrokeOptions.h"

namespace motion {

// One flattened draw primitive produced by scene evaluation.
struct EvaluatedShapeItem {
    // Layer-local geometry; world placement via EvaluatedLayer::worldTransform.
    ShapeGeometry geometry;
    Paint paint;
    bool isStroke = false;
    StrokeOptions stroke;
    // Index into Layer::styles for this paint; -1 when not from a layer style.
    int styleIndex = -1;
};

}  // namespace motion
