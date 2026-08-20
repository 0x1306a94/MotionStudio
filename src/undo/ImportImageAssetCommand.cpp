#include "MotionStudio/undo/ImportImageAssetCommand.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
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

bool ReadFileContents(const std::filesystem::path &path, std::vector<unsigned char> &contents) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::ifstream::pos_type size = input.tellg();
    if (size < 0) {
        return false;
    }
    contents.resize(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!contents.empty()) {
        input.read(reinterpret_cast<char *>(contents.data()), static_cast<std::streamsize>(contents.size()));
    }
    return input.good() || input.eof();
}

bool WriteFileContents(const std::filesystem::path &path, const std::vector<unsigned char> &contents) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    if (!contents.empty()) {
        output.write(reinterpret_cast<const char *>(contents.data()), contents.size());
    }
    return output.good();
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
            removedFileContents_.clear();
            removedFileExisted_ = fileExists;
            if (fileExists) {
                if (!std::filesystem::is_regular_file(*assetFile, error) || error) {
                    removedFileExisted_ = false;
                    return;
                }
                if (!ReadFileContents(*assetFile, removedFileContents_)) {
                    removedFileExisted_ = false;
                    return;
                }
                if (!std::filesystem::remove(*assetFile, error) || error) {
                    removedFileContents_.clear();
                    removedFileExisted_ = false;
                    return;
                }
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
    if (removedFileExisted_) {
        const std::optional<std::filesystem::path> assetFile = AssetFilePath(document, *removedAsset_);
        if (!assetFile || !WriteFileContents(*assetFile, removedFileContents_)) {
            return;
        }
    }
    const size_t insertIndex = (index_ < 0 ? document.assets.size() : (static_cast<size_t>(index_) > document.assets.size() ? document.assets.size() : static_cast<size_t>(index_)));
    document.assets.insert(document.assets.begin() + static_cast<ptrdiff_t>(insertIndex), std::move(*removedAsset_));
    removedAsset_.reset();
    removedFileContents_.clear();
    removedFileExisted_ = false;
}

CommandKind RemoveAssetCommand::kind() const {
    return CommandKind::RemoveAsset;
}

std::string RemoveAssetCommand::describe() const {
    return "Remove Image Asset";
}

}  // namespace motion
