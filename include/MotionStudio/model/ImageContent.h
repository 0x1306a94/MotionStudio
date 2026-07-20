#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerContent.h"

namespace motion {

// Layer content that references a document-level image asset.
class ImageContent : public LayerContent {
public:
    ImageContent();
    ~ImageContent() override;

    // References a document-level Asset by id.
    EntityId assetId;
};

}  // namespace motion
