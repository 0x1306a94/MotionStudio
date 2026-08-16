#include "MotionStudio/undo/MoveLayerEffectCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

namespace {

void MoveLayerEffectInLayer(Layer &layer, int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex) {
        return;
    }
    if (static_cast<size_t>(fromIndex) >= layer.effects.size() ||
        static_cast<size_t>(toIndex) >= layer.effects.size()) {
        return;
    }
    std::unique_ptr<LayerEffect> effect =
        std::move(layer.effects[static_cast<size_t>(fromIndex)]);
    layer.effects.erase(layer.effects.begin() + static_cast<ptrdiff_t>(fromIndex));
    layer.effects.insert(layer.effects.begin() + static_cast<ptrdiff_t>(toIndex),
                         std::move(effect));
}

}  // namespace

MoveLayerEffectCommand::MoveLayerEffectCommand(EntityId layerId, int fromIndex, int toIndex)
    : layerId_(layerId)
    , fromIndex_(fromIndex)
    , toIndex_(toIndex) {
}

void MoveLayerEffectCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveLayerEffectInLayer(*layer, fromIndex_, toIndex_);
}

void MoveLayerEffectCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveLayerEffectInLayer(*layer, toIndex_, fromIndex_);
}

bool MoveLayerEffectCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::MoveLayerEffect) {
        return false;
    }
    const auto &typed = static_cast<const MoveLayerEffectCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.fromIndex_ != toIndex_) {
        return false;
    }
    toIndex_ = typed.toIndex_;
    return true;
}

CommandKind MoveLayerEffectCommand::kind() const {
    return CommandKind::MoveLayerEffect;
}

std::string MoveLayerEffectCommand::describe() const {
    return "Move Effect";
}

}  // namespace motion
