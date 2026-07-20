#include "MotionStudio/animation/Interpolator.h"

#include <stdexcept>

namespace motion {

Color Interpolator<Color>::lerp(const Color& from, const Color& to, float t) {
    return {
        Interpolator<float>::lerp(from.r, to.r, t),
        Interpolator<float>::lerp(from.g, to.g, t),
        Interpolator<float>::lerp(from.b, to.b, t),
        Interpolator<float>::lerp(from.a, to.a, t),
    };
}

BezierPath Interpolator<BezierPath>::lerp(const BezierPath& from, const BezierPath& to,
                                          float t) {
    if (from.vertices.size() != to.vertices.size()) {
        throw std::invalid_argument("BezierPath 插值要求两关键帧顶点数一致");
    }
    BezierPath result;
    result.closed = from.closed;
    result.vertices.reserve(from.vertices.size());
    for (size_t i = 0; i < from.vertices.size(); ++i) {
        const BezierPath::Vertex& fromVertex = from.vertices[i];
        const BezierPath::Vertex& toVertex = to.vertices[i];
        result.vertices.push_back({
            Interpolator<Vec2>::lerp(fromVertex.point, toVertex.point, t),
            Interpolator<Vec2>::lerp(fromVertex.inTangent, toVertex.inTangent, t),
            Interpolator<Vec2>::lerp(fromVertex.outTangent, toVertex.outTangent, t),
        });
    }
    return result;
}

Vec2 cubicBezierPoint(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    const float mt = 1 - t;
    return p0 * (mt * mt * mt) + p1 * (3 * mt * mt * t) + p2 * (3 * mt * t * t) +
           p3 * (t * t * t);
}

Vec2 evaluateSpatial(const Keyframe<Vec2>& from, const Keyframe<Vec2>& to,
                     float easedProgress) {
    if (!from.spatialOutTangent || !to.spatialInTangent) {
        return Interpolator<Vec2>::lerp(from.value, to.value, easedProgress);
    }
    // 空间三次贝塞尔：P0=起点, P1=起点+出手柄, P2=终点+入手柄, P3=终点。
    return cubicBezierPoint(from.value, from.value + *from.spatialOutTangent,
                            to.value + *to.spatialInTangent, to.value, easedProgress);
}

}  // namespace motion
