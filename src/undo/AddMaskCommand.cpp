#include "MotionStudio/undo/AddMaskCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"

namespace motion {

AddMaskCommand::AddMaskCommand(EntityId layerId, Mask mask)
    : layerId_(layerId)
    , mask_(std::move(mask)) {
}

void AddMaskCommand::execute(Document &document) {
    if (!mask_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    layer->masks.push_back(std::move(*mask_));
    index_ = static_cast<int>(layer->masks.size()) - 1;
    mask_.reset();
}

void AddMaskCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->masks.size()) {
        return;
    }
    mask_ = std::move(layer->masks[static_cast<size_t>(index_)]);
    layer->masks.erase(layer->masks.begin() + static_cast<ptrdiff_t>(index_));
}

CommandKind AddMaskCommand::kind() const {
    return CommandKind::AddMask;
}

std::string AddMaskCommand::describe() const {
    return "Add Mask";
}

}  // namespace motion
