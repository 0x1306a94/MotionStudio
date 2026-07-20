#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerContent.h"

namespace motion {

class ImageContent : public LayerContent {
public:
    ImageContent();
    ~ImageContent() override;

    EntityId assetId;  // 引用 Document 级 Asset
};

}  // namespace motion
