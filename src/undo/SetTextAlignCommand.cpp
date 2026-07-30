#include "MotionStudio/undo/SetTextAlignCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextAlignCommand::SetTextAlignCommand(EntityId layerId, TextAlign align)
    : layerId_(layerId)
    , align_(align) {
}

void SetTextAlignCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldAlign_) {
        oldAlign_ = content->align;
    }
    content->align = align_;
}

void SetTextAlignCommand::undo(Document &document) {
    if (!oldAlign_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    static_cast<TextContent *>(layer->content.get())->align = *oldAlign_;
}

bool SetTextAlignCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextAlign) {
        return false;
    }
    const auto &typed = static_cast<const SetTextAlignCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    align_ = typed.align_;
    return true;
}

CommandKind SetTextAlignCommand::kind() const {
    return CommandKind::SetTextAlign;
}

std::string SetTextAlignCommand::describe() const {
    return "Set Text Align";
}

}  // namespace motion
