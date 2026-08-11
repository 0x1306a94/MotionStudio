#include "MotionStudio/undo/MoveLayerStyleCommand.h"

#include <algorithm>
#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"

namespace motion {

namespace {

bool CanMoveLayerStyle(const Layer &layer, int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex) {
        return false;
    }
    const auto &styles = layer.styles;
    if (static_cast<size_t>(fromIndex) >= styles.size() ||
        static_cast<size_t>(toIndex) >= styles.size()) {
        return false;
    }
    const LayerStyleType type = styles[static_cast<size_t>(fromIndex)]->type();
    if (styles[static_cast<size_t>(toIndex)]->type() != type) {
        return false;
    }
    const int lo = std::min(fromIndex, toIndex);
    const int hi = std::max(fromIndex, toIndex);
    for (int i = lo; i <= hi; ++i) {
        if (styles[static_cast<size_t>(i)]->type() != type) {
            return false;
        }
    }
    return true;
}

void MoveLayerStyleInLayer(Layer &layer, int fromIndex, int toIndex) {
    if (!CanMoveLayerStyle(layer, fromIndex, toIndex)) {
        return;
    }
    std::unique_ptr<LayerStyle> style =
        std::move(layer.styles[static_cast<size_t>(fromIndex)]);
    layer.styles.erase(layer.styles.begin() + static_cast<ptrdiff_t>(fromIndex));
    layer.styles.insert(layer.styles.begin() + static_cast<ptrdiff_t>(toIndex), std::move(style));
}

}  // namespace

MoveLayerStyleCommand::MoveLayerStyleCommand(EntityId layerId, int fromIndex, int toIndex)
    : layerId_(layerId)
    , fromIndex_(fromIndex)
    , toIndex_(toIndex) {
}

void MoveLayerStyleCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveLayerStyleInLayer(*layer, fromIndex_, toIndex_);
}

void MoveLayerStyleCommand::undo(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    MoveLayerStyleInLayer(*layer, toIndex_, fromIndex_);
}

bool MoveLayerStyleCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::MoveLayerStyle) {
        return false;
    }
    const auto &typed = static_cast<const MoveLayerStyleCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.fromIndex_ != toIndex_) {
        return false;
    }
    toIndex_ = typed.toIndex_;
    return true;
}

CommandKind MoveLayerStyleCommand::kind() const {
    return CommandKind::MoveLayerStyle;
}

std::string MoveLayerStyleCommand::describe() const {
    return "Move Style";
}

}  // namespace motion
