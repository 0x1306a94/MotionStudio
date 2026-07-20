#include "MotionStudio/undo/RemoveLayerCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

RemoveLayerCommand::RemoveLayerCommand(EntityId compositionId, EntityId layerId)
    : compositionId_(compositionId), layerId_(layerId) {}

void RemoveLayerCommand::execute(Document& document) {
    Composition* composition = document.entityIndex().findComposition(compositionId_);
    if (!composition) {
        return;
    }
    index_ = indexOfLayer(*composition, layerId_);
    if (index_ < 0) {
        return;  // 图层已删除 → 跳过
    }
    layer_ = document.takeLayer(compositionId_, layerId_);
}

void RemoveLayerCommand::undo(Document& document) {
    if (!layer_) {
        return;
    }
    document.addLayer(compositionId_, std::move(layer_), index_);
}

CommandKind RemoveLayerCommand::kind() const {
    return CommandKind::RemoveLayer;
}

std::string RemoveLayerCommand::describe() const {
    return "Remove Layer";
}

}  // namespace motion
