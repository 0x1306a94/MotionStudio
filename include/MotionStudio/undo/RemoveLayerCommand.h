#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// 删图层。execute 时接管 unique_ptr 所有权，undo 时原下标移回，
// 完整恢复子结构与关键帧。
class RemoveLayerCommand : public Command {
public:
    RemoveLayerCommand(EntityId compositionId, EntityId layerId);

    void execute(Document& document) override;
    void undo(Document& document) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    EntityId compositionId_;
    EntityId layerId_;
    int index_ = -1;
    std::unique_ptr<Layer> layer_;
};

}  // namespace motion
