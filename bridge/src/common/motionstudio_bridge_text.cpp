#include "motionstudio_bridge.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/ImportFontAssetCommand.h"
#include "MotionStudio/undo/SetTextAlignCommand.h"
#include "MotionStudio/undo/SetTextAutoHeightCommand.h"
#include "MotionStudio/undo/SetTextFontAssetCommand.h"
#include "MotionStudio/undo/SetTextFontFamilyCommand.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Asset;
using motion::AssetType;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::TextAlign;
using motion::TextContent;
using motion::Vec2;

namespace {

std::string UniqueAssetFileName(const std::filesystem::path &assetsDir,
                                const std::string &preferredName) {
    std::filesystem::path preferred(preferredName.empty() ? "font.ttf" : preferredName);
    std::string stem = preferred.stem().string();
    std::string extension = preferred.extension().string();
    if (extension.empty()) {
        extension = ".ttf";
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

TextContent *TextContentOf(Layer *layer) {
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return nullptr;
    }
    return static_cast<TextContent *>(layer->content.get());
}

}  // namespace

uint64_t ms_command_import_font_asset(MSDocument *document, const char *sourceAbsolutePath,
                                      const char *preferredFileName) {
    DocumentLock lock(document);
    if (document == nullptr || sourceAbsolutePath == nullptr) {
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
    asset.type = AssetType::Font;
    asset.name = fileName;
    asset.path = (std::filesystem::path("assets") / fileName).generic_string();
    const uint64_t assetId = asset.id.value;
    Execute(document, std::make_unique<motion::ImportFontAssetCommand>(std::move(asset)));
    return assetId;
}

uint64_t ms_command_add_text_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Text);
    layer->name = "Text " + std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.anchorPoint.setStaticValue(Vec2{200.0f, 60.0f});
    layer->transform.position.setStaticValue(
        Vec2{composition->width * 0.5f, composition->height * 0.5f});
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(motion::Color{0.0f, 0.0f, 0.0f, 1.0f});
    layer->styles.push_back(std::move(fill));
    const uint64_t layerId = layer->id.value;
    Execute(document, std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

bool ms_command_set_text_font_asset(MSDocument *document, uint64_t layerId, uint64_t assetId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document,
            std::make_unique<motion::SetTextFontAssetCommand>(EntityId{layerId}, EntityId{assetId}));
    return true;
}

bool ms_command_set_text_font_family(MSDocument *document, uint64_t layerId, const char *family) {
    DocumentLock lock(document);
    if (document == nullptr || family == nullptr) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document,
            std::make_unique<motion::SetTextFontFamilyCommand>(EntityId{layerId}, std::string(family)));
    return true;
}

bool ms_command_set_text_auto_height(MSDocument *document, uint64_t layerId, bool autoHeight) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document, std::make_unique<motion::SetTextAutoHeightCommand>(EntityId{layerId}, autoHeight));
    return true;
}

bool ms_command_set_text_align(MSDocument *document, uint64_t layerId, MS_TEXT_ALIGN align) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    if (align < MS_TEXT_ALIGN_LEFT || align > MS_TEXT_ALIGN_RIGHT) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document, std::make_unique<motion::SetTextAlignCommand>(EntityId{layerId}, static_cast<TextAlign>(align)));
    return true;
}

bool ms_layer_text_auto_height(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    return content != nullptr ? content->autoHeight : true;
}

MS_TEXT_ALIGN ms_layer_text_align(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    if (content == nullptr) {
        return MS_TEXT_ALIGN_LEFT;
    }
    return static_cast<MS_TEXT_ALIGN>(content->align);
}

uint64_t ms_layer_text_font_asset(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    return content != nullptr ? content->fontAssetId.value : 0;
}

char *ms_layer_text_font_family(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    return content == nullptr ? nullptr : strdup(content->fontFamily.c_str());
}
