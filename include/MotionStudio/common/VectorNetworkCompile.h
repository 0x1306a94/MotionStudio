#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"

namespace motion {

// Extracts bounded faces after subdividing cubics and splitting at crossings so
// fill regions match visually enclosed pockets. Open chains / outer faces omit.
BezierPath CompileFillFaces(const VectorNetwork &network);

// One open contour per edge (including internal edges). Tangents map from the
// edge endpoints onto the contour vertices' out/in handles.
BezierPath CompileStrokeEdges(const VectorNetwork &network);

}  // namespace motion
