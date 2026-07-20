#pragma once

#include <vector>

#include "MotionStudio/common/Vec2.h"

namespace motion {

// Bezier path using the Lottie tangent convention: tangents are offsets
// relative to the vertex — control points are (point + outTangent) for the
// outgoing handle and (nextVertex.point + nextVertex.inTangent) for the
// incoming handle.
struct BezierPath {
    // A single path vertex with optional cubic tangent handles.
    struct Vertex {
        // Position of this vertex.
        Vec2 point;
        // Incoming tangent offset (used when approaching from the previous vertex).
        Vec2 inTangent;
        // Outgoing tangent offset (used when leaving towards the next vertex).
        Vec2 outTangent;

        bool operator==(const Vertex& other) const;
        bool operator!=(const Vertex& other) const;
    };

    std::vector<Vertex> vertices;
    // True if the last vertex connects back to the first, forming a closed loop.
    bool closed = false;

    bool operator==(const BezierPath& other) const;
    bool operator!=(const BezierPath& other) const;
};

}  // namespace motion
