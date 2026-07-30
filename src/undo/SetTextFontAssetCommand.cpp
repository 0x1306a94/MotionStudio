#include "MotionStudio/undo/SetTextFontAssetCommand.h"

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextFontAssetCommand::SetTextFontAssetCommand(EntityId layerId, EntityId assetId)
    : layerId_(layerId)
    , assetId_(assetId) {
}

void SetTextFontAssetCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldAssetId_) {
        oldAssetId_ = content->fontAssetId;
        oldFontFamily_ = content->fontFamily;
    }
    if (assetId_.isValid()) {
        const Asset *found = nullptr;
        for (const Asset &asset : document.assets) {
            if (asset.id == assetId_ && asset.type == AssetType::Font) {
                found = &asset;
                break;
            }
        }
        if (found == nullptr) {
            content->fontAssetId = EntityId{};
            return;
        }
        content->fontAssetId = assetId_;
        content->fontFamily = found->name;
        return;
    }
    content->fontAssetId = EntityId{};
}

void SetTextFontAssetCommand::undo(Document &document) {
    if (!oldAssetId_ || !oldFontFamily_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->fontAssetId = *oldAssetId_;
    content->fontFamily = *oldFontFamily_;
}

bool SetTextFontAssetCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextFontAsset) {
        return false;
    }
    const auto &typed = static_cast<const SetTextFontAssetCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    assetId_ = typed.assetId_;
    return true;
}

CommandKind SetTextFontAssetCommand::kind() const {
    return CommandKind::SetTextFontAsset;
}

std::string SetTextFontAssetCommand::describe() const {
    return "Set Text Font Asset";
}

}  // namespace motion
