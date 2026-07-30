#include "MotionStudio/undo/SetImageScaleModeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetImageScaleModeCommand::SetImageScaleModeCommand(EntityId layerId, ImageScaleMode mode)
    : layerId_(layerId)
    , mode_(mode) {
}

void SetImageScaleModeCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return;
    }
    auto *content = static_cast<ImageContent *>(layer->content.get());
    if (!oldMode_) {
        oldMode_ = content->scaleMode;
    }
    content->scaleMode = mode_;
}

void SetImageScaleModeCommand::undo(Document &document) {
    if (!oldMode_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return;
    }
    static_cast<ImageContent *>(layer->content.get())->scaleMode = *oldMode_;
}

bool SetImageScaleModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetImageScaleMode) {
        return false;
    }
    const auto &typed = static_cast<const SetImageScaleModeCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    mode_ = typed.mode_;
    return true;
}

CommandKind SetImageScaleModeCommand::kind() const {
    return CommandKind::SetImageScaleMode;
}

std::string SetImageScaleModeCommand::describe() const {
    return "Set Image Scale Mode";
}

}  // namespace motion
