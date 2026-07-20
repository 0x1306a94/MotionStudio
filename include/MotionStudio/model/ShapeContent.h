#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

// Layer content that holds an ordered list of shape elements.
class ShapeContent : public LayerContent {
  public:
    ShapeContent();
    ~ShapeContent() override;

    // Rendered in order; index 0 is drawn first.
    std::vector<std::unique_ptr<ShapeElement>> elements;
};

}  // namespace motion
