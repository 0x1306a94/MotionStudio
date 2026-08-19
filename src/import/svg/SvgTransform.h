#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Layer.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/svg/node/SVGRoot.h"

namespace motion {
namespace svg {

struct DecomposedTransform {
    Vec2 translation = {};
    Vec2 scale = {1.f, 1.f};
    float rotationDegrees = 0.f;
    bool hasShear = false;
    bool singular = false;
};

DecomposedTransform DecomposeSvgMatrix(const tgfx::Matrix &matrix);
void ApplyResidualBake(Layer &layer, const tgfx::Matrix &residual);
void ApplySvgMatrixToLayer(Layer &layer, const tgfx::Matrix &matrix);
void BakeResidualIntoDescendants(std::vector<std::unique_ptr<Layer>> &layers, EntityId parentId,
                                 const tgfx::Matrix &residual);
void ApplyRootViewBoxAndTransform(Layer &root, const tgfx::SVGRoot &svgRoot, int sourceWidth,
                                  int sourceHeight);
void AssignCenterAnchors(std::vector<std::unique_ptr<Layer>> &layers);

}  // namespace svg
}  // namespace motion
