#include "MotionStudio/common/BezierPathTransform.h"

namespace motion {

BezierPath TransformBezierPath(const BezierPath &path, const Mat3 &matrix) {
    BezierPath result;
    result.closed = path.closed;
    result.vertices.reserve(path.vertices.size());
    for (const BezierPath::Vertex &vertex : path.vertices) {
        BezierPath::Vertex transformed;
        transformed.point = matrix.transformPoint(vertex.point);
        transformed.inTangent = matrix.transformVector(vertex.inTangent);
        transformed.outTangent = matrix.transformVector(vertex.outTangent);
        result.vertices.push_back(transformed);
    }
    return result;
}

}  // namespace motion
