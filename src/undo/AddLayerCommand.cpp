#include "MotionStudio/undo/AddLayerCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

AddLayerCommand::AddLayerCommand(EntityId compositionId, std::unique_ptr<Layer> layer,
                                 int index)
    : compositionId_(compositionId), layerId_(layer ? layer->id : EntityId{}),
      index_(index), layer_(std::move(layer)) {}

void AddLayerCommand::execute(Document& document) {
    if (!layer_) {
        return;
    }
    Composition* composition = document.entityIndex().findComposition(compositionId_);
    if (!composition) {
        return;
    }
    document.addLayer(compositionId_, std::move(layer_), index_);
    index_ = IndexOfLayer(*composition, layerId_);
}

void AddLayerCommand::undo(Document& document) {
    layer_ = document.takeLayer(compositionId_, layerId_);
}

CommandKind AddLayerCommand::kind() const {
    return CommandKind::AddLayer;
}

std::string AddLayerCommand::describe() const {
    return "Add Layer";
}

}  // namespace motion
