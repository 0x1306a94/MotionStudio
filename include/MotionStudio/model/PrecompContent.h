#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerContent.h"

namespace motion {

class PrecompContent : public LayerContent {
public:
    PrecompContent();
    ~PrecompContent() override;

    EntityId compositionId;  // 引用另一个 Composition（预合成）
};

}  // namespace motion
