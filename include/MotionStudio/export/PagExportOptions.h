#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"

namespace motion {

// Options for PagExporter::Export. Bitmap fallback is not supported in the
// current milestone; unsupported layers fail the whole export.
struct PagExportOptions {
    // Non-empty path additionally writes the encoded bytes to disk.
    std::string outputPath;
    // Invalid id selects the document's first composition.
    EntityId compositionId;
};

}  // namespace motion
