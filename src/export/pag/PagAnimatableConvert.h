#pragma once

#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/export/PagExporter.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

pag::Point ToPagPoint(Vec2 value);
pag::Color ToPagColor(const Color &value);
pag::Opacity ToPagOpacity(float opacity);
pag::PathHandle ToPagPath(const BezierPath &path);

// Converts MS Animatable to a heap-allocated pag::Property. Caller owns the result.
pag::Property<float> *ConvertFloat(const Animatable<float> &source,
                                   std::vector<PagExportWarning> *warnings, EntityId entityId);
pag::Property<pag::Point> *ConvertPoint(const Animatable<Vec2> &source,
                                        std::vector<PagExportWarning> *warnings, EntityId entityId);
pag::Property<pag::Color> *ConvertColor(const Animatable<Color> &source,
                                        std::vector<PagExportWarning> *warnings, EntityId entityId);
pag::Property<pag::Opacity> *ConvertOpacity(const Animatable<float> &source,
                                            std::vector<PagExportWarning> *warnings,
                                            EntityId entityId);
pag::Property<pag::PathHandle> *ConvertPath(const Animatable<BezierPath> &source,
                                            std::vector<PagExportWarning> *warnings,
                                            EntityId entityId);
pag::Property<pag::Percent> *ConvertPercent(const Animatable<float> &source,
                                            std::vector<PagExportWarning> *warnings,
                                            EntityId entityId);

}  // namespace pag_export
}  // namespace motion
