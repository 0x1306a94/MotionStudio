#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/model/ShapeElement.h"
#include "MotionStudio/model/Transform.h"

namespace motion {

// Groups child shape elements under a shared transform.
class ShapeGroup : public ShapeElement {
  public:
    ShapeGroup();
    ~ShapeGroup() override;

    Transform transform;
    std::vector<std::unique_ptr<ShapeElement>> elements;
};

}  // namespace motion
