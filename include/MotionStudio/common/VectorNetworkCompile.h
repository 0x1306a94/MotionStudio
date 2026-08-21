#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"

namespace motion {

// Extracts bounded faces after subdividing cubics and splitting at crossings so
// fill regions match visually enclosed pockets. Open chains / outer faces omit.
BezierPath CompileFillFaces(const VectorNetwork &network);

// Chains edges through degree-2 vertices into stroke contours so line joins
// apply at corners. Degree != 2 endpoints break chains (branch/junction edges
// stay separate); pure cycles become closed contours. Tangents map from edge
// endpoints onto contour vertices' out/in handles.
BezierPath CompileStrokeEdges(const VectorNetwork &network);

struct CompiledVectorNetwork {
    BezierPath fill;
    BezierPath stroke;
};

// fill + stroke from one lookup. The reference is valid only until the next
// CompileVectorNetwork call on this thread; copy fill/stroke out immediately.
const CompiledVectorNetwork &CompileVectorNetwork(const VectorNetwork &network);

}  // namespace motion
