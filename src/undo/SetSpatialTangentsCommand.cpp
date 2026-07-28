#include "MotionStudio/undo/SetSpatialTangentsCommand.h"

#include <utility>

#include "CommandHelpers.h"
#include "MotionStudio/model/Document.h"

namespace motion {

SetSpatialTangentsCommand::SetSpatialTangentsCommand(PropertyPath property, FrameTime time,
                                                     std::optional<Vec2> spatialIn,
                                                     std::optional<Vec2> spatialOut)
    : property_(std::move(property))
    , time_(time)
    , spatialIn_(std::move(spatialIn))
    , spatialOut_(std::move(spatialOut)) {
}

void SetSpatialTangentsCommand::execute(Document &document) {
    AnimatableBase *target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        std::optional<Vec2> oldIn;
        std::optional<Vec2> oldOut;
        found_ = ApplySpatialTangentsVec2(target, time_, spatialIn_, spatialOut_, &oldIn, &oldOut);
        if (found_) {
            oldSpatialIn_ = oldIn;
            oldSpatialOut_ = oldOut;
        }
        captured_ = true;
    } else if (found_) {
        ApplySpatialTangentsVec2(target, time_, spatialIn_, spatialOut_, nullptr, nullptr);
    }
}

void SetSpatialTangentsCommand::undo(Document &document) {
    if (!captured_ || !found_) {
        return;
    }
    AnimatableBase *target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    ApplySpatialTangentsVec2(target, time_, oldSpatialIn_, oldSpatialOut_, nullptr, nullptr);
}

bool SetSpatialTangentsCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetSpatialTangents) {
        return false;
    }
    const auto &typed = static_cast<const SetSpatialTangentsCommand &>(other);
    if (typed.property_ != property_ || typed.time_ != time_) {
        return false;
    }
    spatialIn_ = typed.spatialIn_;
    spatialOut_ = typed.spatialOut_;
    return true;
}

CommandKind SetSpatialTangentsCommand::kind() const {
    return CommandKind::SetSpatialTangents;
}

std::string SetSpatialTangentsCommand::describe() const {
    return "Set Spatial Tangents";
}

}  // namespace motion
