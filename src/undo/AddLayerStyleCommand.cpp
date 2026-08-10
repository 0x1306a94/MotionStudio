#include "MotionStudio/undo/AddLayerStyleCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

AddLayerStyleCommand::AddLayerStyleCommand(EntityId layerId,
                                           std::unique_ptr<LayerStyle> style)
    : layerId_(layerId)
    , style_(std::move(style)) {
    styleId_ = style_->id;
}

void AddLayerStyleCommand::execute(Document &document) {
    if (!style_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    // Keep disk order closer to paint order: fills before strokes.
    if (style_->type() == LayerStyleType::Fill) {
        size_t insertAt = layer->styles.size();
        for (size_t i = 0; i < layer->styles.size(); ++i) {
            if (layer->styles[i]->type() == LayerStyleType::Stroke) {
                insertAt = i;
                break;
            }
        }
        layer->styles.insert(layer->styles.begin() + static_cast<ptrdiff_t>(insertAt),
                             std::move(style_));
        return;
    }
    layer->styles.push_back(std::move(style_));
}

void AddLayerStyleCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    for (size_t index = 0; index < layer->styles.size(); ++index) {
        if (layer->styles[index]->id == styleId_) {
            style_ = std::move(layer->styles[index]);
            layer->styles.erase(layer->styles.begin() + static_cast<ptrdiff_t>(index));
            return;
        }
    }
}

CommandKind AddLayerStyleCommand::kind() const {
    return CommandKind::AddLayerStyle;
}

std::string AddLayerStyleCommand::describe() const {
    if (style_ && style_->type() == LayerStyleType::Stroke) {
        return "Add Stroke";
    }
    return "Add Fill";
}

}  // namespace motion
