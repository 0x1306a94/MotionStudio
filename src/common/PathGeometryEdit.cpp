#include "MotionStudio/common/PathGeometryEdit.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace motion {

namespace {

constexpr float kCornerEpsilon = 1e-4f;

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

size_t SegmentCount(const BezierPath::Contour &contour) {
    const size_t count = contour.vertices.size();
    if (count < 2) {
        return 0;
    }
    return contour.closed ? count : count - 1;
}

float LengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

bool IsNearZero(Vec2 value) {
    return LengthSquared(value) <= kCornerEpsilon * kCornerEpsilon;
}

Vec2 SafeNormalize(Vec2 value) {
    const float length = std::sqrt(LengthSquared(value));
    if (length <= kCornerEpsilon) {
        return {};
    }
    return value * (1.0f / length);
}

}  // namespace

BezierPath MoveVertex(BezierPath path, size_t index, Vec2 newPoint, bool linkedHandles) {
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path) || index >= contour->vertices.size()) {
        return path;
    }
    BezierPath::Vertex &vertex = contour->vertices[index];
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
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path) || index >= contour->vertices.size()) {
        return path;
    }
    BezierPath::Vertex &vertex = contour->vertices[index];
    vertex.inTangent = newIn;
    if (mirrorOut) {
        vertex.outTangent = -newIn;
    }
    return path;
}

BezierPath MoveOutTangent(BezierPath path, size_t index, Vec2 newOut, bool mirrorIn) {
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path) || index >= contour->vertices.size()) {
        return path;
    }
    BezierPath::Vertex &vertex = contour->vertices[index];
    vertex.outTangent = newOut;
    if (mirrorIn) {
        vertex.inTangent = -newOut;
    }
    return path;
}

BezierPath InsertVertexOnSegment(BezierPath path, size_t segmentIndex, float t) {
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path)) {
        return path;
    }
    const size_t count = contour->vertices.size();
    if (segmentIndex >= SegmentCount(*contour)) {
        return path;
    }
    const size_t nextIndex = (segmentIndex + 1) % count;
    BezierPath::Vertex &from = contour->vertices[segmentIndex];
    BezierPath::Vertex &to = contour->vertices[nextIndex];
    const SegmentSplit split =
        SplitSegment(from.point, from.outTangent, to.inTangent, to.point, t);
    from.outTangent = split.leftOutTangent;
    to.inTangent = split.rightInTangent;
    contour->vertices.insert(contour->vertices.begin() + static_cast<std::ptrdiff_t>(nextIndex),
                             split.mid);
    return path;
}

BezierPath RemoveVertex(BezierPath path, size_t index) {
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path) || index >= contour->vertices.size() ||
        contour->vertices.size() <= 2) {
        return path;
    }
    contour->vertices.erase(contour->vertices.begin() + static_cast<std::ptrdiff_t>(index));
    return path;
}

BezierPath ClosePath(BezierPath path) {
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path) || contour->vertices.size() < 2) {
        return path;
    }
    contour->closed = true;
    return path;
}

BezierPath AppendVertex(BezierPath path, BezierPath::Vertex vertex) {
    if (path.contours.empty()) {
        path.contours.push_back({});
    }
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path) || contour->closed) {
        return path;
    }
    contour->vertices.push_back(std::move(vertex));
    return path;
}

BezierPath ToggleVertexSmooth(BezierPath path, size_t index) {
    BezierPath::Contour *contour = PrimaryContour(path);
    if (contour == nullptr || !IsSingleContour(path) || index >= contour->vertices.size()) {
        return path;
    }
    BezierPath::Vertex &vertex = contour->vertices[index];
    const bool isCorner = IsNearZero(vertex.inTangent) && IsNearZero(vertex.outTangent);
    if (!isCorner) {
        vertex.inTangent = {};
        vertex.outTangent = {};
        return path;
    }

    const size_t count = contour->vertices.size();
    if (count < 2) {
        return path;
    }

    const bool hasPrev = contour->closed || index > 0;
    const bool hasNext = contour->closed || index + 1 < count;
    if (!hasPrev && !hasNext) {
        return path;
    }

    const Vec2 point = vertex.point;
    Vec2 prevPoint = point;
    Vec2 nextPoint = point;
    if (hasPrev) {
        prevPoint = contour->vertices[(index + count - 1) % count].point;
    }
    if (hasNext) {
        nextPoint = contour->vertices[(index + 1) % count].point;
    }

    Vec2 direction;
    if (hasPrev && hasNext) {
        direction = SafeNormalize(nextPoint - prevPoint);
    } else if (hasNext) {
        direction = SafeNormalize(nextPoint - point);
    } else {
        direction = SafeNormalize(point - prevPoint);
    }
    if (IsNearZero(direction)) {
        return path;
    }

    const float inLength =
        hasPrev ? std::sqrt(LengthSquared(point - prevPoint)) / 3.0f : 0.0f;
    const float outLength =
        hasNext ? std::sqrt(LengthSquared(nextPoint - point)) / 3.0f : 0.0f;
    vertex.inTangent = direction * (-inLength);
    vertex.outTangent = direction * outLength;
    return path;
}

BezierPath RecenterPath(BezierPath path, Vec2 &localCenterOut) {
    localCenterOut = {};
    if (path.contours.empty()) {
        return path;
    }
    bool hasVertex = false;
    Vec2 minPoint = {};
    Vec2 maxPoint = {};
    for (const BezierPath::Contour &contour : path.contours) {
        for (const BezierPath::Vertex &vertex : contour.vertices) {
            if (!hasVertex) {
                minPoint = vertex.point;
                maxPoint = vertex.point;
                hasVertex = true;
            }
            minPoint.x = std::min(minPoint.x, vertex.point.x);
            minPoint.y = std::min(minPoint.y, vertex.point.y);
            maxPoint.x = std::max(maxPoint.x, vertex.point.x);
            maxPoint.y = std::max(maxPoint.y, vertex.point.y);
        }
    }
    if (!hasVertex) {
        return path;
    }
    const Vec2 center{(minPoint.x + maxPoint.x) * 0.5f, (minPoint.y + maxPoint.y) * 0.5f};
    if (IsNearZero(center)) {
        return path;
    }
    localCenterOut = center;
    for (BezierPath::Contour &contour : path.contours) {
        for (BezierPath::Vertex &vertex : contour.vertices) {
            vertex.point = vertex.point - center;
        }
    }
    return path;
}

}  // namespace motion
