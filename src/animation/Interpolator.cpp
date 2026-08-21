#include "MotionStudio/animation/Interpolator.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>

#include "MotionStudio/animation/PathResample.h"

namespace motion {

namespace {

bool SameNetworkTopology(const VectorNetwork &from, const VectorNetwork &to) {
    if (from.vertices.size() != to.vertices.size() || from.edges.size() != to.edges.size()) {
        return false;
    }
    std::unordered_map<uint32_t, Vec2> toPoints;
    toPoints.reserve(to.vertices.size());
    for (const VectorNetwork::Vertex &vertex : to.vertices) {
        toPoints.emplace(vertex.id, vertex.point);
    }
    if (toPoints.size() != from.vertices.size()) {
        return false;
    }
    for (const VectorNetwork::Vertex &vertex : from.vertices) {
        if (toPoints.find(vertex.id) == toPoints.end()) {
            return false;
        }
    }
    std::unordered_map<uint32_t, const VectorNetwork::Edge *> toEdges;
    toEdges.reserve(to.edges.size());
    for (const VectorNetwork::Edge &edge : to.edges) {
        toEdges.emplace(edge.id, &edge);
    }
    if (toEdges.size() != from.edges.size()) {
        return false;
    }
    for (const VectorNetwork::Edge &edge : from.edges) {
        const auto found = toEdges.find(edge.id);
        if (found == toEdges.end()) {
            return false;
        }
        if (found->second->start != edge.start || found->second->end != edge.end) {
            return false;
        }
    }
    return true;
}

}  // namespace

float Interpolator<float>::Lerp(float from, float to, float t) {
    return from + (to - from) * t;
}

Vec2 Interpolator<Vec2>::Lerp(const Vec2 &from, const Vec2 &to, float t) {
    return from + (to - from) * t;
}

Vec3 Interpolator<Vec3>::Lerp(const Vec3 &from, const Vec3 &to, float t) {
    return from + (to - from) * t;
}

Vec4 Interpolator<Vec4>::Lerp(const Vec4 &from, const Vec4 &to, float t) {
    return from + (to - from) * t;
}

Color Interpolator<Color>::Lerp(const Color &from, const Color &to, float t) {
    return {
        Interpolator<float>::Lerp(from.r, to.r, t),
        Interpolator<float>::Lerp(from.g, to.g, t),
        Interpolator<float>::Lerp(from.b, to.b, t),
        Interpolator<float>::Lerp(from.a, to.a, t),
    };
}

BezierPath Interpolator<BezierPath>::Lerp(const BezierPath &from, const BezierPath &to,
                                          float t) {
    if (from.contours.size() != to.contours.size()) {
        return from;
    }
    BezierPath result;
    result.contours.reserve(from.contours.size());
    for (size_t contourIndex = 0; contourIndex < from.contours.size(); ++contourIndex) {
        const BezierPath::Contour &fromContour = from.contours[contourIndex];
        const BezierPath::Contour &toContour = to.contours[contourIndex];
        assert(fromContour.closed == toContour.closed && "BezierPath interpolation requires matching closed flags");
        if (fromContour.closed != toContour.closed) {
            return from;
        }
        BezierPath fromSingle = MakeSingleContour(fromContour.vertices, fromContour.closed);
        BezierPath toSingle = MakeSingleContour(toContour.vertices, toContour.closed);
        if (fromContour.vertices.size() != toContour.vertices.size()) {
            const size_t targetCount = std::max(fromContour.vertices.size(), toContour.vertices.size());
            fromSingle = ResamplePath(fromSingle, targetCount);
            toSingle = ResamplePath(toSingle, targetCount);
        }
        const BezierPath::Contour *fromPrimary = PrimaryContour(fromSingle);
        const BezierPath::Contour *toPrimary = PrimaryContour(toSingle);
        if (fromPrimary == nullptr || toPrimary == nullptr ||
            fromPrimary->vertices.size() != toPrimary->vertices.size()) {
            return from;
        }
        BezierPath::Contour outContour;
        outContour.closed = fromPrimary->closed;
        outContour.vertices.reserve(fromPrimary->vertices.size());
        for (size_t i = 0; i < fromPrimary->vertices.size(); ++i) {
            const BezierPath::Vertex &fromVertex = fromPrimary->vertices[i];
            const BezierPath::Vertex &toVertex = toPrimary->vertices[i];
            outContour.vertices.push_back({
                Interpolator<Vec2>::Lerp(fromVertex.point, toVertex.point, t),
                Interpolator<Vec2>::Lerp(fromVertex.inTangent, toVertex.inTangent, t),
                Interpolator<Vec2>::Lerp(fromVertex.outTangent, toVertex.outTangent, t),
            });
        }
        result.contours.push_back(std::move(outContour));
    }
    return result;
}

VectorNetwork Interpolator<VectorNetwork>::Lerp(const VectorNetwork &from, const VectorNetwork &to,
                                                float t) {
    if (!SameNetworkTopology(from, to)) {
        return from;
    }
    std::unordered_map<uint32_t, Vec2> toPoints;
    toPoints.reserve(to.vertices.size());
    for (const VectorNetwork::Vertex &vertex : to.vertices) {
        toPoints.emplace(vertex.id, vertex.point);
    }
    std::unordered_map<uint32_t, const VectorNetwork::Edge *> toEdges;
    toEdges.reserve(to.edges.size());
    for (const VectorNetwork::Edge &edge : to.edges) {
        toEdges.emplace(edge.id, &edge);
    }

    VectorNetwork result = from;
    for (VectorNetwork::Vertex &vertex : result.vertices) {
        const auto found = toPoints.find(vertex.id);
        if (found == toPoints.end()) {
            return from;
        }
        vertex.point = Interpolator<Vec2>::Lerp(vertex.point, found->second, t);
    }
    for (VectorNetwork::Edge &edge : result.edges) {
        const auto found = toEdges.find(edge.id);
        if (found == toEdges.end()) {
            return from;
        }
        edge.startTangent = Interpolator<Vec2>::Lerp(edge.startTangent, found->second->startTangent, t);
        edge.endTangent = Interpolator<Vec2>::Lerp(edge.endTangent, found->second->endTangent, t);
    }
    return result;
}

Vec2 CubicBezierPoint(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    const float mt = 1 - t;
    return p0 * (mt * mt * mt) + p1 * (3 * mt * mt * t) + p2 * (3 * mt * t * t) + p3 * (t * t * t);
}

Vec2 EvaluateSpatial(const Keyframe<Vec2> &from, const Keyframe<Vec2> &to,
                     float easedProgress) {
    if (!from.spatialOutTangent || !to.spatialInTangent) {
        return Interpolator<Vec2>::Lerp(from.value, to.value, easedProgress);
    }
    // Spatial cubic Bezier: P0=start, P1=start+outTangent, P2=end+inTangent, P3=end.
    return CubicBezierPoint(from.value, from.value + *from.spatialOutTangent,
                            to.value + *to.spatialInTangent, to.value, easedProgress);
}

}  // namespace motion
