#include "motionstudio_bridge.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/ImportImageAssetCommand.h"
#include "MotionStudio/undo/SetImageAssetCommand.h"
#include "MotionStudio/undo/SetImageScaleModeCommand.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Asset;
using motion::AssetType;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::ImageContent;
using motion::ImageScaleMode;
using motion::Layer;
using motion::LayerType;
using motion::Vec2;

namespace {

Asset *FindAsset(Document &document, uint64_t assetId) {
    for (Asset &asset : document.assets) {
        if (asset.id.value == assetId) {
            return &asset;
        }
    }
    return nullptr;
}

std::string UniqueAssetFileName(const std::filesystem::path &assetsDir,
                                const std::string &preferredName) {
    std::filesystem::path preferred(preferredName.empty() ? "image.png" : preferredName);
    std::string stem = preferred.stem().string();
    std::string extension = preferred.extension().string();
    if (extension.empty()) {
        extension = ".png";
    }
    std::filesystem::path candidate = assetsDir / (stem + extension);
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = assetsDir / (stem + "_" + std::to_string(suffix) + extension);
        ++suffix;
    }
    return candidate.filename().string();
}

bool CopyFile(const std::filesystem::path &source, const std::filesystem::path &destination) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing,
                               error);
    return !error;
}

}  // namespace

uint64_t ms_command_import_image_asset(MSDocument *document, const char *sourceAbsolutePath,
                                       const char *preferredFileName, int width, int height) {
    DocumentLock lock(document);
    if (document == nullptr || sourceAbsolutePath == nullptr || width <= 0 || height <= 0) {
        return 0;
    }
    if (document->document->projectRoot.empty()) {
        return 0;
    }
    const std::filesystem::path root(document->document->projectRoot);
    const std::filesystem::path assetsDir = root / "assets";
    const std::string fileName =
        UniqueAssetFileName(assetsDir, preferredFileName == nullptr ? "" : preferredFileName);
    const std::filesystem::path destination = assetsDir / fileName;
    if (!CopyFile(sourceAbsolutePath, destination)) {
        return 0;
    }

    Asset asset;
    asset.type = AssetType::Image;
    asset.name = fileName;
    asset.path = (std::filesystem::path("assets") / fileName).generic_string();
    asset.width = width;
    asset.height = height;
    const uint64_t assetId = asset.id.value;
    Execute(document, std::make_unique<motion::ImportImageAssetCommand>(std::move(asset)));
    return assetId;
}

uint64_t ms_command_add_image_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Image);
    layer->name = "Image " + std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.anchorPoint.setStaticValue(Vec2{100.0f, 100.0f});
    layer->transform.position.setStaticValue(
        Vec2{composition->width * 0.5f, composition->height * 0.5f});
    auto *content = static_cast<ImageContent *>(layer->content.get());
    content->size.setStaticValue(Vec2{200.0f, 200.0f});
    content->scaleMode = ImageScaleMode::LetterBox;
    const uint64_t layerId = layer->id.value;
    Execute(document, std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

bool ms_layer_set_image_asset(MSDocument *document, uint64_t layerId, uint64_t assetId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return false;
    }
    Execute(document, std::make_unique<motion::SetImageAssetCommand>(EntityId{layerId}, EntityId{assetId}));
    return true;
}

uint64_t ms_layer_image_asset_id(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return 0;
    }
    return static_cast<ImageContent *>(layer->content.get())->assetId.value;
}

void ms_layer_set_image_scale_mode(MSDocument *document, uint64_t layerId, MS_IMAGE_SCALE mode) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return;
    }
    if (mode < MS_IMAGE_SCALE_NONE || mode > MS_IMAGE_SCALE_ZOOM) {
        return;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return;
    }
    Execute(document, std::make_unique<motion::SetImageScaleModeCommand>(EntityId{layerId}, static_cast<ImageScaleMode>(mode)));
}

MS_IMAGE_SCALE ms_layer_image_scale_mode(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return MS_IMAGE_SCALE_LETTER_BOX;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || layer->type() != LayerType::Image) {
        return MS_IMAGE_SCALE_LETTER_BOX;
    }
    return static_cast<MS_IMAGE_SCALE>(
        static_cast<ImageContent *>(layer->content.get())->scaleMode);
}

int ms_document_asset_count(MSDocument *document) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    return static_cast<int>(document->document->assets.size());
}

uint64_t ms_document_asset_id_at(MSDocument *document, int index) {
    DocumentLock lock(document);
    if (document == nullptr || index < 0 || index >= static_cast<int>(document->document->assets.size())) {
        return 0;
    }
    return document->document->assets[static_cast<size_t>(index)].id.value;
}

char *ms_asset_name(MSDocument *document, uint64_t assetId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return nullptr;
    }
    Asset *asset = FindAsset(*document->document, assetId);
    return asset == nullptr ? nullptr : strdup(asset->name.c_str());
}

char *ms_asset_path(MSDocument *document, uint64_t assetId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return nullptr;
    }
    Asset *asset = FindAsset(*document->document, assetId);
    return asset == nullptr ? nullptr : strdup(asset->path.c_str());
}

int ms_asset_width(MSDocument *document, uint64_t assetId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    Asset *asset = FindAsset(*document->document, assetId);
    return asset == nullptr ? 0 : asset->width;
}

int ms_asset_height(MSDocument *document, uint64_t assetId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    Asset *asset = FindAsset(*document->document, assetId);
    return asset == nullptr ? 0 : asset->height;
}

int ms_asset_type(MSDocument *document, uint64_t assetId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    Asset *asset = FindAsset(*document->document, assetId);
    if (asset == nullptr) {
        return 0;
    }
    return asset->type == AssetType::Font ? 1 : 0;
}
