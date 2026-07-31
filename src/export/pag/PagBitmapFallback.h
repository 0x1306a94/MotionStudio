#pragma once

#include <vector>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/export/BitmapFrameSource.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

struct BitmapFallbackResult {
    pag::PreComposeLayer *layer = nullptr;
    pag::BitmapComposition *composition = nullptr;
};

// Rasterizes one layer (or Group subtree root) into a BitmapComposition wrapped
// by a PreComposeLayer with identity transform and the original timing.
class PagBitmapFallback {
  public:
    static Expected<BitmapFallbackResult, PagExportError> Build(
        const Document &document, const Composition &hostComposition, const Layer &rootLayer,
        float bitmapScale, BitmapFrameSource *frameSource, pag::ID compositionId, pag::ID layerId,
        std::vector<PagExportWarning> *warnings);
};

}  // namespace pag_export
}  // namespace motion
