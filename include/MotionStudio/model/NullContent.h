#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/LayerContent.h"

namespace motion {

// Empty layer content used as a parent anchor or controller.
class NullContent : public LayerContent {
  public:
    NullContent();
    ~NullContent() override;

    // Group clip radius in layer-local pixels. Clip rect is descendant AABB.
    Animatable<float> cornerRadius{0.0f};
};

}  // namespace motion
