#include "MotionStudio/import/svg/SvgImporter.h"

#include <fstream>
#include <iterator>

#include "SvgParse.h"
#include "SvgTransform.h"
#include "SvgWalk.h"

namespace motion {
namespace svg {

Expected<SvgLayerTree, std::string> BuildSvgLayers(const void *bytes, size_t length,
                                                   const ImportOptions &options) {
    auto parsed = ParseSvgBytes(bytes, length);
    if (!parsed.hasValue()) {
        return Unexpected<std::string>(parsed.error());
    }
    auto root = std::make_unique<Layer>(LayerType::Group);
    root->name = options.rootName;
    root->inPoint = 0;
    root->outPoint = 0;
    SvgLayerTree tree = {};
    tree.sourceWidth = parsed.value().sourceWidth;
    tree.sourceHeight = parsed.value().sourceHeight;
    tree.layers.push_back(std::move(root));
    if (parsed.value().dom && parsed.value().dom->getRoot()) {
        WalkSvgRoot(*parsed.value().dom->getRoot(), tree);
    }
    AssignCenterAnchors(tree.layers);
    return tree;
}

Expected<SvgLayerTree, std::string> BuildSvgLayersFromFile(const std::string &path,
                                                           const ImportOptions &options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Unexpected<std::string>("cannot read file");
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    return BuildSvgLayers(contents.data(), contents.size(), options);
}

Expected<ImportResult, std::string> ImportSvgInto(Document &document, UndoManager &undo,
                                                  EntityId compositionId, const void *bytes,
                                                  size_t length, const ImportOptions &options) {
    (void)document;
    (void)undo;
    (void)compositionId;
    (void)bytes;
    (void)length;
    (void)options;
    return Unexpected<std::string>("not implemented");
}

Expected<ImportResult, std::string> ImportSvgFileInto(Document &document, UndoManager &undo,
                                                      EntityId compositionId, const std::string &path,
                                                      const ImportOptions &options) {
    (void)document;
    (void)undo;
    (void)compositionId;
    (void)path;
    (void)options;
    return Unexpected<std::string>("not implemented");
}

}  // namespace svg
}  // namespace motion
