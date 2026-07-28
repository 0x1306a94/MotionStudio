#pragma once

#include <cstddef>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Pure local-space BezierPath edits. Invalid indices / empty paths return the
// input unchanged. No Document / undo / world transform.

// Moves vertex index to newPoint. When linkedHandles is true, relative tangents
// are preserved. When false, absolute control-point positions are preserved
// (relative tangents are rewritten).
BezierPath MoveVertex(BezierPath path, size_t index, Vec2 newPoint, bool linkedHandles);

// Sets the incoming tangent at index. When mirrorOut is true, outTangent becomes
// the negation of newIn (smooth linked handles).
BezierPath MoveInTangent(BezierPath path, size_t index, Vec2 newIn, bool mirrorOut);

// Sets the outgoing tangent at index. When mirrorIn is true, inTangent becomes
// the negation of newOut (smooth linked handles).
BezierPath MoveOutTangent(BezierPath path, size_t index, Vec2 newOut, bool mirrorIn);

// Inserts a vertex on segmentIndex at parameter t in [0, 1] via de Casteljau
// split, updating adjacent tangents so the curve shape is preserved.
BezierPath InsertVertexOnSegment(BezierPath path, size_t segmentIndex, float t);

// Removes vertex index. No-op when the path would drop below two vertices.
BezierPath RemoveVertex(BezierPath path, size_t index);

// Marks the path closed. No-op when fewer than two vertices.
BezierPath ClosePath(BezierPath path);

// Appends a vertex to an open path. No-op when the path is already closed.
BezierPath AppendVertex(BezierPath path, BezierPath::Vertex vertex);

}  // namespace motion
