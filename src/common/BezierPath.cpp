#include "MotionStudio/common/BezierPath.h"

namespace motion {

bool BezierPath::Vertex::operator==(const Vertex &other) const {
    return point == other.point && inTangent == other.inTangent &&
        outTangent == other.outTangent;
}

bool BezierPath::Vertex::operator!=(const Vertex &other) const {
    return !(*this == other);
}

bool BezierPath::operator==(const BezierPath &other) const {
    return vertices == other.vertices && closed == other.closed;
}

bool BezierPath::operator!=(const BezierPath &other) const {
    return !(*this == other);
}

}  // namespace motion
