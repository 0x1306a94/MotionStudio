#pragma once

#include <cstdint>
#include <vector>

#include "MotionStudio/common/Vec2.h"

namespace motion {

struct BezierPath;

namespace detail {
uint64_t GeometryRevision(const BezierPath &path);
void StampGeometryRevision(BezierPath &path);
}  // namespace detail

// Bezier path whose tangents are offsets relative to the vertex — control
// points are (point + outTangent) for the outgoing handle and
// (nextVertex.point + nextVertex.inTangent) for the incoming handle.
struct BezierPath {
    // A single path vertex with optional cubic tangent handles.
    struct Vertex {
        // Position of this vertex.
        Vec2 point;
        // Incoming tangent offset (used when approaching from the previous vertex).
        Vec2 inTangent;
        // Outgoing tangent offset (used when leaving towards the next vertex).
        Vec2 outTangent;

        bool operator==(const Vertex &other) const;
        bool operator!=(const Vertex &other) const;
    };

    // A single connected ring of vertices, either open or closed.
    struct Contour {
        std::vector<Vertex> vertices;
        // True if the last vertex connects back to the first, forming a closed loop.
        bool closed = false;

        bool operator==(const Contour &other) const;
        bool operator!=(const Contour &other) const;
    };

    std::vector<Contour> contours;

    // True when the path collapses to a point (width and height both zero),
    // including empty paths. Hairlines (zero on only one axis) return false.
    bool isZero() const;

    bool operator==(const BezierPath &other) const;
    bool operator!=(const BezierPath &other) const;

  private:
    uint64_t revision_ = 0;
    friend uint64_t detail::GeometryRevision(const BezierPath &);
    friend void detail::StampGeometryRevision(BezierPath &);
};

// Builds a BezierPath consisting of a single contour with the given vertices.
BezierPath MakeSingleContour(std::vector<BezierPath::Vertex> vertices, bool closed);

// True if the path has exactly one contour.
bool IsSingleContour(const BezierPath &path);

// Returns the first contour, or nullptr if the path has no contours.
const BezierPath::Contour *PrimaryContour(const BezierPath &path);
BezierPath::Contour *PrimaryContour(BezierPath &path);

}  // namespace motion
