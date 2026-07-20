#pragma once

namespace motion {

// Easing curve: controls the interpolation rhythm between consecutive keyframes.
// The Bezier type defines a cubic bezier with P0=(0,0), P1=(inX,inY),
// P2=(outX,outY), P3=(1,1), matching the CSS cubic-bezier(x1, y1, x2, y2)
// convention where (inX,inY)=(x1,y1) and (outX,outY)=(x2,y2).
struct Easing {
    enum class Type {
        Linear,
        Bezier,
        Hold
    };

    Type type = Type::Linear;
    float inX = 0;
    float inY = 0;
    float outX = 1;
    float outY = 1;

    // Returns a linear (constant-speed) easing.
    static Easing Linear();

    // Returns a hold easing — the value snaps to the next keyframe with no transition.
    static Easing Hold();

    // Returns a cubic bezier easing.
    // inX: x coordinate of the first control point.
    // inY: y coordinate of the first control point.
    // outX: x coordinate of the second control point.
    // outY: y coordinate of the second control point.
    static Easing Bezier(float inX, float inY, float outX, float outY);

    // Returns a standard ease-in curve: Bezier(0.42, 0, 1, 1).
    static Easing EaseIn();

    // Returns a standard ease-out curve: Bezier(0, 0, 0.58, 1).
    static Easing EaseOut();

    bool operator==(const Easing &other) const;
    bool operator!=(const Easing &other) const;
};

// Maps a time progress in [0,1] to a value progress in [0,1].
// For Bezier easings the y component may exceed [0,1] to produce overshoot effects.
// easing: the easing curve to evaluate.
// progress: normalized time progress in [0,1].
float ApplyEasing(const Easing &easing, float progress);

// Solves the cubic bezier for y given x, using Newton's method with bisection
// fallback (same strategy as CSS animation engines).
// Requires x1, x2 in [0,1] so the x-axis is monotonic; y1, y2 may exceed [0,1].
// x1: x coordinate of the first control point.
// y1: y coordinate of the first control point.
// x2: x coordinate of the second control point.
// y2: y coordinate of the second control point.
// x: the x value at which to evaluate the curve.
float SolveBezierEasing(float x1, float y1, float x2, float y2, float x);

}  // namespace motion
