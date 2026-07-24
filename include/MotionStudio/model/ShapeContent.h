#pragma once

#include <memory>

#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

// Layer content with a single editable geometry.
class ShapeContent : public LayerContent {
  public:
    ShapeContent();
    ~ShapeContent() override;

    std::unique_ptr<ShapeElement> geometry;
};

}  // namespace motion
