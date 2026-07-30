#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/AssetType.h"

namespace motion {

// Document-level asset (image or font). M1 keeps only the minimal fields.
struct Asset {
    EntityId id = EntityId::Generate();
    AssetType type = AssetType::Image;
    std::string name;
    // Path relative to Document::projectRoot, e.g. "assets/photo.png".
    std::string path;
    // Intrinsic pixel size for image assets (written at import time).
    int width = 0;
    int height = 0;
};

}  // namespace motion
