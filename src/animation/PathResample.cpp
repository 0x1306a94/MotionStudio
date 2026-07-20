#include "MotionStudio/animation/PathResample.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace motion {

namespace {

constexpr size_t kSamplesPerSegment = 32;

float Length(Vec2 v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

Vec2 CubicPoint(Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3, float t) {
    const float mt = 1 - t;
    return p0 * (mt * mt * mt) + c1 * (3 * mt * mt * t) + c2 * (3 * mt * t * t) +
           p3 * (t * t * t);
}

// Flattened arc length of the curve portion [0, t].
float SegmentPrefixLength(Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3, float t) {
    constexpr size_t kRefineSamples = 16;
    float length = 0;
    Vec2 previous = p0;
    for (size_t i = 1; i <= kRefineSamples; ++i) {
        const float u = t * float(i) / float(kRefineSamples);
        const Vec2 point = CubicPoint(p0, c1, c2, p3, u);
        length += Length(point - previous);
        previous = point;
    }
    return length;
}

// de Casteljau split at t: the point on the curve plus the tangent handles of
// the right-half segment (in handle = D-F, out handle = E-F).
BezierPath::Vertex SplitVertex(Vec2 p0, Vec2 outTangent0, Vec2 inTangent3, Vec2 p3,
                               float t) {
    const Vec2 c1 = p0 + outTangent0;
    const Vec2 c2 = p3 + inTangent3;
    const Vec2 a = p0 + (c1 - p0) * t;
    const Vec2 b = c1 + (c2 - c1) * t;
    const Vec2 c = c2 + (p3 - c2) * t;
    const Vec2 d = a + (b - a) * t;
    const Vec2 e = b + (c - b) * t;
    const Vec2 f = d + (e - d) * t;
    return {f, d - f, e - f};
}

}  // namespace

BezierPath ResamplePath(const BezierPath& path, size_t vertexCount) {
    const size_t count = path.vertices.size();
    if (count < 2 || vertexCount < 2 || vertexCount == count) {
        return path;
    }
    const size_t segmentCount = path.closed ? count : count - 1;

    // Flatten every segment and build the cumulative arc-length table.
    std::vector<float> cumulative;
    cumulative.reserve(segmentCount * kSamplesPerSegment + 1);
    cumulative.push_back(0);
    for (size_t segment = 0; segment < segmentCount; ++segment) {
        const BezierPath::Vertex& from = path.vertices[segment];
        const BezierPath::Vertex& to = path.vertices[(segment + 1) % count];
        const Vec2 c1 = from.point + from.outTangent;
        const Vec2 c2 = to.point + to.inTangent;
        Vec2 previous = from.point;
        for (size_t sample = 1; sample <= kSamplesPerSegment; ++sample) {
            const float t = float(sample) / float(kSamplesPerSegment);
            const Vec2 point = CubicPoint(from.point, c1, c2, to.point, t);
            cumulative.push_back(cumulative.back() + Length(point - previous));
            previous = point;
        }
    }
    const float total = cumulative.back();
    if (total <= 0) {
        return path;  // degenerate path
    }

    BezierPath result;
    result.closed = path.closed;
    result.vertices.reserve(vertexCount);
    const size_t spanCount = path.closed ? vertexCount : vertexCount - 1;
    for (size_t k = 0; k < vertexCount; ++k) {
        const float target = total * float(k) / float(spanCount);
        const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), target);
        const size_t sampleIndex =
            std::clamp(size_t(it - cumulative.begin()), size_t(1), cumulative.size() - 1);
        const size_t segment = (sampleIndex - 1) / kSamplesPerSegment;
        const BezierPath::Vertex& from = path.vertices[segment];
        const BezierPath::Vertex& to = path.vertices[(segment + 1) % count];
        const Vec2 c1 = from.point + from.outTangent;
        const Vec2 c2 = to.point + to.inTangent;

        // Bisect t on the segment until the flattened prefix length matches
        // the target distance (exact for straight segments).
        const float prefixTarget = target - cumulative[segment * kSamplesPerSegment];
        float tLo = 0;
        float tHi = 1;
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float mid = (tLo + tHi) * 0.5f;
            if (SegmentPrefixLength(from.point, c1, c2, to.point, mid) < prefixTarget) {
                tLo = mid;
            } else {
                tHi = mid;
            }
        }
        result.vertices.push_back(SplitVertex(from.point, from.outTangent,
                                              to.inTangent, to.point,
                                              (tLo + tHi) * 0.5f));
    }
    return result;
}

}  // namespace motion
