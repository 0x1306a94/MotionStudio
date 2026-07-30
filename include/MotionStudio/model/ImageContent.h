#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/LayerContent.h"

namespace motion {

// Layer content that references a document-level image asset.
class ImageContent : public LayerContent {
  public:
    ImageContent();
    ~ImageContent() override;

    // References a document-level Asset by id. Invalid = unbound placeholder.
    EntityId assetId;
    // Container size in layer-local pixels (independent of transform.scale).
    Animatable<Vec2> size{Vec2{200, 200}};
    ImageScaleMode scaleMode = ImageScaleMode::LetterBox;
};

}  // namespace motion
