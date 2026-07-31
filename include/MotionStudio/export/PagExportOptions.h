#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"

namespace motion {

// Options for PagExporter::Export.
struct PagExportOptions {
    // Non-empty path additionally writes the encoded bytes to disk.
    std::string outputPath;
    // Invalid id selects the document's first composition.
    EntityId compositionId;
    // When false, unsupported content fails the export instead of rasterizing.
    bool allowBitmapFallback = true;
    // Pixel scale for bitmap fallback (>0). Applied to the host composition size.
    float bitmapScale = 1.0f;
};

}  // namespace motion
