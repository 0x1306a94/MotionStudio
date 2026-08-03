#pragma once

#include <set>
#include <vector>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeElement.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

// Collects frame times where outline geometry may change (geometry + stroke width KFs).
// Always includes `fallbackTime` when the set would otherwise be empty.
void CollectStrokeOutlineBakeTimes(const ShapeElement &geometry, const StrokeStyle &stroke,
                                   FrameTime fallbackTime, std::set<FrameTime> *times);

// Bakes Inside/Outside outline at `time`. Returns empty handle on failure / Center position.
pag::PathHandle BakePositionedStrokeOutline(const ShapeElement &geometry, const StrokeStyle &stroke,
                                            FrameTime time);

// Static or keyframed PathHandle covering `times` (must be non-empty, sorted ascending).
// Returns nullptr if baking fails at any required sample.
pag::Property<pag::PathHandle> *MakeStrokeOutlinePathProperty(const ShapeElement &geometry,
                                                              const StrokeStyle &stroke,
                                                              const std::vector<FrameTime> &times);

}  // namespace pag_export
}  // namespace motion
