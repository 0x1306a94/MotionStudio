#include "MotionStudio/common/BezierPath.h"

#include <algorithm>
#include <utility>

namespace motion {

bool BezierPath::Vertex::operator==(const Vertex &other) const {
    return point == other.point && inTangent == other.inTangent &&
        outTangent == other.outTangent;
}

bool BezierPath::Vertex::operator!=(const Vertex &other) const {
    return !(*this == other);
}

bool BezierPath::Contour::operator==(const Contour &other) const {
    return vertices == other.vertices && closed == other.closed;
}

bool BezierPath::Contour::operator!=(const Contour &other) const {
    return !(*this == other);
}

bool BezierPath::isZero() const {
    bool hasVertex = false;
    float minX = 0;
    float maxX = 0;
    float minY = 0;
    float maxY = 0;
    for (const Contour &contour : contours) {
        for (const Vertex &vertex : contour.vertices) {
            if (!hasVertex) {
                minX = maxX = vertex.point.x;
                minY = maxY = vertex.point.y;
                hasVertex = true;
            }
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
    }
    if (!hasVertex) {
        return true;
    }
    return maxX <= minX && maxY <= minY;
}

bool BezierPath::operator==(const BezierPath &other) const {
    return contours == other.contours;
}

bool BezierPath::operator!=(const BezierPath &other) const {
    return !(*this == other);
}

BezierPath MakeSingleContour(std::vector<BezierPath::Vertex> vertices, bool closed) {
    BezierPath path;
    BezierPath::Contour contour;
    contour.vertices = std::move(vertices);
    contour.closed = closed;
    path.contours.push_back(std::move(contour));
    return path;
}

bool IsSingleContour(const BezierPath &path) {
    return path.contours.size() == 1;
}

const BezierPath::Contour *PrimaryContour(const BezierPath &path) {
    return path.contours.empty() ? nullptr : &path.contours.front();
}

BezierPath::Contour *PrimaryContour(BezierPath &path) {
    return path.contours.empty() ? nullptr : &path.contours.front();
}

}  // namespace motion
