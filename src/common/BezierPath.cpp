#include "MotionStudio/common/BezierPath.h"

#include <algorithm>

namespace motion {

bool BezierPath::Vertex::operator==(const Vertex &other) const {
    return point == other.point && inTangent == other.inTangent &&
        outTangent == other.outTangent;
}

bool BezierPath::Vertex::operator!=(const Vertex &other) const {
    return !(*this == other);
}

bool BezierPath::isZero() const {
    if (vertices.empty()) {
        return true;
    }
    float minX = vertices.front().point.x;
    float maxX = minX;
    float minY = vertices.front().point.y;
    float maxY = minY;
    for (const Vertex &vertex : vertices) {
        minX = std::min(minX, vertex.point.x);
        maxX = std::max(maxX, vertex.point.x);
        minY = std::min(minY, vertex.point.y);
        maxY = std::max(maxY, vertex.point.y);
        // Tangents can inflate a collapsed polyline into a real cubic.
        minX = std::min(minX, vertex.point.x + vertex.inTangent.x);
        maxX = std::max(maxX, vertex.point.x + vertex.inTangent.x);
        minY = std::min(minY, vertex.point.y + vertex.inTangent.y);
        maxY = std::max(maxY, vertex.point.y + vertex.inTangent.y);
        minX = std::min(minX, vertex.point.x + vertex.outTangent.x);
        maxX = std::max(maxX, vertex.point.x + vertex.outTangent.x);
        minY = std::min(minY, vertex.point.y + vertex.outTangent.y);
        maxY = std::max(maxY, vertex.point.y + vertex.outTangent.y);
    }
    return maxX <= minX && maxY <= minY;
}

bool BezierPath::operator==(const BezierPath &other) const {
    return vertices == other.vertices && closed == other.closed;
}

bool BezierPath::operator!=(const BezierPath &other) const {
    return !(*this == other);
}

}  // namespace motion
