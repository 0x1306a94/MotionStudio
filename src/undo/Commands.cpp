#include "MotionStudio/undo/Commands.h"

#include <algorithm>
#include <utility>

#include "MotionStudio/model/Document.h"

namespace motion {

namespace {

// ---- 类型擦除工具：在 AnimatableBase 上按候选类型分发 ----

template <typename T>
bool applyStaticValue(AnimatableBase* target, const PropertyValue& newValue,
                      PropertyValue& oldValue) {
    auto* animatable = dynamic_cast<Animatable<T>*>(target);
    if (!animatable) return false;
    oldValue = animatable->staticValue();
    return std::visit(
        [&](const auto& typed) {
            using ValueType = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<ValueType, T>) {
                animatable->setStaticValue(typed);
                return true;
            } else {
                return false;  // 值类型与属性类型不符
            }
        },
        newValue);
}

bool applyStaticValueAny(AnimatableBase* target, const PropertyValue& newValue,
                         PropertyValue& oldValue) {
    if (applyStaticValue<float>(target, newValue, oldValue)) return true;
    if (applyStaticValue<Vec2>(target, newValue, oldValue)) return true;
    if (applyStaticValue<Color>(target, newValue, oldValue)) return true;
    if (applyStaticValue<BezierPath>(target, newValue, oldValue)) return true;
    return applyStaticValue<std::string>(target, newValue, oldValue);
}

std::optional<KeyframeData> takeKeyframeAny(AnimatableBase* target, FrameTime time) {
    if (auto* animatable = dynamic_cast<Animatable<float>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) return std::nullopt;
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<Vec2>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) return std::nullopt;
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<Color>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) return std::nullopt;
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<BezierPath>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) return std::nullopt;
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<std::string>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) return std::nullopt;
        return KeyframeData{std::move(*keyframe)};
    }
    return std::nullopt;
}

void addKeyframeAny(AnimatableBase* target, const KeyframeData& keyframe) {
    std::visit(
        [&](const auto& typed) {
            using ValueType = std::decay_t<decltype(typed.value)>;
            auto* animatable = dynamic_cast<Animatable<ValueType>*>(target);
            if (animatable) animatable->addKeyframe(typed);
        },
        keyframe);
}

FrameTime keyframeTime(const KeyframeData& keyframe) {
    return std::visit([](const auto& typed) { return typed.time; }, keyframe);
}

void setKeyframeTime(KeyframeData& keyframe, FrameTime time) {
    std::visit([&](auto& typed) { typed.time = time; }, keyframe);
}

template <typename T>
bool applyEasing(AnimatableBase* target, FrameTime time, const Easing& easing,
                 Easing* oldEasingOut) {
    auto* animatable = dynamic_cast<Animatable<T>*>(target);
    if (!animatable) return false;
    for (const Keyframe<T>& keyframe : animatable->keyframes()) {
        if (keyframe.time != time) continue;
        if (oldEasingOut) *oldEasingOut = keyframe.easing;
        Keyframe<T> updated = keyframe;
        updated.easing = easing;
        return animatable->updateKeyframe(time, std::move(updated));
    }
    return false;  // 该类型上无此帧
}

bool applyEasingAny(AnimatableBase* target, FrameTime time, const Easing& easing,
                    Easing* oldEasingOut) {
    if (auto* animatable = dynamic_cast<Animatable<float>*>(target))
        return applyEasing<float>(animatable, time, easing, oldEasingOut);
    if (auto* animatable = dynamic_cast<Animatable<Vec2>*>(target))
        return applyEasing<Vec2>(animatable, time, easing, oldEasingOut);
    if (auto* animatable = dynamic_cast<Animatable<Color>*>(target))
        return applyEasing<Color>(animatable, time, easing, oldEasingOut);
    if (auto* animatable = dynamic_cast<Animatable<BezierPath>*>(target))
        return applyEasing<BezierPath>(animatable, time, easing, oldEasingOut);
    if (auto* animatable = dynamic_cast<Animatable<std::string>*>(target))
        return applyEasing<std::string>(animatable, time, easing, oldEasingOut);
    return false;
}

int indexOfLayer(const Composition& composition, EntityId layerId) {
    for (size_t i = 0; i < composition.layers.size(); ++i) {
        if (composition.layers[i]->id == layerId) return int(i);
    }
    return -1;
}

}  // namespace

// ---- AddLayer ----

AddLayerCommand::AddLayerCommand(EntityId compositionId, std::unique_ptr<Layer> layer,
                                 int index)
    : compositionId_(compositionId), layerId_(layer ? layer->id : EntityId{}),
      index_(index), layer_(std::move(layer)) {}

void AddLayerCommand::execute(Document& document) {
    if (!layer_) return;
    Composition* composition = document.entityIndex().findComposition(compositionId_);
    if (!composition) return;  // 合成已删除 → 跳过
    document.addLayer(compositionId_, std::move(layer_), index_);
    index_ = indexOfLayer(*composition, layerId_);  // 记录实际位置供 undo/redo
}

void AddLayerCommand::undo(Document& document) {
    layer_ = document.takeLayer(compositionId_, layerId_);
}

// ---- RemoveLayer ----

RemoveLayerCommand::RemoveLayerCommand(EntityId compositionId, EntityId layerId)
    : compositionId_(compositionId), layerId_(layerId) {}

void RemoveLayerCommand::execute(Document& document) {
    Composition* composition = document.entityIndex().findComposition(compositionId_);
    if (!composition) return;
    index_ = indexOfLayer(*composition, layerId_);
    if (index_ < 0) return;  // 图层已删除 → 跳过
    layer_ = document.takeLayer(compositionId_, layerId_);
}

