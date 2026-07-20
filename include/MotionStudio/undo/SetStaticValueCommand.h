#pragma once

#include <optional>
#include <string>

#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/PropertyValue.h"

namespace motion {

// 设置静态值。首次执行捕获旧值；同目标连续设置可合并（拖拽数值）。
class SetStaticValueCommand : public Command {
public:
    SetStaticValueCommand(PropertyPath property, PropertyValue value);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    PropertyPath property_;
    PropertyValue value_;
    std::optional<PropertyValue> oldValue_;
    bool captured_ = false;
};

}  // namespace motion
