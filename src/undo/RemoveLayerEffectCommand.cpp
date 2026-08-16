#include "MotionStudio/undo/RemoveLayerEffectCommand.h"

#include <algorithm>
#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

RemoveLayerEffectCommand::RemoveLayerEffectCommand(EntityId layerId, int index)
    : layerId_(layerId)
    , index_(index) {
}

void RemoveLayerEffectCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (effectId_.isValid()) {
        for (size_t index = 0; index < layer->effects.size(); ++index) {
            if (layer->effects[index]->id == effectId_) {
                index_ = static_cast<int>(index);
                effect_ = std::move(layer->effects[index]);
                layer->effects.erase(layer->effects.begin() + static_cast<ptrdiff_t>(index));
                return;
            }
        }
        return;
    }
    if (index_ < 0 || static_cast<size_t>(index_) >= layer->effects.size()) {
        return;
    }
    const size_t index = static_cast<size_t>(index_);
    effectId_ = layer->effects[index]->id;
    effect_ = std::move(layer->effects[index]);
    layer->effects.erase(layer->effects.begin() + static_cast<ptrdiff_t>(index));
}

void RemoveLayerEffectCommand::undo(Document &document) {
    if (!effect_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    const size_t index =
        std::min(static_cast<size_t>(std::max(index_, 0)), layer->effects.size());
    layer->effects.insert(layer->effects.begin() + static_cast<ptrdiff_t>(index),
                          std::move(effect_));
}

CommandKind RemoveLayerEffectCommand::kind() const {
    return CommandKind::RemoveLayerEffect;
}

std::string RemoveLayerEffectCommand::describe() const {
    return "Remove Effect";
}

}  // namespace motion
