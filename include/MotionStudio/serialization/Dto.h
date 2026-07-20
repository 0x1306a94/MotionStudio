#pragma once

#include <stdexcept>
#include <string>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/ShapeElement.h"

// JSON v1 schema 的枚举字符串映射（camelCase，与 Lottie 生态对齐）。
// 文件结构见 Serializer.cpp；迁移只操作 JSON，不依赖运行时模型。
namespace motion::dto {

inline constexpr int kSchemaVersion = 1;

inline const char* toString(LayerType type) {
    switch (type) {
        case LayerType::Shape: return "shape";
        case LayerType::Image: return "image";
        case LayerType::Text: return "text";
        case LayerType::Null: return "null";
        case LayerType::Precomp: return "precomp";
    }
    throw std::invalid_argument("未知 LayerType");
}

inline LayerType layerTypeFromString(const std::string& text) {
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
    throw std::invalid_argument("未知 layer 类型: " + text);
}

inline const char* toString(ShapeType type) {
    switch (type) {
        case ShapeType::Path: return "path";
        case ShapeType::Fill: return "fill";
        case ShapeType::Stroke: return "stroke";
        case ShapeType::Group: return "group";
        case ShapeType::Rect: return "rect";
        case ShapeType::Ellipse: return "ellipse";
        case ShapeType::TrimPath: return "trimPath";
    }
    throw std::invalid_argument("未知 ShapeType");
}

inline ShapeType shapeTypeFromString(const std::string& text) {
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
    throw std::invalid_argument("未知 shape 类型: " + text);
}

inline const char* toString(FillRule rule) {
    return rule == FillRule::NonZero ? "nonZero" : "evenOdd";
}

inline FillRule fillRuleFromString(const std::string& text) {
    if (text == "nonZero") {
        return FillRule::NonZero;
    }
    if (text == "evenOdd") {
        return FillRule::EvenOdd;
    }
    throw std::invalid_argument("未知 fillRule: " + text);
}

inline const char* toString(LineCap cap) {
    switch (cap) {
        case LineCap::Butt: return "butt";
        case LineCap::Round: return "round";
        case LineCap::Square: return "square";
    }
    throw std::invalid_argument("未知 LineCap");
}

inline LineCap lineCapFromString(const std::string& text) {
    if (text == "butt") {
        return LineCap::Butt;
    }
    if (text == "round") {
        return LineCap::Round;
    }
    if (text == "square") {
        return LineCap::Square;
    }
    throw std::invalid_argument("未知 lineCap: " + text);
}

inline const char* toString(LineJoin join) {
    switch (join) {
        case LineJoin::Miter: return "miter";
        case LineJoin::Round: return "round";
        case LineJoin::Bevel: return "bevel";
    }
    throw std::invalid_argument("未知 LineJoin");
}

inline LineJoin lineJoinFromString(const std::string& text) {
    if (text == "miter") {
        return LineJoin::Miter;
    }
    if (text == "round") {
        return LineJoin::Round;
    }
    if (text == "bevel") {
        return LineJoin::Bevel;
    }
    throw std::invalid_argument("未知 lineJoin: " + text);
}

inline const char* toString(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: return "normal";
        case BlendMode::Multiply: return "multiply";
        case BlendMode::Screen: return "screen";
        case BlendMode::Add: return "add";
    }
    throw std::invalid_argument("未知 BlendMode");
}

inline BlendMode blendModeFromString(const std::string& text) {
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
    throw std::invalid_argument("未知 blendMode: " + text);
}

inline const char* toString(MaskMode mode) {
    switch (mode) {
        case MaskMode::Add: return "add";
        case MaskMode::Subtract: return "subtract";
        case MaskMode::Intersect: return "intersect";
    }
    throw std::invalid_argument("未知 MaskMode");
}

inline MaskMode maskModeFromString(const std::string& text) {
    if (text == "add") {
        return MaskMode::Add;
    }
    if (text == "subtract") {
        return MaskMode::Subtract;
    }
    if (text == "intersect") {
        return MaskMode::Intersect;
    }
    throw std::invalid_argument("未知 mask mode: " + text);
}

inline const char* toString(AssetType type) {
    return type == AssetType::Image ? "image" : "font";
}

inline AssetType assetTypeFromString(const std::string& text) {
    if (text == "image") {
        return AssetType::Image;
    }
    if (text == "font") {
        return AssetType::Font;
    }
    throw std::invalid_argument("未知 asset 类型: " + text);
}

inline const char* toString(Easing::Type type) {
    switch (type) {
        case Easing::Type::Linear: return "linear";
        case Easing::Type::Bezier: return "bezier";
        case Easing::Type::Hold: return "hold";
    }
    throw std::invalid_argument("未知 Easing::Type");
}

inline Easing::Type easingTypeFromString(const std::string& text) {
    if (text == "linear") {
        return Easing::Type::Linear;
    }
    if (text == "bezier") {
        return Easing::Type::Bezier;
    }
    if (text == "hold") {
        return Easing::Type::Hold;
    }
    throw std::invalid_argument("未知 easing 类型: " + text);
}

}  // namespace motion::dto
