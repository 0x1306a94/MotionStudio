#include "MotionStudio/common/PathGeometryEdit.h"

#include <algorithm>
#include <utility>

namespace motion {

namespace {

struct SegmentSplit {
    Vec2 leftOutTangent = {};
    BezierPath::Vertex mid = {};
    Vec2 rightInTangent = {};
};

// de Casteljau split matching PathResample::SplitVertex semantics, plus the
// rewritten end-handle offsets for the left and right half-segments.
SegmentSplit SplitSegment(Vec2 p0, Vec2 outTangent0, Vec2 inTangent3, Vec2 p3, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const Vec2 c1 = p0 + outTangent0;
    const Vec2 c2 = p3 + inTangent3;
    const Vec2 a = p0 + (c1 - p0) * clamped;
    const Vec2 b = c1 + (c2 - c1) * clamped;
    const Vec2 c = c2 + (p3 - c2) * clamped;
    const Vec2 d = a + (b - a) * clamped;
    const Vec2 e = b + (c - b) * clamped;
    const Vec2 f = d + (e - d) * clamped;

    SegmentSplit split;
    split.leftOutTangent = a - p0;
    split.mid = {f, d - f, e - f};
    split.rightInTangent = c - p3;
    return split;
}

size_t SegmentCount(const BezierPath &path) {
    const size_t count = path.vertices.size();
    if (count < 2) {
        return 0;
    }
    return path.closed ? count : count - 1;
}

}  // namespace

BezierPath MoveVertex(BezierPath path, size_t index, Vec2 newPoint, bool linkedHandles) {
    if (index >= path.vertices.size()) {
        return path;
    }
    BezierPath::Vertex &vertex = path.vertices[index];
    if (!linkedHandles) {
        const Vec2 absoluteIn = vertex.point + vertex.inTangent;
        const Vec2 absoluteOut = vertex.point + vertex.outTangent;
        vertex.point = newPoint;
        vertex.inTangent = absoluteIn - newPoint;
        vertex.outTangent = absoluteOut - newPoint;
        return path;
    }
    vertex.point = newPoint;
    return path;
}

BezierPath MoveInTangent(BezierPath path, size_t index, Vec2 newIn, bool mirrorOut) {
    if (index >= path.vertices.size()) {
        return path;
    }
    BezierPath::Vertex &vertex = path.vertices[index];
    vertex.inTangent = newIn;
    if (mirrorOut) {
        vertex.outTangent = -newIn;
    }
    return path;
}

BezierPath MoveOutTangent(BezierPath path, size_t index, Vec2 newOut, bool mirrorIn) {
    if (index >= path.vertices.size()) {
        return path;
    }
    BezierPath::Vertex &vertex = path.vertices[index];
    vertex.outTangent = newOut;
    if (mirrorIn) {
        vertex.inTangent = -newOut;
    }
    return path;
}

BezierPath InsertVertexOnSegment(BezierPath path, size_t segmentIndex, float t) {
    const size_t count = path.vertices.size();
    if (segmentIndex >= SegmentCount(path)) {
        return path;
    }
    const size_t nextIndex = (segmentIndex + 1) % count;
    BezierPath::Vertex &from = path.vertices[segmentIndex];
    BezierPath::Vertex &to = path.vertices[nextIndex];
    const SegmentSplit split =
        SplitSegment(from.point, from.outTangent, to.inTangent, to.point, t);
    from.outTangent = split.leftOutTangent;
    to.inTangent = split.rightInTangent;
    path.vertices.insert(path.vertices.begin() + static_cast<std::ptrdiff_t>(nextIndex),
                         split.mid);
    return path;
}

BezierPath RemoveVertex(BezierPath path, size_t index) {
    if (index >= path.vertices.size() || path.vertices.size() <= 2) {
        return path;
    }
    path.vertices.erase(path.vertices.begin() + static_cast<std::ptrdiff_t>(index));
    return path;
}

BezierPath ClosePath(BezierPath path) {
    if (path.vertices.size() < 2) {
        return path;
    }
    path.closed = true;
    return path;
}

BezierPath AppendVertex(BezierPath path, BezierPath::Vertex vertex) {
    if (path.closed) {
        return path;
    }
    path.vertices.push_back(std::move(vertex));
    return path;
}

}  // namespace motion
