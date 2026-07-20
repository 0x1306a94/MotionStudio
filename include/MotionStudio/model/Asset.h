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
    std::string path;
};

}  // namespace motion
