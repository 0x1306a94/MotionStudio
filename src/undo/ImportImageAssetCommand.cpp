#include "MotionStudio/undo/ImportImageAssetCommand.h"

#include "MotionStudio/model/Document.h"

namespace motion {

ImportImageAssetCommand::ImportImageAssetCommand(Asset asset)
    : asset_(std::move(asset)) {
}

void ImportImageAssetCommand::execute(Document &document) {
    for (const Asset &existing : document.assets) {
        if (existing.id == asset_.id) {
            return;
        }
    }
    document.assets.push_back(asset_);
    inserted_ = true;
}

void ImportImageAssetCommand::undo(Document &document) {
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

CommandKind ImportImageAssetCommand::kind() const {
    return CommandKind::ImportImageAsset;
}

std::string ImportImageAssetCommand::describe() const {
    return "Import Image Asset";
}

}  // namespace motion
