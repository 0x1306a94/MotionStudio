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
        case LayerType::Group: {
            return "group";
        }
        case LayerType::Precomp: {
            return "precomp";
        }
    }
    return "unknown";
}

Expected<LayerType, std::string> layerTypeFromString(const std::string &text) {
    if (text == "shape") {
        return LayerType::Shape;
    }
    if (text == "image") {
        return LayerType::Image;
    }
    if (text == "text") {
        return LayerType::Text;
    }
    if (text == "group") {
        return LayerType::Group;
    }
    if (text == "precomp") {
        return LayerType::Precomp;
    }
    return Unexpected(std::string("unknown layer type: " + text));
}

const char *ToString(ShapeType type) {
    switch (type) {
        case ShapeType::Path: {
            return "path";
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

Expected<ShapeType, std::string> shapeTypeFromString(const std::string &text) {
    if (text == "path") {
        return ShapeType::Path;
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
    return Unexpected(std::string("unknown shape type: " + text));
}

const char *ToString(FillRule rule) {
    return rule == FillRule::NonZero ? "nonZero" : "evenOdd";
}

Expected<FillRule, std::string> fillRuleFromString(const std::string &text) {
    if (text == "nonZero") {
        return FillRule::NonZero;
    }
    if (text == "evenOdd") {
        return FillRule::EvenOdd;
    }
    return Unexpected(std::string("unknown fill rule: " + text));
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

Expected<LineCap, std::string> lineCapFromString(const std::string &text) {
    if (text == "butt") {
        return LineCap::Butt;
    }
    if (text == "round") {
        return LineCap::Round;
    }
    if (text == "square") {
        return LineCap::Square;
    }
    return Unexpected(std::string("unknown line cap: " + text));
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

Expected<LineJoin, std::string> lineJoinFromString(const std::string &text) {
    if (text == "miter") {
        return LineJoin::Miter;
    }
    if (text == "round") {
        return LineJoin::Round;
    }
    if (text == "bevel") {
        return LineJoin::Bevel;
    }
    return Unexpected(std::string("unknown line join: " + text));
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

Expected<BlendMode, std::string> blendModeFromString(const std::string &text) {
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
    return Unexpected(std::string("unknown blend mode: " + text));
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

Expected<MaskMode, std::string> maskModeFromString(const std::string &text) {
    if (text == "add") {
        return MaskMode::Add;
    }
    if (text == "subtract") {
        return MaskMode::Subtract;
    }
    if (text == "intersect") {
        return MaskMode::Intersect;
    }
    return Unexpected(std::string("unknown mask mode: " + text));
}

const char *ToString(AssetType type) {
    return type == AssetType::Image ? "image" : "font";
}

Expected<AssetType, std::string> assetTypeFromString(const std::string &text) {
    if (text == "image") {
        return AssetType::Image;
    }
    if (text == "font") {
        return AssetType::Font;
    }
    return Unexpected(std::string("unknown asset type: " + text));
}

}  // namespace motion::dto
