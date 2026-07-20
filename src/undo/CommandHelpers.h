#pragma once

#include <optional>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/undo/KeyframeData.h"
#include "MotionStudio/undo/PropertyValue.h"

// 命令内部共用的类型擦除工具（非公共接口）。
namespace motion {

class AnimatableBase;
class Composition;

// 按值类型分发设置静态值；类型与属性不符返回 false。oldValue 输出设置前的值。
bool ApplyStaticValueAny(AnimatableBase* target, const PropertyValue& newValue,
                         PropertyValue& oldValue);

// 按 Animatable 实际类型取出关键帧；不存在 / 类型未知返回 nullopt。
std::optional<KeyframeData> TakeKeyframeAny(AnimatableBase* target, FrameTime time);

// 按关键帧实际类型插入。
void AddKeyframeAny(AnimatableBase* target, const KeyframeData& keyframe);

FrameTime KeyframeTime(const KeyframeData& keyframe);
void SetKeyframeTime(KeyframeData& keyframe, FrameTime time);

// 设置关键帧缓动；time 处无关键帧返回 false。oldEasingOut 非空时输出旧缓动。
bool ApplyEasingAny(AnimatableBase* target, FrameTime time, const Easing& easing,
                    Easing* oldEasingOut);

// 图层在合成中的下标；不存在返回 -1。
int IndexOfLayer(const Composition& composition, EntityId layerId);

}  // namespace motion
