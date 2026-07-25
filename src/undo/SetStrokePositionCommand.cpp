#include "MotionStudio/undo/SetStrokePositionCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"

namespace motion {

namespace {

StrokeStyle *FindStroke(Document &document, EntityId layerId, int index) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->styles.size()) {
        return nullptr;
    }
    LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    if (style->type() != LayerStyleType::Stroke) {
        return nullptr;
    }
    return static_cast<StrokeStyle *>(style);
}

}  // namespace

SetStrokePositionCommand::SetStrokePositionCommand(EntityId layerId, int index,
                                                   StrokePosition position)
    : layerId_(layerId)
    , index_(index)
    , position_(position) {
}

void SetStrokePositionCommand::execute(Document &document) {
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke == nullptr) {
        return;
    }
    if (!oldPosition_) {
        oldPosition_ = stroke->position;
    }
    stroke->position = position_;
}

void SetStrokePositionCommand::undo(Document &document) {
    if (!oldPosition_) {
        return;
    }
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke != nullptr) {
        stroke->position = *oldPosition_;
    }
}

bool SetStrokePositionCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStrokePosition) {
        return false;
    }
    const auto &typed = static_cast<const SetStrokePositionCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    position_ = typed.position_;  // Preserve oldPosition_, absorb final value.
    return true;
}

CommandKind SetStrokePositionCommand::kind() const {
    return CommandKind::SetStrokePosition;
}

std::string SetStrokePositionCommand::describe() const {
    return "Set Stroke Position";
}

}  // namespace motion
