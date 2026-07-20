#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ShapeType.h"

namespace motion {

// Shape 元素基类。具体类型见 ShapePath/ShapeFill/ShapeStroke/ShapeGroup/
// ShapeRect/ShapeEllipse/ShapeTrimPath；有序，渲染按序应用。
class ShapeElement {
public:
    explicit ShapeElement(ShapeType type);
    virtual ~ShapeElement();

    ShapeType type() const;

    EntityId id;

private:
    ShapeType type_;
};

}  // namespace motion
