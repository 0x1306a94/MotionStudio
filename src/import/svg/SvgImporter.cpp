#include "MotionStudio/import/svg/SvgImporter.h"

#include <fstream>
#include <iterator>
#include <unordered_map>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/CompositeCommand.h"
#include "MotionStudio/undo/ImportImageAssetCommand.h"
#include "SvgParse.h"
#include "SvgTransform.h"
#include "SvgWalk.h"

namespace {

bool ReadFileContents(const std::string &path, std::string &out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return true;
}

void AppendPostorderIndices(size_t index,
                            const std::vector<std::unique_ptr<motion::Layer>> &layers,
                            const std::unordered_map<uint64_t, std::vector<size_t>> &children,
                            std::vector<char> &visiting, std::vector<size_t> &order) {
    if (index >= layers.size() || visiting[index] != 0) {
        return;
    }
    visiting[index] = 1;
    const auto found = children.find(layers[index]->id.value);
    if (found != children.end()) {
        for (size_t childIndex : found->second) {
            AppendPostorderIndices(childIndex, layers, children, visiting, order);
        }
    }
    order.push_back(index);
}

void ReorderParentAfterChildren(std::vector<std::unique_ptr<motion::Layer>> &layers) {
    if (layers.size() <= 1) {
        return;
    }
    std::unordered_map<uint64_t, size_t> indexById;
    for (size_t index = 0; index < layers.size(); ++index) {
        indexById[layers[index]->id.value] = index;
    }
    std::unordered_map<uint64_t, std::vector<size_t>> children;
    std::vector<size_t> roots;
    for (size_t index = 0; index < layers.size(); ++index) {
        const motion::EntityId parent = layers[index]->parentId;
        if (parent.isValid()) {
            const auto found = indexById.find(parent.value);
            if (found != indexById.end()) {
                children[parent.value].push_back(index);
                continue;
            }
        }
        roots.push_back(index);
    }
    std::vector<char> visiting(layers.size(), 0);
    std::vector<size_t> order;
    order.reserve(layers.size());
    for (size_t rootIndex : roots) {
        AppendPostorderIndices(rootIndex, layers, children, visiting, order);
    }
    for (size_t index = 0; index < layers.size(); ++index) {
        if (visiting[index] == 0) {
            order.push_back(index);
        }
    }
    std::vector<std::unique_ptr<motion::Layer>> reordered;
    reordered.reserve(layers.size());
    for (size_t index : order) {
        reordered.push_back(std::move(layers[index]));
    }
    layers = std::move(reordered);
}

}  // namespace

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
        WalkSvgRoot(*parsed.value().dom->getRoot(), parsed.value().dom->nodeIDMapper(), tree);
        ApplyRootViewBoxAndTransform(*tree.layers.front(), *parsed.value().dom->getRoot(),
                                     tree.sourceWidth, tree.sourceHeight);
    }
    AssignCenterAnchors(tree.layers);
    return tree;
}

Expected<SvgLayerTree, std::string> BuildSvgLayersFromFile(const std::string &path,
                                                           const ImportOptions &options) {
    std::string contents;
    if (!ReadFileContents(path, contents)) {
        return Unexpected<std::string>("cannot read file");
    }
    return BuildSvgLayers(contents.data(), contents.size(), options);
}

Expected<ImportResult, std::string> ImportSvgInto(Document &document, UndoManager &undo,
                                                  EntityId compositionId, const void *bytes,
                                                  size_t length, const ImportOptions &options) {
    auto built = BuildSvgLayers(bytes, length, options);
    if (!built.hasValue()) {
        return Unexpected<std::string>(built.error());
    }
    Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return Unexpected<std::string>("composition not found");
    }
    SvgLayerTree tree = std::move(built.value());
    if (tree.layers.empty()) {
        return Unexpected<std::string>("empty layer tree");
    }
    if (options.parentLayerId.isValid()) {
        if (!tree.layers.front()->setParent(options.parentLayerId, document)) {
            return Unexpected<std::string>("parent cycle");
        }
    }
    for (auto &layer : tree.layers) {
        layer->inPoint = 0;
        layer->outPoint = composition->duration;
    }
    ImportResult out;
    out.sourceWidth = tree.sourceWidth;
    out.sourceHeight = tree.sourceHeight;
    out.diagnostics = tree.diagnostics;
    out.embeddedImages = std::move(tree.embeddedImages);
    out.rootLayerId = tree.layers.front()->id;
    ReorderParentAfterChildren(tree.layers);
    auto composite = std::make_unique<CompositeCommand>("Import SVG");
    for (Asset &asset : tree.assets) {
        composite->add(std::make_unique<ImportImageAssetCommand>(asset));
    }
    int index = options.insertIndex;
    for (size_t i = 0; i < tree.layers.size(); ++i) {
        out.layerIds.push_back(tree.layers[i]->id);
        composite->add(std::make_unique<AddLayerCommand>(compositionId, std::move(tree.layers[i]),
                                                         index));
        if (index >= 0) {
            index += 1;
        }
    }
    undo.execute(document, std::move(composite));
    return out;
}

Expected<ImportResult, std::string> ImportSvgFileInto(Document &document, UndoManager &undo,
                                                      EntityId compositionId, const std::string &path,
                                                      const ImportOptions &options) {
    std::string contents;
    if (!ReadFileContents(path, contents)) {
        return Unexpected<std::string>("cannot read file");
    }
    return ImportSvgInto(document, undo, compositionId, contents.data(), contents.size(), options);
}

}  // namespace svg
}  // namespace motion
