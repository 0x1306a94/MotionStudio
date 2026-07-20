#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ShapeType.h"

namespace motion {

// Base class for shape elements. Concrete types: ShapePath, ShapeFill,
// ShapeStroke, ShapeGroup, ShapeRect, ShapeEllipse, ShapeTrimPath.
// Elements are rendered in the order they appear in their parent list.
class ShapeElement {
  public:
    // type: which shape element variant this instance represents.
    explicit ShapeElement(ShapeType type);
    virtual ~ShapeElement();

    // Returns the shape element variant tag.
    ShapeType type() const;

    EntityId id;

  private:
    ShapeType type_;
};

}  // namespace motion
