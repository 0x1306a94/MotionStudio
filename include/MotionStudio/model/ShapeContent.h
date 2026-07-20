#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

class ShapeContent : public LayerContent {
public:
    ShapeContent();
    ~ShapeContent() override;

    std::vector<std::unique_ptr<ShapeElement>> elements;  // 有序
};

}  // namespace motion
