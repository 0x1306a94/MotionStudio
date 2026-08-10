#include "MotionStudio/undo/SetLayerNameCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetLayerNameCommand::SetLayerNameCommand(EntityId layerId, std::string name)
    : layerId_(layerId)
    , name_(std::move(name)) {
}

void SetLayerNameCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (!oldName_) {
        oldName_ = layer->name;
    }
    layer->name = name_;
}

void SetLayerNameCommand::undo(Document &document) {
    if (!oldName_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer != nullptr) {
        layer->name = *oldName_;
    }
}

bool SetLayerNameCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerName) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerNameCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    name_ = typed.name_;
    return true;
}

CommandKind SetLayerNameCommand::kind() const {
    return CommandKind::SetLayerName;
}

std::string SetLayerNameCommand::describe() const {
    return "Rename Layer";
}

}  // namespace motion
