#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/ShapeElement.h"
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
    PagFileBuilder(const Document &document, const Composition &composition);

    Expected<PagBuildResult, PagExportError> build();

  private:
    Expected<pag::VectorComposition *, PagExportError> buildComposition();
    Expected<pag::Layer *, PagExportError> buildLayer(const Layer &layer);
    Expected<pag::ShapeLayer *, PagExportError> buildShapeLayer(const Layer &layer);
    Expected<pag::NullLayer *, PagExportError> buildNullLayer(const Layer &layer);
    Expected<void, PagExportError> fillCommonLayer(pag::Layer *pagLayer, const Layer &layer);
    Expected<pag::ShapeElement *, PagExportError> buildGeometry(const ShapeElement &element,
                                                                EntityId layerId);
    Expected<void, PagExportError> appendStyles(std::vector<pag::ShapeElement *> *contents,
                                                const Layer &layer);
    Expected<void, PagExportError> rejectUnsupported(const Layer &layer);
    pag::Transform2D *buildTransform(const Transform &transform, EntityId layerId);
    pag::BlendMode mapBlendMode(BlendMode mode, EntityId layerId, bool *ok);

    const Document &document_;
    const Composition &composition_;
    std::vector<PagExportWarning> warnings_;
    std::unordered_map<uint64_t, pag::Layer *> layerByEntity_;
    pag::ID nextLayerId_ = 1;
};

}  // namespace pag_export
}  // namespace motion
