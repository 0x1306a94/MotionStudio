#include "MotionStudio/undo/SetTrackMatteCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetTrackMatteCommand::SetTrackMatteCommand(EntityId layerId, EntityId matteLayerId,
                                           TrackMatteType type)
    : layerId_(layerId)
    , matteLayerId_(matteLayerId)
    , type_(type) {
}

void SetTrackMatteCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (!oldType_) {
        oldMatteLayerId_ = layer->trackMatteLayerId;
        oldType_ = layer->trackMatteType;
    }
    if (type_ == TrackMatteType::None || matteLayerId_ == layerId_) {
        layer->trackMatteLayerId = EntityId{};
        layer->trackMatteType = TrackMatteType::None;
        return;
    }
    if (matteLayerId_.isValid() && document.entityIndex().findLayer(matteLayerId_) == nullptr) {
        layer->trackMatteLayerId = EntityId{};
        layer->trackMatteType = TrackMatteType::None;
        return;
    }
    layer->trackMatteLayerId = matteLayerId_;
    layer->trackMatteType = type_;
}

void SetTrackMatteCommand::undo(Document &document) {
    if (!oldType_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    layer->trackMatteLayerId = *oldMatteLayerId_;
    layer->trackMatteType = *oldType_;
}

bool SetTrackMatteCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTrackMatte) {
        return false;
    }
    const auto &typed = static_cast<const SetTrackMatteCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    matteLayerId_ = typed.matteLayerId_;
    type_ = typed.type_;
    return true;
}

CommandKind SetTrackMatteCommand::kind() const {
    return CommandKind::SetTrackMatte;
}

std::string SetTrackMatteCommand::describe() const {
    return "Set Track Matte";
}

}  // namespace motion
