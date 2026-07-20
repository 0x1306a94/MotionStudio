#include "MotionStudio/model/ShapeElement.h"

namespace motion {

ShapeElement::ShapeElement(ShapeType type)
    : id(EntityId::Generate())
    , type_(type) {
}

ShapeElement::~ShapeElement() = default;

ShapeType ShapeElement::type() const {
    return type_;
}

}  // namespace motion
