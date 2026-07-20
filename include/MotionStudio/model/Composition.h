#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

class Composition {
public:
    EntityId id = EntityId::Generate();
    std::string name;
    FrameTime duration = 0;  // 帧数
    FrameRate frameRate;
    int width = 1920;
    int height = 1080;
    Color backgroundColor{0, 0, 0, 1};

    std::vector<std::unique_ptr<Layer>> layers;  // 有序，index 0 = 最底层，向上渲染
};

}  // namespace motion
