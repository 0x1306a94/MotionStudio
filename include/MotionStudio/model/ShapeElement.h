#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ShapeType.h"

namespace motion {

// Base class for shape geometry elements. Fill/Stroke live on Layer as styles.
// Concrete geometry types: ShapePath, ShapeRect, ShapeEllipse, ShapeTrimPath.
// Nesting / shared transforms use the Layer tree (LayerType::Group + parentId).
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
