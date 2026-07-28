#include "MotionStudio/animation/PathSampling.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace motion {
namespace {

constexpr int kSamplesPerSegment = 32;

float Distance(Vec2 a, Vec2 b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

Vec2 CubicBezierPoint(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    const float mt = 1.0f - t;
    return p0 * (mt * mt * mt) + p1 * (3.0f * mt * mt * t) + p2 * (3.0f * mt * t * t) +
        p3 * (t * t * t);
}

Vec2 CubicBezierTangent(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    const float mt = 1.0f - t;
    return (p1 - p0) * (3.0f * mt * mt) + (p2 - p1) * (6.0f * mt * t) +
        (p3 - p2) * (3.0f * t * t);
}

size_t SegmentCount(const BezierPath &path) {
    const size_t count = path.vertices.size();
    if (count < 2) {
        return 0;
    }
    if (path.closed) {
        return count;
    }
    return count - 1;
}

void SegmentControlPoints(const BezierPath &path, size_t segmentIndex, Vec2 &p0, Vec2 &p1,
                          Vec2 &p2, Vec2 &p3) {
    const size_t count = path.vertices.size();
    const BezierPath::Vertex &from = path.vertices[segmentIndex];
    const BezierPath::Vertex &to = path.vertices[(segmentIndex + 1) % count];
    p0 = from.point;
    p1 = from.point + from.outTangent;
    p2 = to.point + to.inTangent;
    p3 = to.point;
}

float SegmentArcLength(const BezierPath &path, size_t segmentIndex) {
    Vec2 p0 = {};
    Vec2 p1 = {};
    Vec2 p2 = {};
    Vec2 p3 = {};
    SegmentControlPoints(path, segmentIndex, p0, p1, p2, p3);
    float length = 0.0f;
    Vec2 previous = CubicBezierPoint(p0, p1, p2, p3, 0.0f);
    for (int step = 1; step <= kSamplesPerSegment; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(kSamplesPerSegment);
        const Vec2 current = CubicBezierPoint(p0, p1, p2, p3, t);
        length += Distance(previous, current);
        previous = current;
    }
    return length;
}

}  // namespace

float PathArcLength(const BezierPath &path) {
    const size_t segments = SegmentCount(path);
    float total = 0.0f;
    for (size_t index = 0; index < segments; ++index) {
        total += SegmentArcLength(path, index);
    }
    return total;
}

PathSample PointAndTangentAtArcLength(const BezierPath &path, float arcLength) {
    PathSample sample;
    sample.tangent = {1.0f, 0.0f};
    if (path.vertices.empty()) {
        return sample;
    }
    sample.point = path.vertices.front().point;

    const size_t segments = SegmentCount(path);
    if (segments == 0) {
        return sample;
    }

    const float total = PathArcLength(path);
    if (total <= 0.0f) {
        return sample;
    }

    const float target = std::clamp(arcLength, 0.0f, total);
    float remaining = target;
    for (size_t index = 0; index < segments; ++index) {
        const float segmentLength = SegmentArcLength(path, index);
        const bool lastSegment = index + 1 == segments;
        if (remaining > segmentLength && !lastSegment) {
            remaining -= segmentLength;
            continue;
        }

        Vec2 p0 = {};
        Vec2 p1 = {};
        Vec2 p2 = {};
        Vec2 p3 = {};
        SegmentControlPoints(path, index, p0, p1, p2, p3);

        if (segmentLength <= 0.0f) {
            sample.point = p3;
            sample.tangent = CubicBezierTangent(p0, p1, p2, p3, 1.0f);
            if (ApproxEqual(sample.tangent, {0.0f, 0.0f})) {
                sample.tangent = {1.0f, 0.0f};
            }
            return sample;
        }

        const float local = std::clamp(remaining, 0.0f, segmentLength);
        float walked = 0.0f;
        Vec2 previous = CubicBezierPoint(p0, p1, p2, p3, 0.0f);
        for (int step = 1; step <= kSamplesPerSegment; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(kSamplesPerSegment);
            const Vec2 current = CubicBezierPoint(p0, p1, p2, p3, t);
            const float stepLength = Distance(previous, current);
            if (walked + stepLength >= local || step == kSamplesPerSegment) {
                float frac = 0.0f;
                if (stepLength > 0.0f) {
                    frac = (local - walked) / stepLength;
                }
                frac = std::clamp(frac, 0.0f, 1.0f);
                const float t0 = static_cast<float>(step - 1) /
                    static_cast<float>(kSamplesPerSegment);
                const float sampleT = t0 + (t - t0) * frac;
                sample.point = CubicBezierPoint(p0, p1, p2, p3, sampleT);
                sample.tangent = CubicBezierTangent(p0, p1, p2, p3, sampleT);
                if (ApproxEqual(sample.tangent, {0.0f, 0.0f})) {
                    sample.tangent = current - previous;
                }
                if (ApproxEqual(sample.tangent, {0.0f, 0.0f})) {
                    sample.tangent = {1.0f, 0.0f};
                }
                return sample;
            }
            walked += stepLength;
            previous = current;
        }
    }
    return sample;
}

}  // namespace motion
