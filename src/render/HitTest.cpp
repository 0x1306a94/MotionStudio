#include "MotionStudio/render/HitTest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "MotionStudio/model/FillRule.h"

namespace motion {

namespace {

constexpr int kSamplesPerSegment = 16;
constexpr float kEpsilon = 1e-6f;

float Cross(Vec2 lhs, Vec2 rhs) {
    return lhs.x * rhs.y - lhs.y * rhs.x;
}

float Dot(Vec2 lhs, Vec2 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

float LengthSquared(Vec2 value) {
    return Dot(value, value);
}

Vec2 CubicPoint(Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3, float t) {
    const float mt = 1.0f - t;
    return p0 * (mt * mt * mt) + c1 * (3.0f * mt * mt * t) +
        c2 * (3.0f * mt * t * t) + p3 * (t * t * t);
}

bool HasCurveHandles(const BezierPath::Vertex &from, const BezierPath::Vertex &to) {
    return LengthSquared(from.outTangent) > kEpsilon || LengthSquared(to.inTangent) > kEpsilon;
}

std::vector<Vec2> FlattenPath(const BezierPath &path) {
    std::vector<Vec2> points;
    const size_t count = path.vertices.size();
    if (count == 0) {
        return points;
    }
    points.push_back(path.vertices.front().point);
    const size_t segmentCount = path.closed ? count : count - 1;
    for (size_t segment = 0; segment < segmentCount; ++segment) {
        const BezierPath::Vertex &from = path.vertices[segment];
        const BezierPath::Vertex &to = path.vertices[(segment + 1) % count];
        if (HasCurveHandles(from, to)) {
            const Vec2 c1 = from.point + from.outTangent;
            const Vec2 c2 = to.point + to.inTangent;
            for (int sample = 1; sample <= kSamplesPerSegment; ++sample) {
                const float t = static_cast<float>(sample) / static_cast<float>(kSamplesPerSegment);
                points.push_back(CubicPoint(from.point, c1, c2, to.point, t));
            }
        } else {
            points.push_back(to.point);
        }
    }
    return points;
}

float DistanceSquaredToSegment(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float lengthSquared = LengthSquared(ab);
    if (lengthSquared <= kEpsilon) {
        return LengthSquared(point - a);
    }
    const float t = std::clamp(Dot(point - a, ab) / lengthSquared, 0.0f, 1.0f);
    return LengthSquared(point - (a + ab * t));
}

bool IsNearPolyline(const std::vector<Vec2> &points, bool closed, Vec2 point, float tolerance) {
    if (points.size() < 2) {
        return false;
    }
    const float toleranceSquared = tolerance * tolerance;
    for (size_t index = 0; index + 1 < points.size(); ++index) {
        if (DistanceSquaredToSegment(point, points[index], points[index + 1]) <= toleranceSquared) {
            return true;
        }
    }
    if (closed && DistanceSquaredToSegment(point, points.back(), points.front()) <= toleranceSquared) {
        return true;
    }
    return false;
}

int WindingNumber(const std::vector<Vec2> &points, Vec2 point) {
    int winding = 0;
    const size_t count = points.size();
    for (size_t index = 0; index < count; ++index) {
        const Vec2 a = points[index];
        const Vec2 b = points[(index + 1) % count];
        if (a.y <= point.y) {
            if (b.y > point.y && Cross(b - a, point - a) > 0.0f) {
                ++winding;
            }
        } else if (b.y <= point.y && Cross(b - a, point - a) < 0.0f) {
            --winding;
        }
    }
    return winding;
}

bool ContainsPoint(const std::vector<Vec2> &points, FillRule fillRule, Vec2 point) {
    if (points.size() < 3) {
        return false;
    }
    const int winding = WindingNumber(points, point);
    if (fillRule == FillRule::EvenOdd) {
        return winding % 2 != 0;
    }
    return winding != 0;
}

void ExpandBounds(const std::vector<Vec2> &points, Vec2 &minPoint, Vec2 &maxPoint) {
    for (Vec2 point : points) {
        minPoint.x = std::min(minPoint.x, point.x);
        minPoint.y = std::min(minPoint.y, point.y);
        maxPoint.x = std::max(maxPoint.x, point.x);
        maxPoint.y = std::max(maxPoint.y, point.y);
    }
}

void ExpandBounds(const std::vector<Vec2> &points, float padding, Vec2 &minPoint, Vec2 &maxPoint) {
    for (Vec2 point : points) {
        minPoint.x = std::min(minPoint.x, point.x - padding);
        minPoint.y = std::min(minPoint.y, point.y - padding);
        maxPoint.x = std::max(maxPoint.x, point.x + padding);
        maxPoint.y = std::max(maxPoint.y, point.y + padding);
    }
}

bool BoundsContain(Vec2 minPoint, Vec2 maxPoint, Vec2 point) {
    return point.x >= minPoint.x && point.x <= maxPoint.x && point.y >= minPoint.y &&
        point.y <= maxPoint.y;
}

struct FlattenedShapeItem {
    const EvaluatedShapeItem *item = nullptr;
    std::vector<Vec2> points;
};

bool HitTestShapeItem(const EvaluatedShapeItem &item, const std::vector<Vec2> &points, Vec2 point, float tolerance) {
    const float strokeTolerance = std::max(tolerance, item.strokeWidth * 0.5f + tolerance);
    if (item.isStroke) {
        return IsNearPolyline(points, item.path.closed, point, strokeTolerance);
    }
    if (ContainsPoint(points, item.paint.fillRule, point)) {
        return true;
    }
    return IsNearPolyline(points, item.path.closed, point, tolerance);
}

}  // namespace

bool HitTestLayer(const EvaluatedLayer &layer, Vec2 point, float tolerance) {
    if (layer.opacity <= 0.0f) {
        return false;
    }
    const float safeTolerance = std::max(tolerance, 0.0f);
    Vec2 minPoint{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec2 maxPoint{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    bool hasBounds = false;
    std::vector<FlattenedShapeItem> flattenedItems;
    flattenedItems.reserve(layer.shapeItems.size());

    for (const EvaluatedShapeItem &item : layer.shapeItems) {
        FlattenedShapeItem flattened;
        flattened.item = &item;
        flattened.points = FlattenPath(item.path);
        if (flattened.points.empty()) {
            continue;
        }
        const float padding = item.isStroke ? item.strokeWidth * 0.5f + safeTolerance : safeTolerance;
        ExpandBounds(flattened.points, padding, minPoint, maxPoint);
        hasBounds = true;
        flattenedItems.push_back(std::move(flattened));
    }

    if (!hasBounds || !BoundsContain(minPoint, maxPoint, point)) {
        return false;
    }

    for (const FlattenedShapeItem &flattened : flattenedItems) {
        if (flattened.item != nullptr && HitTestShapeItem(*flattened.item, flattened.points, point, safeTolerance)) {
            return true;
        }
    }
    return false;
}

bool BoundsOfLayer(const EvaluatedLayer &layer, Vec2 &minPoint, Vec2 &maxPoint) {
    if (layer.opacity <= 0.0f || layer.shapeItems.empty()) {
        return false;
    }
    minPoint = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    maxPoint = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    bool hasBounds = false;
    for (const EvaluatedShapeItem &item : layer.shapeItems) {
        std::vector<Vec2> points = FlattenPath(item.path);
        if (points.empty()) {
            continue;
        }
        ExpandBounds(points, minPoint, maxPoint);
        hasBounds = true;
    }
    return hasBounds;
}

EntityId HitTestLayerAtPoint(const SceneState &state, Vec2 point, float tolerance) {
    for (auto it = state.layers.rbegin(); it != state.layers.rend(); ++it) {
        if (HitTestLayer(*it, point, tolerance)) {
            return it->id;
        }
    }
    return {};
}

}  // namespace motion
