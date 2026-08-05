#include "MotionStudio/common/BezierPathTransform.h"

namespace motion {

BezierPath TransformBezierPath(const BezierPath &path, const Mat3 &matrix) {
    BezierPath result;
    result.contours.reserve(path.contours.size());
    for (const BezierPath::Contour &contour : path.contours) {
        BezierPath::Contour transformedContour;
        transformedContour.closed = contour.closed;
        transformedContour.vertices.reserve(contour.vertices.size());
        for (const BezierPath::Vertex &vertex : contour.vertices) {
            BezierPath::Vertex transformed;
            transformed.point = matrix.transformPoint(vertex.point);
            transformed.inTangent = matrix.transformVector(vertex.inTangent);
            transformed.outTangent = matrix.transformVector(vertex.outTangent);
            transformedContour.vertices.push_back(transformed);
        }
        result.contours.push_back(std::move(transformedContour));
    }
    return result;
}

}  // namespace motion
