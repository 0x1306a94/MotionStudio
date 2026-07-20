#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerContent.h"

namespace motion {

// Layer content that references another composition (pre-composition).
class PrecompContent : public LayerContent {
  public:
    PrecompContent();
    ~PrecompContent() override;

    // References another Composition by id.
    EntityId compositionId;
};

}  // namespace motion
