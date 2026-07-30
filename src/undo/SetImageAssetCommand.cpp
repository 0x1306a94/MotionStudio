#include "MotionStudio/undo/SetImageAssetCommand.h"

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetImageAssetCommand::SetImageAssetCommand(EntityId layerId, EntityId assetId)
    : layerId_(layerId)
    , assetId_(assetId) {
}

void SetImageAssetCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return;
    }
    auto *content = static_cast<ImageContent *>(layer->content.get());
    if (!oldAssetId_) {
        oldAssetId_ = content->assetId;
    }
    if (assetId_.isValid()) {
        bool found = false;
        for (const Asset &asset : document.assets) {
            if (asset.id == assetId_ && asset.type == AssetType::Image) {
                found = true;
                break;
            }
        }
        if (!found) {
            content->assetId = EntityId{};
            return;
        }
    }
    content->assetId = assetId_;
}

void SetImageAssetCommand::undo(Document &document) {
    if (!oldAssetId_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return;
    }
    static_cast<ImageContent *>(layer->content.get())->assetId = *oldAssetId_;
}

bool SetImageAssetCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetImageAsset) {
        return false;
    }
    const auto &typed = static_cast<const SetImageAssetCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    assetId_ = typed.assetId_;
    return true;
}

CommandKind SetImageAssetCommand::kind() const {
    return CommandKind::SetImageAsset;
}

std::string SetImageAssetCommand::describe() const {
    return "Set Image Asset";
}

}  // namespace motion
