#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/AssetType.h"

namespace motion {

// 文档级资源（图片/字体）。M1 仅保留最小字段。
struct Asset {
    EntityId id = EntityId::Generate();
    AssetType type = AssetType::Image;
    std::string name;
    std::string path;
};

}  // namespace motion
