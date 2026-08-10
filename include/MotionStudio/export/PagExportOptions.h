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
    // When false, any bitmap export (including _bmp) fails the whole export.
    bool allowBitmapExport = true;
    // Pixel scale for bitmap export (>0). Applied to composition / host size.
    float bitmapScale = 1.0f;
    // Cap on the shorter bitmap side after scale; <=0 disables the cap.
    int bitmapMaxResolution = 720;
    // Max distance between bitmap keyframes (frames); aligned with AE exporter.
    int bitmapKeyFrameInterval = 60;
    // WebP quality 0–100 for bitmap sequence rectangles.
    int bitmapImageQuality = 80;
    // Optional. Prefer real font metrics so point-text baseline matches MS layout.
    TextAscentResolver textAscentResolver;
};

}  // namespace motion
