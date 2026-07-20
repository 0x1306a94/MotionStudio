#include "MotionStudio/serialization/Dto.h"

namespace motion::dto {

const char *ToString(LayerType type) {
    switch (type) {
        case LayerType::Shape: {
            return "shape";
        }
        case LayerType::Image: {
            return "image";
        }
        case LayerType::Text: {
            return "text";
        }
        case LayerType::Null: {
            return "null";
        }
        case LayerType::Precomp: {
            return "precomp";
        }
    }
    return "unknown";
}

Expected<LayerType> layerTypeFromString(const std::string &text) {
    if (text == "shape") {
        return LayerType::Shape;
    }
    if (text == "image") {
        return LayerType::Image;
    }
    if (text == "text") {
        return LayerType::Text;
    }
    if (text == "null") {
        return LayerType::Null;
    }
    if (text == "precomp") {
        return LayerType::Precomp;
    }
    return Error("unknown layer type: " + text);
}

const char *ToString(ShapeType type) {
    switch (type) {
        case ShapeType::Path: {
            return "path";
        }
        case ShapeType::Fill: {
            return "fill";
        }
        case ShapeType::Stroke: {
            return "stroke";
        }
        case ShapeType::Group: {
            return "group";
        }
        case ShapeType::Rect: {
            return "rect";
        }
        case ShapeType::Ellipse: {
            return "ellipse";
        }
        case ShapeType::TrimPath: {
            return "trimPath";
        }
    }
    return "unknown";
}

Expected<ShapeType> shapeTypeFromString(const std::string &text) {
    if (text == "path") {
        return ShapeType::Path;
    }
    if (text == "fill") {
        return ShapeType::Fill;
    }
    if (text == "stroke") {
        return ShapeType::Stroke;
    }
    if (text == "group") {
        return ShapeType::Group;
    }
    if (text == "rect") {
        return ShapeType::Rect;
    }
    if (text == "ellipse") {
        return ShapeType::Ellipse;
    }
    if (text == "trimPath") {
        return ShapeType::TrimPath;
    }
    return Error("unknown shape type: " + text);
}

const char *ToString(FillRule rule) {
    return rule == FillRule::NonZero ? "nonZero" : "evenOdd";
}

Expected<FillRule> fillRuleFromString(const std::string &text) {
    if (text == "nonZero") {
        return FillRule::NonZero;
    }
    if (text == "evenOdd") {
        return FillRule::EvenOdd;
    }
    return Error("unknown fill rule: " + text);
}

const char *ToString(LineCap cap) {
    switch (cap) {
        case LineCap::Butt: {
            return "butt";
        }
        case LineCap::Round: {
            return "round";
        }
        case LineCap::Square: {
            return "square";
        }
    }
    return "unknown";
}

Expected<LineCap> lineCapFromString(const std::string &text) {
    if (text == "butt") {
        return LineCap::Butt;
    }
    if (text == "round") {
        return LineCap::Round;
    }
    if (text == "square") {
        return LineCap::Square;
    }
    return Error("unknown line cap: " + text);
}

const char *ToString(LineJoin join) {
    switch (join) {
        case LineJoin::Miter: {
            return "miter";
        }
        case LineJoin::Round: {
            return "round";
        }
        case LineJoin::Bevel: {
            return "bevel";
        }
    }
    return "unknown";
}

Expected<LineJoin> lineJoinFromString(const std::string &text) {
    if (text == "miter") {
        return LineJoin::Miter;
    }
    if (text == "round") {
        return LineJoin::Round;
    }
    if (text == "bevel") {
        return LineJoin::Bevel;
    }
    return Error("unknown line join: " + text);
}

const char *ToString(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: {
            return "normal";
        }
        case BlendMode::Multiply: {
            return "multiply";
        }
        case BlendMode::Screen: {
            return "screen";
        }
        case BlendMode::Add: {
            return "add";
        }
    }
    return "unknown";
}

Expected<BlendMode> blendModeFromString(const std::string &text) {
    if (text == "normal") {
        return BlendMode::Normal;
    }
    if (text == "multiply") {
        return BlendMode::Multiply;
    }
    if (text == "screen") {
        return BlendMode::Screen;
    }
    if (text == "add") {
        return BlendMode::Add;
    }
    return Error("unknown blend mode: " + text);
}

const char *ToString(MaskMode mode) {
    switch (mode) {
        case MaskMode::Add: {
            return "add";
        }
        case MaskMode::Subtract: {
            return "subtract";
        }
        case MaskMode::Intersect: {
            return "intersect";
        }
    }
    return "unknown";
}

Expected<MaskMode> maskModeFromString(const std::string &text) {
    if (text == "add") {
        return MaskMode::Add;
    }
    if (text == "subtract") {
        return MaskMode::Subtract;
    }
    if (text == "intersect") {
        return MaskMode::Intersect;
    }
    return Error("unknown mask mode: " + text);
}

const char *ToString(AssetType type) {
    return type == AssetType::Image ? "image" : "font";
}

Expected<AssetType> assetTypeFromString(const std::string &text) {
    if (text == "image") {
        return AssetType::Image;
    }
    if (text == "font") {
        return AssetType::Font;
    }
    return Error("unknown asset type: " + text);
}

const char *ToString(Easing::Type type) {
    switch (type) {
        case Easing::Type::Linear: {
            return "linear";
        }
        case Easing::Type::Bezier: {
            return "bezier";
        }
        case Easing::Type::Hold: {
            return "hold";
        }
    }
    return "unknown";
}

Expected<Easing::Type> easingTypeFromString(const std::string &text) {
    if (text == "linear") {
        return Easing::Type::Linear;
    }
    if (text == "bezier") {
        return Easing::Type::Bezier;
    }
    if (text == "hold") {
        return Easing::Type::Hold;
    }
    return Error("unknown easing type: " + text);
}

}  // namespace motion::dto
