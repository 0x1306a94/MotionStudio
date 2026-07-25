#include "MotionStudio/undo/AddFillStyleCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

AddFillStyleCommand::AddFillStyleCommand(EntityId layerId)
    : layerId_(layerId)
    , style_(std::make_unique<FillStyle>()) {
    styleId_ = style_->id;
}

void AddFillStyleCommand::execute(Document &document) {
    if (!style_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    layer->styles.push_back(std::move(style_));
}

void AddFillStyleCommand::undo(Document &document) {
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

CommandKind AddFillStyleCommand::kind() const {
    return CommandKind::AddFillStyle;
}

std::string AddFillStyleCommand::describe() const {
    return "Add Fill";
}

}  // namespace motion
