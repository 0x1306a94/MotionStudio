#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Math.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// 命令携带的属性值 / 关键帧（类型擦除，执行时按类型分发到 Animatable<T>）。
using PropertyValue = std::variant<float, Vec2, Color, BezierPath, std::string>;
using KeyframeData =
    std::variant<Keyframe<float>, Keyframe<Vec2>, Keyframe<Color>,
                 Keyframe<BezierPath>, Keyframe<std::string>>;

// 加图层。undo 时取回所有权，redo 按记录的下标重新插入。
class AddLayerCommand : public Command {
public:
    AddLayerCommand(EntityId compositionId, std::unique_ptr<Layer> layer, int index = -1);

    void execute(Document& document) override;
    void undo(Document& document) override;
    std::string describe() const override { return "Add Layer"; }

private:
    EntityId compositionId_;
    EntityId layerId_;
    int index_;
    std::unique_ptr<Layer> layer_;
};

// 删图层。execute 时接管 unique_ptr 所有权，undo 时原下标移回，
// 完整恢复子结构与关键帧。
class RemoveLayerCommand : public Command {
public:
    RemoveLayerCommand(EntityId compositionId, EntityId layerId);

    void execute(Document& document) override;
    void undo(Document& document) override;
    std::string describe() const override { return "Remove Layer"; }

private:
    EntityId compositionId_;
    EntityId layerId_;
    int index_ = -1;
    std::unique_ptr<Layer> layer_;
};

// 改图层顺序。连续拖拽（前一个 toIndex == 后一个 fromIndex）可合并。
class MoveLayerCommand : public Command {
public:
    MoveLayerCommand(EntityId compositionId, int fromIndex, int toIndex);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    std::string describe() const override { return "Move Layer"; }

private:
    EntityId compositionId_;
    int fromIndex_;
    int toIndex_;
};

// 设置静态值。首次执行捕获旧值；同目标连续设置可合并（拖拽数值）。
class SetStaticValueCommand : public Command {
public:
    SetStaticValueCommand(PropertyPath property, PropertyValue value);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    std::string describe() const override { return "Set Value"; }

private:
    PropertyPath property_;
    PropertyValue value_;
    std::optional<PropertyValue> oldValue_;
    bool captured_ = false;
};

// 加关键帧。若该帧已有则记录被替换者，undo 时还原。
class AddKeyframeCommand : public Command {
public:
    AddKeyframeCommand(PropertyPath property, KeyframeData keyframe);

    void execute(Document& document) override;
    void undo(Document& document) override;
    std::string describe() const override { return "Add Keyframe"; }

private:
    PropertyPath property_;
    KeyframeData keyframe_;
    std::optional<KeyframeData> replaced_;
    bool captured_ = false;
};

// 删关键帧。execute 时接管关键帧，undo 时放回。
class RemoveKeyframeCommand : public Command {
public:
    RemoveKeyframeCommand(PropertyPath property, FrameTime time);

    void execute(Document& document) override;
    void undo(Document& document) override;
    std::string describe() const override { return "Remove Keyframe"; }

private:
    PropertyPath property_;
    FrameTime time_;
    std::optional<KeyframeData> removed_;
    bool captured_ = false;
};

// 移动关键帧。记录目标帧上被覆盖的关键帧，undo 时一并还原。
// 连续拖拽（other.oldTime == 本命令 newTime）可合并。
class MoveKeyframeCommand : public Command {
public:
    MoveKeyframeCommand(PropertyPath property, FrameTime oldTime, FrameTime newTime);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    std::string describe() const override { return "Move Keyframe"; }

private:
    PropertyPath property_;
    FrameTime oldTime_;
    FrameTime newTime_;
    std::optional<KeyframeData> overwritten_;
    bool captured_ = false;
};

// 改关键帧缓动。目标帧不存在则为空操作。
class SetEasingCommand : public Command {
public:
    SetEasingCommand(PropertyPath property, FrameTime time, Easing easing);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    std::string describe() const override { return "Set Easing"; }

private:
    PropertyPath property_;
    FrameTime time_;
    Easing easing_;
    std::optional<Easing> oldEasing_;
    bool captured_ = false;
    bool found_ = false;
};

}  // namespace motion
