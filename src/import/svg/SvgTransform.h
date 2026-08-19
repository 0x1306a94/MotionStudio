#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/model/Layer.h"

namespace motion {
namespace svg {

void AssignCenterAnchors(std::vector<std::unique_ptr<Layer>> &layers);

}  // namespace svg
}  // namespace motion
