#include "MotionStudio/undo/RemoveStyleCommand.h"

#include <algorithm>
#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

RemoveStyleCommand::RemoveStyleCommand(EntityId layerId, int index)
    : layerId_(layerId)
    , index_(index) {
}

void RemoveStyleCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    // On redo the style is already known; locate it by id in case earlier
    // removals shifted indices.
    if (styleId_.isValid()) {
        for (size_t index = 0; index < layer->styles.size(); ++index) {
            if (layer->styles[index]->id == styleId_) {
                index_ = static_cast<int>(index);
                style_ = std::move(layer->styles[index]);
                layer->styles.erase(layer->styles.begin() + static_cast<ptrdiff_t>(index));
                return;
            }
        }
        return;
    }
    if (index_ < 0 || static_cast<size_t>(index_) >= layer->styles.size()) {
        return;
    }
    const size_t index = static_cast<size_t>(index_);
    styleId_ = layer->styles[index]->id;
    style_ = std::move(layer->styles[index]);
    layer->styles.erase(layer->styles.begin() + static_cast<ptrdiff_t>(index));
}

void RemoveStyleCommand::undo(Document &document) {
    if (!style_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    const size_t index = std::min(static_cast<size_t>(std::max(index_, 0)),
                                  layer->styles.size());
    layer->styles.insert(layer->styles.begin() + static_cast<ptrdiff_t>(index),
                         std::move(style_));
}

CommandKind RemoveStyleCommand::kind() const {
    return CommandKind::RemoveStyle;
}

std::string RemoveStyleCommand::describe() const {
    return "Remove Style";
}

}  // namespace motion
