#include "MotionStudio/undo/SetFollowPathCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetFollowPathCommand::SetFollowPathCommand(EntityId layerId, bool enabled, EntityId pathLayerId,
                                           bool orientAlongPath)
    : layerId_(layerId)
    , enabled_(enabled)
    , pathLayerId_(pathLayerId)
    , orientAlongPath_(orientAlongPath) {
}

void SetFollowPathCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (!oldEnabled_) {
        oldEnabled_ = layer->followPath.enabled;
        oldPathLayerId_ = layer->followPath.pathLayerId;
        oldOrientAlongPath_ = layer->followPath.orientAlongPath;
    }
    if (!enabled_ || pathLayerId_ == layerId_) {
        layer->followPath.enabled = false;
        layer->followPath.pathLayerId = EntityId{};
        layer->followPath.orientAlongPath = orientAlongPath_;
        return;
    }
    if (pathLayerId_.isValid() && document.entityIndex().findLayer(pathLayerId_) == nullptr) {
        layer->followPath.enabled = false;
        layer->followPath.pathLayerId = EntityId{};
        layer->followPath.orientAlongPath = orientAlongPath_;
        return;
    }
    layer->followPath.enabled = enabled_;
    layer->followPath.pathLayerId = pathLayerId_;
    layer->followPath.orientAlongPath = orientAlongPath_;
}

void SetFollowPathCommand::undo(Document &document) {
    if (!oldEnabled_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    layer->followPath.enabled = *oldEnabled_;
    layer->followPath.pathLayerId = *oldPathLayerId_;
    layer->followPath.orientAlongPath = *oldOrientAlongPath_;
}

bool SetFollowPathCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetFollowPath) {
        return false;
    }
    const auto &typed = static_cast<const SetFollowPathCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    enabled_ = typed.enabled_;
    pathLayerId_ = typed.pathLayerId_;
    orientAlongPath_ = typed.orientAlongPath_;
    return true;
}

CommandKind SetFollowPathCommand::kind() const {
    return CommandKind::SetFollowPath;
}

std::string SetFollowPathCommand::describe() const {
    return "Set Follow Path";
}

}  // namespace motion
