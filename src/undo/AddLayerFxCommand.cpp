#include "MotionStudio/undo/AddLayerFxCommand.h"

#include <cstddef>
#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

AddLayerFxCommand::AddLayerFxCommand(EntityId layerId, std::unique_ptr<LayerFx> style)
    : layerId_(layerId)
    , style_(std::move(style)) {
    styleId_ = style_->id;
}

void AddLayerFxCommand::execute(Document &document) {
    if (!style_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    layer->layerStyles.push_back(std::move(style_));
}

void AddLayerFxCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    for (size_t index = 0; index < layer->layerStyles.size(); ++index) {
        if (layer->layerStyles[index]->id == styleId_) {
            style_ = std::move(layer->layerStyles[index]);
            layer->layerStyles.erase(layer->layerStyles.begin() + static_cast<ptrdiff_t>(index));
            return;
        }
    }
}

CommandKind AddLayerFxCommand::kind() const {
    return CommandKind::AddLayerFx;
}

std::string AddLayerFxCommand::describe() const {
    return "Add Layer Style";
}

}  // namespace motion