void RemoveLayerCommand::undo(Document& document) {
    if (!layer_) return;
    document.addLayer(compositionId_, std::move(layer_), index_);
}

// ---- MoveLayer ----

MoveLayerCommand::MoveLayerCommand(EntityId compositionId, int fromIndex, int toIndex)
    : compositionId_(compositionId), fromIndex_(fromIndex), toIndex_(toIndex) {}

void MoveLayerCommand::execute(Document& document) {
    document.moveLayer(compositionId_, fromIndex_, toIndex_);
}

void MoveLayerCommand::undo(Document& document) {
    document.moveLayer(compositionId_, toIndex_, fromIndex_);
}

bool MoveLayerCommand::mergeWith(const Command& other) {
    const auto* typed = dynamic_cast<const MoveLayerCommand*>(&other);
    if (!typed || typed->compositionId_ != compositionId_) return false;
    if (typed->fromIndex_ != toIndex_) return false;  // 仅合并连续拖动
    toIndex_ = typed->toIndex_;
    return true;
}

// ---- SetStaticValue ----

SetStaticValueCommand::SetStaticValueCommand(PropertyPath property, PropertyValue value)
    : property_(std::move(property)), value_(std::move(value)) {}

void SetStaticValueCommand::execute(Document& document) {
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    PropertyValue oldValue;
    if (!applyStaticValueAny(target, value_, oldValue)) return;
    if (!captured_) {
        oldValue_ = std::move(oldValue);
        captured_ = true;
    }
}

void SetStaticValueCommand::undo(Document& document) {
    if (!captured_) return;
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    PropertyValue discarded;
    applyStaticValueAny(target, *oldValue_, discarded);
}

bool SetStaticValueCommand::mergeWith(const Command& other) {
    const auto* typed = dynamic_cast<const SetStaticValueCommand*>(&other);
    if (!typed || typed->property_ != property_) return false;
    value_ = typed->value_;  // 保留 oldValue_，吸收最终值
    return true;
}

// ---- AddKeyframe ----

AddKeyframeCommand::AddKeyframeCommand(PropertyPath property, KeyframeData keyframe)
    : property_(std::move(property)), keyframe_(std::move(keyframe)) {}

void AddKeyframeCommand::execute(Document& document) {
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    if (!captured_) {
        replaced_ = takeKeyframeAny(target, keyframeTime(keyframe_));
        captured_ = true;
    }
    addKeyframeAny(target, keyframe_);
}

void AddKeyframeCommand::undo(Document& document) {
    if (!captured_) return;
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    takeKeyframeAny(target, keyframeTime(keyframe_));  // 移除本次加入的
    if (replaced_) addKeyframeAny(target, *replaced_);
}

// ---- RemoveKeyframe ----

RemoveKeyframeCommand::RemoveKeyframeCommand(PropertyPath property, FrameTime time)
    : property_(std::move(property)), time_(time) {}

void RemoveKeyframeCommand::execute(Document& document) {
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    if (!captured_) {
        removed_ = takeKeyframeAny(target, time_);
        captured_ = true;
    } else {
        takeKeyframeAny(target, time_);  // redo：再次移除
    }
}

void RemoveKeyframeCommand::undo(Document& document) {
    if (!captured_ || !removed_) return;
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    addKeyframeAny(target, *removed_);
}

// ---- MoveKeyframe ----

MoveKeyframeCommand::MoveKeyframeCommand(PropertyPath property, FrameTime oldTime,
                                         FrameTime newTime)
    : property_(std::move(property)), oldTime_(oldTime), newTime_(newTime) {}

void MoveKeyframeCommand::execute(Document& document) {
    if (oldTime_ == newTime_) return;
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    if (!captured_) {
        overwritten_ = takeKeyframeAny(target, newTime_);
        captured_ = true;
    }
    std::optional<KeyframeData> moved = takeKeyframeAny(target, oldTime_);
    if (!moved) return;
    setKeyframeTime(*moved, newTime_);
    addKeyframeAny(target, *moved);
}

void MoveKeyframeCommand::undo(Document& document) {
    if (!captured_) return;
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    std::optional<KeyframeData> moved = takeKeyframeAny(target, newTime_);
    if (overwritten_) addKeyframeAny(target, *overwritten_);
    if (moved) {
        setKeyframeTime(*moved, oldTime_);
        addKeyframeAny(target, *moved);
    }
}

bool MoveKeyframeCommand::mergeWith(const Command& other) {
    const auto* typed = dynamic_cast<const MoveKeyframeCommand*>(&other);
    if (!typed || typed->property_ != property_) return false;
    if (typed->oldTime_ != newTime_) return false;  // 仅合并连续拖动
    newTime_ = typed->newTime_;
    return true;
}

// ---- SetEasing ----

SetEasingCommand::SetEasingCommand(PropertyPath property, FrameTime time, Easing easing)
    : property_(std::move(property)), time_(time), easing_(easing) {}

void SetEasingCommand::execute(Document& document) {
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    if (!captured_) {
        Easing oldEasing;
        found_ = applyEasingAny(target, time_, easing_, &oldEasing);
        if (found_) oldEasing_ = oldEasing;
        captured_ = true;
    } else if (found_) {
        applyEasingAny(target, time_, easing_, nullptr);
    }
}

void SetEasingCommand::undo(Document& document) {
    if (!captured_ || !found_) return;
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) return;
    applyEasingAny(target, time_, *oldEasing_, nullptr);
}

bool SetEasingCommand::mergeWith(const Command& other) {
    const auto* typed = dynamic_cast<const SetEasingCommand*>(&other);
    if (!typed || typed->property_ != property_ || typed->time_ != time_) return false;
    easing_ = typed->easing_;
    return true;
}

}  // namespace motion
