#include "MotionStudio/undo/RemoveLayerFxCommand.h"

#include <algorithm>
#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

RemoveLayerFxCommand::RemoveLayerFxCommand(EntityId layerId, int index)
    : layerId_(layerId)
    , index_(index) {
}

void RemoveLayerFxCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (styleId_.isValid()) {
        for (size_t index = 0; index < layer->layerStyles.size(); ++index) {
            if (layer->layerStyles[index]->id == styleId_) {
                index_ = static_cast<int>(index);
                style_ = std::move(layer->layerStyles[index]);
                layer->layerStyles.erase(layer->layerStyles.begin() + static_cast<ptrdiff_t>(index));
                return;
            }
        }
        return;
    }
    if (index_ < 0 || static_cast<size_t>(index_) >= layer->layerStyles.size()) {
        return;
    }
    const size_t index = static_cast<size_t>(index_);
    styleId_ = layer->layerStyles[index]->id;
    style_ = std::move(layer->layerStyles[index]);
    layer->layerStyles.erase(layer->layerStyles.begin() + static_cast<ptrdiff_t>(index));
}

void RemoveLayerFxCommand::undo(Document &document) {
    if (!style_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    const size_t index =
        std::min(static_cast<size_t>(std::max(index_, 0)), layer->layerStyles.size());
    layer->layerStyles.insert(layer->layerStyles.begin() + static_cast<ptrdiff_t>(index),
                              std::move(style_));
}

CommandKind RemoveLayerFxCommand::kind() const {
    return CommandKind::RemoveLayerFx;
}

std::string RemoveLayerFxCommand::describe() const {
    return "Remove Layer Style";
}

}  // namespace motion
