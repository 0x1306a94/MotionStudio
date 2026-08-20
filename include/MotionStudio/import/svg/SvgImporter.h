#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/UndoManager.h"

namespace motion {
namespace svg {

// Options that control how a converted SVG tree is named and where it is inserted.
struct ImportOptions {
    // Index of the root Group in the host composition; -1 appends at the top.
    int insertIndex = -1;
    // Parent of the imported root Group; invalid id means the composition root.
    EntityId parentLayerId{};
    // Display name of the imported root Group.
    std::string rootName{"SVG"};
};

// A skipped or unsupported SVG feature recorded during conversion.
struct Diagnostic {
    enum class Severity {
        Warning,
        Error,
    };
    Severity severity = Severity::Warning;
    std::string code;
    std::string message;
    std::string nodeName;
};

// Decoded data-URI image that the host should write under project assets/.
struct EmbeddedImage {
    EntityId assetId;
    std::string suggestedFileName;
    std::vector<uint8_t> bytes;
};

// Pure conversion result: a flat layer list with parentId links, plus assets.
struct SvgLayerTree {
    std::vector<std::unique_ptr<Layer>> layers;
    std::vector<Asset> assets;
    std::vector<EmbeddedImage> embeddedImages;
    std::vector<Diagnostic> diagnostics;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

// Result of inserting a converted SVG tree into an existing composition.
struct ImportResult {
    EntityId rootLayerId;
    std::vector<EntityId> layerIds;
    std::vector<Diagnostic> diagnostics;
    std::vector<EmbeddedImage> embeddedImages;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

// Converts SVG bytes into a Core layer tree without mutating a Document.
// bytes: SVG XML buffer.
// length: byte count of the buffer.
// options: insertion name and parent (parent is applied later by ImportSvgInto).
Expected<SvgLayerTree, std::string> BuildSvgLayers(const void *bytes, size_t length,
                                                   const ImportOptions &options = {});

// Reads path and forwards the file contents to BuildSvgLayers.
// path: filesystem path of an .svg file.
// options: same as BuildSvgLayers.
Expected<SvgLayerTree, std::string> BuildSvgLayersFromFile(const std::string &path,
                                                           const ImportOptions &options = {});

// Inserts the converted tree into compositionId as one undoable CompositeCommand.
// document: destination document.
// undo: host UndoManager that records the import.
// compositionId: existing composition that receives the layers.
// bytes / length: SVG XML buffer.
// options: insert index, optional parent, and root Group name.
Expected<ImportResult, std::string> ImportSvgInto(Document &document, UndoManager &undo,
                                                  EntityId compositionId, const void *bytes,
                                                  size_t length,
                                                  const ImportOptions &options = {});

// Reads path and forwards the file contents to ImportSvgInto.
Expected<ImportResult, std::string> ImportSvgFileInto(Document &document, UndoManager &undo,
                                                      EntityId compositionId,
                                                      const std::string &path,
                                                      const ImportOptions &options = {});

}  // namespace svg
}  // namespace motion
