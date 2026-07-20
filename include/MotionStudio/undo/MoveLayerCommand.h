#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// 改图层顺序。连续拖拽（前一个 toIndex == 后一个 fromIndex）可合并。
class MoveLayerCommand : public Command {
public:
    MoveLayerCommand(EntityId compositionId, int fromIndex, int toIndex);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    std::string describe() const override;

private:
    EntityId compositionId_;
    int fromIndex_;
    int toIndex_;
};

}  // namespace motion
