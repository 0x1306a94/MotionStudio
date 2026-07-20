#pragma once

#include <optional>
#include <vector>

#include "MotionStudio/animation/AnimatableType.h"
#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/Time.h"

namespace motion {

// Non-template base returned by PropertyPath resolution; consumers downcast
// to the concrete Animatable<T> via valueType() (dynamic_cast is banned).
class AnimatableBase {
  public:
    virtual ~AnimatableBase() = default;

    // Value type of the concrete Animatable<T>. Per-type definitions live in
    // src/animation/Animatable.cpp alongside the explicit instantiations.
    virtual AnimatableType valueType() const = 0;
};

// Animatable property: either a static value or an ordered keyframe sequence.
// Keyframe mutations must go through commands; UI never mutates the model directly.
// Implementation and explicit instantiations live in src/animation/Animatable.cpp;
// new animatable types must be registered there.
template <typename T>
class Animatable : public AnimatableBase {
  public:
    Animatable() = default;

    // Non-explicit: enables `Animatable<float> opacity{1};` style member init.
    // staticValue: the value used when no keyframes are present.
    Animatable(T staticValue);

    // Inserts a keyframe ordered by time, replacing any existing keyframe at the same time.
    // keyframe: keyframe to insert.
    void addKeyframe(Keyframe<T> keyframe);

    // Removes the keyframe at the given time. No-op if none exists.
    // time: frame time of the keyframe to remove.
    void removeKeyframe(FrameTime time);

    // Removes and returns the keyframe at the given time (used by MoveKeyframe).
    // Returns nullopt if no keyframe exists at that time.
    // time: frame time of the keyframe to take.
    std::optional<Keyframe<T>> takeKeyframe(FrameTime time);

    // Replaces the keyframe at the given time.
    // Returns false if no keyframe exists at that time.
    // time: frame time of the keyframe to update.
    // keyframe: replacement keyframe.
    bool updateKeyframe(FrameTime time, Keyframe<T> keyframe);

    // Removes all keyframes, leaving the static value in effect.
    void clearKeyframes();

    // Evaluates the property at the given time.
    // Returns the static value when no keyframes are present; clamps to the
    // first/last keyframe value outside the keyframe range (no extrapolation).
    // time: frame time to evaluate at.
    T evaluate(FrameTime time) const;

    AnimatableType valueType() const override;

    // Returns true if the property has keyframes (i.e. is animated rather than static).
    bool isAnimated() const;

    // Returns the current static value.
    const T &staticValue() const;

    // Sets the static value used when no keyframes are present.
    // value: new static value.
    void setStaticValue(T value);

    // Returns the keyframe list sorted by ascending time.
    const std::vector<Keyframe<T>> &keyframes() const;

  private:
    typename std::vector<Keyframe<T>>::iterator lowerBound(FrameTime time);
    typename std::vector<Keyframe<T>>::const_iterator upperBound(FrameTime time) const;
    typename std::vector<Keyframe<T>>::iterator find(FrameTime time);

    T value_{};
    // Keyframes sorted by ascending time.
    std::vector<Keyframe<T>> keyframes_;
};

}  // namespace motion
