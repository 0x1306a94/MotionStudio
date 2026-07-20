#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

enum class LayerType { Shape, Image, Text, Null, Precomp };

// 图层内容多态基类（五态：Shape / Image / Text / Null / Precomp）。
class LayerContent {
public:
    explicit LayerContent(LayerType type) : type_(type) {}
    virtual ~LayerContent() = default;

    LayerType type() const { return type_; }

private:
    LayerType type_;
};

class ShapeContent : public LayerContent {
public:
    ShapeContent() : LayerContent(LayerType::Shape) {}

    std::vector<std::unique_ptr<ShapeElement>> elements;  // 有序
};

class ImageContent : public LayerContent {
public:
    ImageContent() : LayerContent(LayerType::Image) {}

    EntityId assetId;  // 引用 Document 级 Asset
};

class TextContent : public LayerContent {
public:
    TextContent() : LayerContent(LayerType::Text) {}

    Animatable<std::string> text{std::string{}};
    std::string fontFamily;
    Animatable<float> fontSize{24.0f};
};

class NullContent : public LayerContent {
public:
    NullContent() : LayerContent(LayerType::Null) {}
};

class PrecompContent : public LayerContent {
public:
    PrecompContent() : LayerContent(LayerType::Precomp) {}

    EntityId compositionId;  // 引用另一个 Composition（预合成）
};

}  // namespace motion
