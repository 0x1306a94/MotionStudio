#include "MotionStudio/undo/MoveMaskCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

namespace {

void MoveMaskInLayer(Layer &layer, int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex) {
        return;
    }
    if (static_cast<size_t>(fromIndex) >= layer.masks.size() ||
        static_cast<size_t>(toIndex) >= layer.masks.size()) {
        return;
    }
    Mask mask = std::move(layer.masks[static_cast<size_t>(fromIndex)]);
    layer.masks.erase(layer.masks.begin() + static_cast<ptrdiff_t>(fromIndex));
    layer.masks.insert(layer.masks.begin() + static_cast<ptrdiff_t>(toIndex), std::move(mask));
}

}  // namespace

MoveMaskCommand::MoveMaskCommand(EntityId layerId, int fromIndex, int toIndex)
    : layerId_(layerId)
    , fromIndex_(fromIndex)
    , toIndex_(toIndex) {
}

void MoveMaskCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveMaskInLayer(*layer, fromIndex_, toIndex_);
}

void MoveMaskCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveMaskInLayer(*layer, toIndex_, fromIndex_);
}

bool MoveMaskCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::MoveMask) {
        return false;
    }
    const auto &typed = static_cast<const MoveMaskCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.fromIndex_ != toIndex_) {
        return false;
    }
    toIndex_ = typed.toIndex_;
    return true;
}

CommandKind MoveMaskCommand::kind() const {
    return CommandKind::MoveMask;
}

std::string MoveMaskCommand::describe() const {
    return "Move Mask";
}

}  // namespace motion
