#pragma once

#include <cstdint>

#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/VectorNetwork.h"

namespace motion {

// Adds a vertex at point. Writes the new id to outId when non-null.
VectorNetwork AddVertex(VectorNetwork network, Vec2 point, uint32_t *outId);

// Adds a directed edge start→end. No-op (and leaves outId unchanged/0) when an
// undirected edge between the endpoints already exists (either direction), or
// endpoints are invalid / equal. Writes the new edge id to outId when non-null
// and an edge is added.
VectorNetwork AddEdge(VectorNetwork network, uint32_t start, uint32_t end, uint32_t *outId);

// Moves the vertex point. Incident edge tangents stay relative offsets.
VectorNetwork MoveVertex(VectorNetwork network, uint32_t id, Vec2 point);

// Sets the handle at one end of an edge. When mirror is true and the endpoint
// has degree == 2, mirrors the other incident edge's handle at that vertex to
// -tangent. Degree != 2 ignores mirror.
VectorNetwork MoveEdgeTangent(VectorNetwork network, uint32_t edgeId, bool atStart, Vec2 tangent,
                              bool mirror);

// Splits edge at cubic parameter t into two edges and a new mid vertex.
VectorNetwork InsertVertexOnEdge(VectorNetwork network, uint32_t edgeId, float t, uint32_t *outId);

// Removes the vertex and every incident edge.
VectorNetwork RemoveVertex(VectorNetwork network, uint32_t id);

VectorNetwork RemoveEdge(VectorNetwork network, uint32_t id);

// Corner ↔ smooth for a degree-2 vertex (auto 1/3-chord handles, or clear).
// Degree ≠ 2 is a no-op — shared hubs must not mirror / auto-smooth.
VectorNetwork ToggleVertexSmooth(VectorNetwork network, uint32_t vertexId);

// Translates all vertices so AABB center is at origin. Tangents unchanged
// (relative). Writes the previous center to localCenterOut (zero when no-op).
VectorNetwork RecenterNetwork(VectorNetwork network, Vec2 &localCenterOut);

}  // namespace motion
