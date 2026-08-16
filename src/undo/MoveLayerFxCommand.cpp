#include "MotionStudio/undo/MoveLayerFxCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

namespace {

void MoveLayerFxInLayer(Layer &layer, int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex) {
        return;
    }
    if (static_cast<size_t>(fromIndex) >= layer.layerStyles.size() ||
        static_cast<size_t>(toIndex) >= layer.layerStyles.size()) {
        return;
    }
    std::unique_ptr<LayerFx> style = std::move(layer.layerStyles[static_cast<size_t>(fromIndex)]);
    layer.layerStyles.erase(layer.layerStyles.begin() + static_cast<ptrdiff_t>(fromIndex));
    layer.layerStyles.insert(layer.layerStyles.begin() + static_cast<ptrdiff_t>(toIndex),
                             std::move(style));
}

}  // namespace

MoveLayerFxCommand::MoveLayerFxCommand(EntityId layerId, int fromIndex, int toIndex)
    : layerId_(layerId)
    , fromIndex_(fromIndex)
    , toIndex_(toIndex) {
}

void MoveLayerFxCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveLayerFxInLayer(*layer, fromIndex_, toIndex_);
}

void MoveLayerFxCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveLayerFxInLayer(*layer, toIndex_, fromIndex_);
}

bool MoveLayerFxCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::MoveLayerFx) {
        return false;
    }
    const auto &typed = static_cast<const MoveLayerFxCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.fromIndex_ != toIndex_) {
        return false;
    }
    toIndex_ = typed.toIndex_;
    return true;
}

CommandKind MoveLayerFxCommand::kind() const {
    return CommandKind::MoveLayerFx;
}

std::string MoveLayerFxCommand::describe() const {
    return "Move Layer Style";
}

}  // namespace motion
