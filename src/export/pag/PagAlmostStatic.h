#pragma once

#include "MotionStudio/common/Time.h"
#include "MotionStudio/export/BitmapFrameSource.h"

namespace motion {
namespace pag_export {

// Samples first/mid/last (+ up to 2 uniform) frames in [start, end).
// Returns true if changed-pixel ratio across consecutive samples < 0.001f.
// Requires a prepared FrameSource; does not call finish().
bool IsAlmostStaticSequence(BitmapFrameSource *source, FrameTime start, FrameTime end);

}  // namespace pag_export
}  // namespace motion
