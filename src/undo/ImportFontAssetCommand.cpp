#include "MotionStudio/undo/ImportFontAssetCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"

namespace motion {

ImportFontAssetCommand::ImportFontAssetCommand(Asset asset)
    : asset_(std::move(asset)) {
}

void ImportFontAssetCommand::execute(Document &document) {
    for (const Asset &existing : document.assets) {
        if (existing.id == asset_.id) {
            return;
        }
    }
    document.assets.push_back(asset_);
    inserted_ = true;
}

void ImportFontAssetCommand::undo(Document &document) {
    if (!inserted_) {
        return;
    }
    for (auto it = document.assets.begin(); it != document.assets.end(); ++it) {
        if (it->id == asset_.id) {
            document.assets.erase(it);
            break;
        }
    }
}

CommandKind ImportFontAssetCommand::kind() const {
    return CommandKind::ImportFontAsset;
}

std::string ImportFontAssetCommand::describe() const {
    return "Import Font Asset";
}

}  // namespace motion
