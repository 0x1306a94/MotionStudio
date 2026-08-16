#include "MotionStudio/undo/AddLayerEffectCommand.h"

#include <cstddef>
#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

AddLayerEffectCommand::AddLayerEffectCommand(EntityId layerId,
                                             std::unique_ptr<LayerEffect> effect)
    : layerId_(layerId)
    , effect_(std::move(effect)) {
    effectId_ = effect_->id;
}

void AddLayerEffectCommand::execute(Document &document) {
    if (!effect_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    layer->effects.push_back(std::move(effect_));
}

void AddLayerEffectCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    for (size_t index = 0; index < layer->effects.size(); ++index) {
        if (layer->effects[index]->id == effectId_) {
            effect_ = std::move(layer->effects[index]);
            layer->effects.erase(layer->effects.begin() + static_cast<ptrdiff_t>(index));
            return;
        }
    }
}

CommandKind AddLayerEffectCommand::kind() const {
    return CommandKind::AddLayerEffect;
}

std::string AddLayerEffectCommand::describe() const {
    return "Add Effect";
}

}  // namespace motion
