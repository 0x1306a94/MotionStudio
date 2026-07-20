#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"

namespace motion {

enum class AssetType { Image, Font };

// 文档级资源（图片/字体）。M1 仅保留最小字段。
struct Asset {
    EntityId id = EntityId::generate();
    AssetType type = AssetType::Image;
    std::string name;
    std::string path;
};

}  // namespace motion
