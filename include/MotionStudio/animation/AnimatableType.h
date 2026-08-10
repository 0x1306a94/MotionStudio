#pragma once

namespace motion {

// Identifies the value type held by an Animatable<T>, so type-erased consumers
// can downcast to the right Animatable<T> via static_cast (dynamic_cast is
// banned by the coding rules).
enum class AnimatableType {
    Float,
    Vec2,
    Vec3,
    Color,
    // Authoring paths use VectorNetwork. BezierPath remains for export/helper
    // Animatable instances that hold compiled contours.
    BezierPath,
    VectorNetwork,
    String,
    Vec4
};

}  // namespace motion
