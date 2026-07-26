#include "MotionStudio/undo/RemoveMaskCommand.h"

#include <algorithm>
#include <utility>

#include "MotionStudio/model/Document.h"

namespace motion {

RemoveMaskCommand::RemoveMaskCommand(EntityId layerId, int index)
    : layerId_(layerId)
    , index_(index) {
}

void RemoveMaskCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (index_ < 0 || static_cast<size_t>(index_) >= layer->masks.size()) {
        return;
    }
    mask_ = std::move(layer->masks[static_cast<size_t>(index_)]);
    layer->masks.erase(layer->masks.begin() + static_cast<ptrdiff_t>(index_));
}

void RemoveMaskCommand::undo(Document &document) {
    if (!mask_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    const size_t index =
        std::min(static_cast<size_t>(std::max(index_, 0)), layer->masks.size());
    layer->masks.insert(layer->masks.begin() + static_cast<ptrdiff_t>(index),
                        std::move(*mask_));
    mask_.reset();
}

CommandKind RemoveMaskCommand::kind() const {
    return CommandKind::RemoveMask;
}

std::string RemoveMaskCommand::describe() const {
    return "Remove Mask";
}

}  // namespace motion
