#include "MotionStudio/undo/SetLayerFxStrokePositionCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerFx.h"

namespace motion {

namespace {

LayerStrokeStyle *FindLayerStrokeStyle(Document &document, EntityId layerId, int index) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->layerStyles.size()) {
        return nullptr;
    }
    LayerFx *style = layer->layerStyles[static_cast<size_t>(index)].get();
    if (style->type() != LayerFxType::Stroke) {
        return nullptr;
    }
    return static_cast<LayerStrokeStyle *>(style);
}

}  // namespace

SetLayerFxStrokePositionCommand::SetLayerFxStrokePositionCommand(EntityId layerId, int index,
                                                                 StrokePosition position)
    : layerId_(layerId)
    , index_(index)
    , position_(position) {
}

void SetLayerFxStrokePositionCommand::execute(Document &document) {
    LayerStrokeStyle *stroke = FindLayerStrokeStyle(document, layerId_, index_);
    if (stroke == nullptr) {
        return;
    }
    if (!oldPosition_) {
        oldPosition_ = stroke->position;
    }
    stroke->position = position_;
}

void SetLayerFxStrokePositionCommand::undo(Document &document) {
    if (!oldPosition_) {
        return;
    }
    LayerStrokeStyle *stroke = FindLayerStrokeStyle(document, layerId_, index_);
    if (stroke != nullptr) {
        stroke->position = *oldPosition_;
    }
}

bool SetLayerFxStrokePositionCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerFxStrokePosition) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerFxStrokePositionCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    position_ = typed.position_;
    return true;
}

CommandKind SetLayerFxStrokePositionCommand::kind() const {
    return CommandKind::SetLayerFxStrokePosition;
}

std::string SetLayerFxStrokePositionCommand::describe() const {
    return "Set Layer Style Position";
}

}  // namespace motion
