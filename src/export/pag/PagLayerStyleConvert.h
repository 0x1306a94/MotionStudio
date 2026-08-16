#pragma once

#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/LayerFx.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

BlendMode LayerFxBlendMode(const LayerFx &style);

// Maps one enabled MS LayerFx to a heap-allocated pag::LayerStyle. Caller owns the result.
pag::LayerStyle *ToPagLayerStyle(const LayerFx &style, pag::BlendMode blendMode,
                                 std::vector<PagExportWarning> *warnings, EntityId entityId);

}  // namespace pag_export
}  // namespace motion
