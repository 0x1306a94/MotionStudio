#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// 加图层。undo 时取回所有权，redo 按记录的下标重新插入。
class AddLayerCommand : public Command {
public:
    AddLayerCommand(EntityId compositionId, std::unique_ptr<Layer> layer, int index = -1);

    void execute(Document& document) override;
    void undo(Document& document) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    EntityId compositionId_;
    EntityId layerId_;
    int index_;
    std::unique_ptr<Layer> layer_;
};

}  // namespace motion
