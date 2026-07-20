#pragma once

namespace motion {

// Concrete shape element types within a shape layer.
enum class ShapeType {
    Path,
    Fill,
    Stroke,
    Group,
    Rect,
    Ellipse,
    TrimPath
};

}  // namespace motion
