#include "MotionStudio/undo/ImportImageAssetCommand.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/RemoveAssetCommand.h"

namespace motion {

namespace {

bool LayerReferencesAsset(const Layer &layer, EntityId assetId) {
    if (layer.type() != LayerType::Image) {
        return false;
    }
    const auto *content = static_cast<const ImageContent *>(layer.content.get());
    return content->assetId == assetId;
}

bool LayersReferenceAsset(const std::vector<std::unique_ptr<Layer>> &layers, EntityId assetId) {
    for (const auto &layer : layers) {
        if (!layer) {
            continue;
        }
        if (LayerReferencesAsset(*layer, assetId)) {
            return true;
        }
    }
    return false;
}

bool AssetIsReferenced(const Document &document, EntityId assetId) {
    for (const auto &composition : document.compositions) {
        if (!composition) {
            continue;
        }
        if (LayersReferenceAsset(composition->layers, assetId)) {
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> AssetFilePath(const Document &document, const Asset &asset) {
    if (document.projectRoot.empty() || asset.path.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path relativePath(asset.path);
    if (relativePath.is_absolute()) {
        return std::nullopt;
    }
    return std::filesystem::path(document.projectRoot) / relativePath;
}

std::optional<std::filesystem::path> UniqueTrashFilePath(const Document &document, EntityId assetId, const std::filesystem::path &sourcePath) {
    if (document.assetTrashRoot.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path trashRoot(document.assetTrashRoot);
    const std::string fileName = sourcePath.filename().empty() ? "resource" : sourcePath.filename().string();
    const std::string prefix = "asset_" + std::to_string(assetId.value) + "_";
    std::error_code error;
    for (int attempt = 0; attempt < 10000; ++attempt) {
        const std::filesystem::path candidate =
            trashRoot / (prefix + std::to_string(attempt) + "_" + fileName);
        if (!std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
        error.clear();
    }
    return std::nullopt;
}

}  // namespace

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

RemoveAssetCommand::RemoveAssetCommand(EntityId assetId)
    : assetId_(assetId) {
}

void RemoveAssetCommand::execute(Document &document) {
    removedAsset_.reset();
    removedFilePath_.clear();
    trashFilePath_.clear();
    movedFileToTrash_ = false;
    if (!assetId_.isValid()) {
        return;
    }
    if (AssetIsReferenced(document, assetId_)) {
        return;
    }
    for (size_t index = 0; index < document.assets.size(); ++index) {
        if (document.assets[index].id == assetId_) {
            const std::optional<std::filesystem::path> assetFile = AssetFilePath(document, document.assets[index]);
            if (!assetFile) {
                return;
            }
            std::error_code error;
            const bool fileExists = std::filesystem::exists(*assetFile, error);
            if (error) {
                return;
            }
            if (fileExists) {
                if (!std::filesystem::is_regular_file(*assetFile, error) || error) {
                    return;
                }
                const std::optional<std::filesystem::path> trashFile = UniqueTrashFilePath(document, assetId_, *assetFile);
                if (!trashFile) {
                    return;
                }
                std::filesystem::create_directories(trashFile->parent_path(), error);
                if (error) {
                    return;
                }
                std::filesystem::rename(*assetFile, *trashFile, error);
                if (error) {
                    return;
                }
                removedFilePath_ = assetFile->string();
                trashFilePath_ = trashFile->string();
                movedFileToTrash_ = true;
            }
            removedAsset_ = std::move(document.assets[index]);
            index_ = static_cast<int>(index);
            document.assets.erase(document.assets.begin() + static_cast<ptrdiff_t>(index));
            return;
        }
    }
}

void RemoveAssetCommand::undo(Document &document) {
    if (!removedAsset_) {
        return;
    }
    for (const Asset &asset : document.assets) {
        if (asset.id == assetId_) {
            return;
        }
    }
    if (movedFileToTrash_) {
        if (removedFilePath_.empty() || trashFilePath_.empty()) {
            return;
        }
        std::error_code error;
        const std::filesystem::path removedFilePath(removedFilePath_);
        std::filesystem::create_directories(removedFilePath.parent_path(), error);
        if (error) {
            return;
        }
        std::filesystem::rename(std::filesystem::path(trashFilePath_), removedFilePath, error);
        if (error) {
            return;
        }
    }
    const size_t insertIndex = (index_ < 0 ? document.assets.size() : (static_cast<size_t>(index_) > document.assets.size() ? document.assets.size() : static_cast<size_t>(index_)));
    document.assets.insert(document.assets.begin() + static_cast<ptrdiff_t>(insertIndex), std::move(*removedAsset_));
    removedAsset_.reset();
    removedFilePath_.clear();
    trashFilePath_.clear();
    movedFileToTrash_ = false;
}

CommandKind RemoveAssetCommand::kind() const {
    return CommandKind::RemoveAsset;
}

std::string RemoveAssetCommand::describe() const {
    return "Remove Image Asset";
}

}  // namespace motion
