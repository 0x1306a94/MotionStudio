#pragma once

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Interpolation strategy trait. Animatable<T> delegates value blending to
// this struct so it stays agnostic of T's specifics.
// The primary template performs step (hold) interpolation — discrete types
// like std::string keep the previous keyframe's value until the next one.
// (The primary template must remain defined in the header as a generic fallback.)
template <typename T>
struct Interpolator {
    static T Lerp(const T &from, const T & /*to*/, float /*t*/) {
        return from;
    }
};

// Specializations are implemented in src/animation/Interpolator.cpp.
template <>
struct Interpolator<float> {
    // from: start value.
    // to: end value.
    // t: blend factor in [0,1].
    static float Lerp(float from, float to, float t);
};

template <>
struct Interpolator<Vec2> {
    // from: start vector.
    // to: end vector.
    // t: blend factor in [0,1].
    static Vec2 Lerp(const Vec2 &from, const Vec2 &to, float t);
};

template <>
struct Interpolator<Color> {
    // from: start color.
    // to: end color.
    // t: blend factor in [0,1].
    static Color Lerp(const Color &from, const Color &to, float t);
};

template <>
struct Interpolator<BezierPath> {
    // Per-vertex interpolation. Mismatched vertex counts are handled by
    // resampling the shorter path along its edges (ResamplePath, M2 auto
    // vertex matching). Mismatched closed flags are a data-contract violation:
    // assert in debug builds, degrade to returning from in release builds.
    // from: start path.
    // to: end path.
    // t: blend factor in [0,1].
    static BezierPath Lerp(const BezierPath &from, const BezierPath &to, float t);
};

// Evaluates a point on a cubic bezier curve:
// B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3.
// p0: first control point.
// p1: second control point.
// p2: third control point.
// p3: fourth control point.
// t: parameter in [0,1].
Vec2 CubicBezierPoint(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t);

// Spatial interpolation between two Vec2 keyframes: follows a bezier arc
// when spatial tangent handles are present; falls back to linear lerp otherwise.
// from: start keyframe.
// to: end keyframe.
// easedProgress: eased blend factor in [0,1].
Vec2 EvaluateSpatial(const Keyframe<Vec2> &from, const Keyframe<Vec2> &to,
                     float easedProgress);

}  // namespace motion
