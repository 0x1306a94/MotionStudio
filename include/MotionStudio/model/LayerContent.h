#pragma once

#include "MotionStudio/model/LayerType.h"

namespace motion {

// 图层内容多态基类（五态：Shape / Image / Text / Null / Precomp）。
class LayerContent {
public:
    explicit LayerContent(LayerType type);
    virtual ~LayerContent();

    LayerType type() const;

private:
    LayerType type_;
};

}  // namespace motion
