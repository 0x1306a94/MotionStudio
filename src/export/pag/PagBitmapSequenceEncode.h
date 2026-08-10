#pragma once

#include <cstdint>
#include <vector>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/export/BitmapFrameSource.h"
#include "MotionStudio/export/PagExporter.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

struct BitmapSize {
    int width = 0;
    int height = 0;
    float factor = 1.0f;
};

// Applies bitmapScale then optional short-side maxResolution (AE-aligned).
BitmapSize ComputeBitmapSize(int compositionWidth, int compositionHeight, float bitmapScale,
                             int bitmapMaxResolution);

// Fills sequence->frames using AE-style diff / keyframe / WebP rectangle encoding.
Expected<void, PagExportError> EncodeBitmapSequence(BitmapFrameSource *frameSource,
                                                    pag::BitmapSequence *sequence, FrameTime start,
                                                    FrameTime end, int width, int height,
                                                    int keyFrameInterval, int imageQuality);

}  // namespace pag_export
}  // namespace motion
