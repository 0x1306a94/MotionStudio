#pragma once

#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/LayerEffect.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

// Maps one enabled MS LayerEffect to a heap-allocated pag::Effect. Caller owns the result.
pag::Effect *ToPagEffect(const LayerEffect &effect, std::vector<PagExportWarning> *warnings,
                         EntityId entityId);

}  // namespace pag_export
}  // namespace motion
