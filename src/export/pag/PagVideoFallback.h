#pragma once

#include <vector>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/export/BitmapFrameSource.h"
#include "MotionStudio/export/PagExportOptions.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

struct VideoFallbackResult {
    pag::PreComposeLayer *layer = nullptr;
    pag::VideoComposition *composition = nullptr;
};

// Rasterizes layers / compositions marked with _bmp into VideoComposition.
class PagVideoFallback {
  public:
    static Expected<VideoFallbackResult, PagExportError> Build(
        const Document &document, const Composition &hostComposition, const Layer &rootLayer,
        const PagExportOptions &options, BitmapFrameSource *frameSource, pag::ID compositionId,
        pag::ID layerId, std::vector<PagExportWarning> *warnings);

    static Expected<pag::VideoComposition *, PagExportError> BuildComposition(
        const Document &document, const Composition &composition, const PagExportOptions &options,
        BitmapFrameSource *frameSource, pag::ID compositionId,
        std::vector<PagExportWarning> *warnings);
};

}  // namespace pag_export
}  // namespace motion
