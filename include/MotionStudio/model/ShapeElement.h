#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Math.h"
#include "MotionStudio/model/Transform.h"

namespace motion {

enum class ShapeType { Path, Fill, Stroke, Group, Rect, Ellipse, TrimPath };

enum class FillRule { NonZero, EvenOdd };

enum class LineCap { Butt, Round, Square };

enum class LineJoin { Miter, Round, Bevel };

// Shape 元素基类。具体类型见下方各子类；有序，渲染按序应用。
class ShapeElement {
public:
    explicit ShapeElement(ShapeType type) : id(EntityId::generate()), type_(type) {}
    virtual ~ShapeElement() = default;

    ShapeType type() const { return type_; }

    EntityId id;

private:
    ShapeType type_;
};

class ShapePath : public ShapeElement {
public:
    ShapePath() : ShapeElement(ShapeType::Path) {}

    Animatable<BezierPath> path;  // 整条路径作为可动画值
};

class ShapeFill : public ShapeElement {
public:
    ShapeFill() : ShapeElement(ShapeType::Fill) {}

    Animatable<Color> color{Color{0, 0, 0, 1}};
    Animatable<float> opacity{1.0f};
    FillRule fillRule = FillRule::NonZero;
};

class ShapeStroke : public ShapeElement {
public:
    ShapeStroke() : ShapeElement(ShapeType::Stroke) {}

    Animatable<Color> color{Color{0, 0, 0, 1}};
    Animatable<float> width{2.0f};
    Animatable<float> opacity{1.0f};
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.0f;
};

class ShapeGroup : public ShapeElement {
public:
    ShapeGroup() : ShapeElement(ShapeType::Group) {}

    Transform transform;
    std::vector<std::unique_ptr<ShapeElement>> elements;
};

class ShapeRect : public ShapeElement {
public:
    ShapeRect() : ShapeElement(ShapeType::Rect) {}

    Animatable<Vec2> position{Vec2{0, 0}};
    Animatable<Vec2> size{Vec2{0, 0}};
    Animatable<float> cornerRadius{0.0f};
};

class ShapeEllipse : public ShapeElement {
public:
    ShapeEllipse() : ShapeElement(ShapeType::Ellipse) {}

    Animatable<Vec2> position{Vec2{0, 0}};
    Animatable<Vec2> size{Vec2{0, 0}};
};

class ShapeTrimPath : public ShapeElement {
public:
    ShapeTrimPath() : ShapeElement(ShapeType::TrimPath) {}

    Animatable<float> start{0.0f};   // 0.0 ~ 1.0
    Animatable<float> end{1.0f};     // 0.0 ~ 1.0
    Animatable<float> offset{0.0f};  // 度
};

}  // namespace motion
