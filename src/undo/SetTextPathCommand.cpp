#include "MotionStudio/undo/SetTextPathCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextPathCommand::SetTextPathCommand(EntityId layerId, bool enabled, EntityId pathLayerId,
                                       bool reversed, bool perpendicular, bool forceAlignment)
    : layerId_(layerId)
    , enabled_(enabled)
    , pathLayerId_(pathLayerId)
    , reversed_(reversed)
    , perpendicular_(perpendicular)
    , forceAlignment_(forceAlignment) {
}

void SetTextPathCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->content->type() != LayerType::Text) {
        return;
    }
    auto *textContent = static_cast<TextContent *>(layer->content.get());
    if (!oldEnabled_) {
        oldEnabled_ = textContent->textPath.enabled;
        oldPathLayerId_ = textContent->textPath.pathLayerId;
        oldReversed_ = textContent->textPath.reversed;
        oldPerpendicular_ = textContent->textPath.perpendicular;
        oldForceAlignment_ = textContent->textPath.forceAlignment;
    }
    if (!enabled_ || pathLayerId_ == layerId_) {
        textContent->textPath.enabled = false;
        textContent->textPath.pathLayerId = EntityId{};
        textContent->textPath.reversed = reversed_;
        textContent->textPath.perpendicular = perpendicular_;
        textContent->textPath.forceAlignment = forceAlignment_;
        return;
    }
    if (pathLayerId_.isValid() && document.entityIndex().findLayer(pathLayerId_) == nullptr) {
        textContent->textPath.enabled = false;
        textContent->textPath.pathLayerId = EntityId{};
        textContent->textPath.reversed = reversed_;
        textContent->textPath.perpendicular = perpendicular_;
        textContent->textPath.forceAlignment = forceAlignment_;
        return;
    }
    textContent->textPath.enabled = enabled_;
    textContent->textPath.pathLayerId = pathLayerId_;
    textContent->textPath.reversed = reversed_;
    textContent->textPath.perpendicular = perpendicular_;
    textContent->textPath.forceAlignment = forceAlignment_;
}

void SetTextPathCommand::undo(Document &document) {
    if (!oldEnabled_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->content->type() != LayerType::Text) {
        return;
    }
    auto *textContent = static_cast<TextContent *>(layer->content.get());
    textContent->textPath.enabled = *oldEnabled_;
    textContent->textPath.pathLayerId = *oldPathLayerId_;
    textContent->textPath.reversed = *oldReversed_;
    textContent->textPath.perpendicular = *oldPerpendicular_;
    textContent->textPath.forceAlignment = *oldForceAlignment_;
}

bool SetTextPathCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextPath) {
        return false;
    }
    const auto &typed = static_cast<const SetTextPathCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    enabled_ = typed.enabled_;
    pathLayerId_ = typed.pathLayerId_;
    reversed_ = typed.reversed_;
    perpendicular_ = typed.perpendicular_;
    forceAlignment_ = typed.forceAlignment_;
    return true;
}

CommandKind SetTextPathCommand::kind() const {
    return CommandKind::SetTextPath;
}

std::string SetTextPathCommand::describe() const {
    return "Set Text Path";
}

}  // namespace motion
