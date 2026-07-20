#pragma once

#include <optional>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/undo/KeyframeData.h"
#include "MotionStudio/undo/PropertyValue.h"

// Internal type-erasure utilities shared across commands (not public API).
namespace motion {

class AnimatableBase;
class Composition;

// Set static value by dispatching on value type; returns false on type mismatch. Outputs previous value via oldValue.
bool ApplyStaticValueAny(AnimatableBase* target, const PropertyValue& newValue,
                         PropertyValue& oldValue);

// Extract keyframe by actual Animatable type; returns nullopt if absent or type unknown.
std::optional<KeyframeData> TakeKeyframeAny(AnimatableBase* target, FrameTime time);

// Insert keyframe by its actual type.
void AddKeyframeAny(AnimatableBase* target, const KeyframeData& keyframe);

FrameTime KeyframeTime(const KeyframeData& keyframe);
void SetKeyframeTime(KeyframeData& keyframe, FrameTime time);

// Set keyframe easing; returns false if no keyframe at time. Outputs old easing via oldEasingOut if non-null.
bool ApplyEasingAny(AnimatableBase* target, FrameTime time, const Easing& easing,
                    Easing* oldEasingOut);

// Layer index within composition; returns -1 if not found.
int IndexOfLayer(const Composition& composition, EntityId layerId);

}  // namespace motion
