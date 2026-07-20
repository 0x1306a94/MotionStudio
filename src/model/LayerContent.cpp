#include "MotionStudio/model/LayerContent.h"

namespace motion {

LayerContent::LayerContent(LayerType type)
    : type_(type) {
}

LayerContent::~LayerContent() = default;

LayerType LayerContent::type() const {
    return type_;
}

}  // namespace motion
