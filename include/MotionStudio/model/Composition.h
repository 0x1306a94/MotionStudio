#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

// A composition holds an ordered list of layers and timing/size metadata.
class Composition {
  public:
    EntityId id = EntityId::Generate();
    std::string name;
    FrameTime duration = 0;  // total frame count
    FrameRate frameRate;
    int width = 1920;
    int height = 1080;
    Color backgroundColor{0, 0, 0, 1};

    // Rendered bottom-to-top; index 0 is the bottommost layer.
    std::vector<std::unique_ptr<Layer>> layers;
};

}  // namespace motion
