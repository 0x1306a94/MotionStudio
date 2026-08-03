#pragma once

#include <functional>
#include <string>

#include "MotionStudio/common/EntityId.h"

namespace motion {

// Resolves font ascent (positive distance above baseline) for text export.
// Return <= 0 to fall back to the fontSize * 0.8 heuristic.
using TextAscentResolver = std::function<float(const std::string &fontFamily,
                                               const std::string &fontStyle, float fontSize)>;

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
    // Optional. Prefer real font metrics so point-text baseline matches MS layout.
    TextAscentResolver textAscentResolver;
};

}  // namespace motion
