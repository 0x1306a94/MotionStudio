#pragma once

#include "MotionStudio/model/LayerContent.h"

namespace motion {

class NullContent : public LayerContent {
public:
    NullContent();
    ~NullContent() override;
};

}  // namespace motion
