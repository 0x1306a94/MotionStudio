#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"

namespace motion {

class Document;

// One CPU raster frame for PAG bitmap fallback. Pixels are row-major top-left RGBA8.
// Lifetime of rgba is owned by the BitmapFrameSource until the next renderFrame/finish.
struct BitmapFrame {
    int width = 0;
    int height = 0;
    const uint8_t *rgba = nullptr;
    size_t rowBytes = 0;
    bool premultiplied = true;
};

// Supplies per-frame RGBA for layers (or Group subtrees) that cannot map to editable PAG.
// Production offscreen implementations are optional; tests inject a Fake source.
class BitmapFrameSource {
  public:
    virtual ~BitmapFrameSource() = default;

    // Prepare rendering for one layer (or Group subtree) on the host composition timeline.
    // document: immutable source document for the export call.
    // hostCompositionId: composition that owns rootLayerId.
    // rootLayerId: layer or Group root being rasterized.
    // visibleRange: half-open [start, end) on the host timeline.
    // bitmapScale: >0 pixel scale relative to the host composition size.
    virtual Expected<void, std::string> prepare(const Document &document,
                                                EntityId hostCompositionId, EntityId rootLayerId,
                                                TimeRange visibleRange, float bitmapScale) = 0;

    // Prepare rendering for an entire composition (composition-name _bmp path).
    // document: immutable source document for the export call.
    // compositionId: composition to rasterize.
    // visibleRange: half-open [start, end) on that composition's timeline.
    // bitmapScale: >0 pixel scale relative to the composition size.
    virtual Expected<void, std::string> prepareComposition(const Document &document,
                                                           EntityId compositionId,
                                                           TimeRange visibleRange,
                                                           float bitmapScale) = 0;

    // time: timeline frame for the active prepare. Returns premultiplied RGBA8 (recommended).
    virtual Expected<BitmapFrame, std::string> renderFrame(FrameTime time) = 0;

    virtual void finish() = 0;
};

}  // namespace motion
