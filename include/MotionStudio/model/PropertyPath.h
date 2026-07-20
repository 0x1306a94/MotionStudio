#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"

namespace motion {

class AnimatableBase;
class Document;

// 属性路径：定位实体内某个 Animatable<T>。
// 语法（以 '.' 分段，数组段写作 name[index]）：
//   Layer:         "transform.position" / "content.text"
//                  "elements[0].color"（ShapeContent，Group 可继续 "elements[i]"）
//   ShapeElement:  "color" / "path" / "width" ...（entityId 直接指向形状元素）
struct PropertyPath {
    EntityId entityId;  // Layer 或 ShapeElement 的 ID
    std::string path;

    bool operator==(const PropertyPath& other) const;
    bool operator!=(const PropertyPath& other) const;
};

// 路径解析中间表示。
struct PathSegment {
    std::string name;
    int index = -1;  // -1 = 无数组下标

    bool operator==(const PathSegment& other) const;
};

// 解析 "a.b[2].c" → [{a,-1},{b,2},{c,-1}]；格式非法返回空。
std::vector<PathSegment> ParsePropertyPath(const std::string& path);

// 解析属性路径到 Animatable；实体不存在 / 路径无效 / 类型不符返回 nullptr。
AnimatableBase* ResolveAnimatable(Document& document, const PropertyPath& property);

}  // namespace motion
