#pragma once

#include "MotionStudio/model/LayerContent.h"

namespace motion {

// Empty layer content used as a parent anchor or controller.
class NullContent : public LayerContent {
  public:
    NullContent();
    ~NullContent() override;
};

}  // namespace motion
