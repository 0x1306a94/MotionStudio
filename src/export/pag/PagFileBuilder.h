#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/export/BitmapFrameSource.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/ShapeElement.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/model/Transform.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

struct PagBuildResult {
    std::shared_ptr<pag::File> file;
    std::vector<PagExportWarning> warnings;
};

class PagFileBuilder {
  public:
    PagFileBuilder(const Document &document, const Composition &composition,
                   const PagExportOptions &options, BitmapFrameSource *frameSource);

    Expected<PagBuildResult, PagExportError> build();

  private:
    Expected<void, PagExportError> collectCompositionOrder(EntityId compositionId);
    Expected<pag::VectorComposition *, PagExportError> buildComposition(
        const Composition &composition);
    Expected<pag::Layer *, PagExportError> buildLayer(const Layer &layer);
    Expected<pag::Layer *, PagExportError> buildFallbackLayer(const Layer &layer,
                                                              const Composition &hostComposition);
    Expected<pag::ShapeLayer *, PagExportError> buildShapeLayer(const Layer &layer);
    Expected<pag::NullLayer *, PagExportError> buildNullLayer(const Layer &layer);
    Expected<pag::TextLayer *, PagExportError> buildTextLayer(const Layer &layer);
    Expected<pag::ImageLayer *, PagExportError> buildImageLayer(const Layer &layer);
    Expected<pag::PreComposeLayer *, PagExportError> buildPrecompLayer(const Layer &layer);
    Expected<void, PagExportError> fillCommonLayer(pag::Layer *pagLayer, const Layer &layer);
    Expected<void, PagExportError> appendMasks(pag::Layer *pagLayer, const Layer &layer);
    Expected<pag::ShapeElement *, PagExportError> buildGeometry(const ShapeElement &element,
                                                                EntityId layerId);
    Expected<void, PagExportError> appendStyles(std::vector<pag::ShapeElement *> *contents,
                                                const Layer &layer);
    bool needsBitmapFallback(const Layer &layer) const;
    void collectDescendants(EntityId rootLayerId, std::unordered_set<uint64_t> *out) const;
    void skipLayerWithWarning(const Layer &layer, const char *code, const char *message,
                              std::unordered_set<uint64_t> *skippedDescendants);
    pag::ShapeLayer *buildCompositionBackdrop(const Composition &composition);
    pag::VectorComposition *wrapCompositionWithCornerClip(pag::VectorComposition *inner,
                                                          const Composition &composition);
    void applyImageContainerFit(pag::ImageLayer *pagLayer, const Layer &layer,
                                const ImageContent &content, int intrinsicWidth,
                                int intrinsicHeight);
    Expected<pag::ImageBytes *, PagExportError> imageBytesForAsset(EntityId assetId,
                                                                   EntityId layerId);
    pag::Property<pag::TextDocumentHandle> *buildSourceText(const Layer &layer,
                                                            const TextContent &content);
    pag::TextDocumentHandle makeTextDocument(const Layer &layer, const TextContent &content,
                                             FrameTime time);
    pag::Transform2D *buildTransform(const Transform &transform, EntityId layerId);
    pag::BlendMode mapBlendMode(BlendMode mode, EntityId layerId, bool *ok);
    const Composition *findComposition(EntityId id) const;

    const Document &document_;
    const Composition &rootComposition_;
    PagExportOptions options_ = {};
    BitmapFrameSource *frameSource_ = nullptr;
    std::vector<PagExportWarning> warnings_;
    std::vector<EntityId> compositionOrder_;
    std::unordered_set<uint64_t> visitedCompositions_;
    std::unordered_map<uint64_t, pag::Composition *> compositionByEntity_;
    std::unordered_map<uint64_t, pag::Layer *> layerByEntity_;
    std::unordered_map<uint64_t, pag::ImageBytes *> imageBytesByAsset_;
    std::vector<pag::ImageBytes *> imageBytesList_;
    std::vector<pag::Composition *> bitmapCompositions_;
    // Inner comps created when wrapping corner-radius clip; owned until VerifyAndMake.
    std::vector<pag::Composition *> nestedCompositions_;
    const Composition *currentHostComposition_ = nullptr;
    pag::ID nextLayerId_ = 1;
    pag::ID nextCompositionId_ = 1;
    pag::ID nextImageId_ = 1;
    pag::ID nextMaskId_ = 1;
};

}  // namespace pag_export
}  // namespace motion
