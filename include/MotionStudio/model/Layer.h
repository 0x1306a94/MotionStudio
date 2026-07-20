#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/Transform.h"

namespace motion {

class Document;

struct Mask {
    BezierPath path;
    MaskMode mode = MaskMode::Add;
    Animatable<float> opacity{1.0f};
    bool inverted = false;
};

class Layer {
public:
    // 按类型创建对应的 LayerContent。
    explicit Layer(LayerType type);
    ~Layer();

    LayerType type() const;

    // 设置父图层。沿父链检测环路，会形成环（含自身为父）或目标不存在时
    // 返回 false 且不修改；成功返回 true。
    bool setParent(EntityId newParentId, const Document& document);

    Mat3 localTransform(FrameTime time) const;
    Mat3 worldTransform(FrameTime time, const Document& document) const;

    EntityId id = EntityId::Generate();
    std::string name;

    // 时间控制
    FrameTime inPoint = 0;    // 在宿主合成时间轴上的起始帧
    FrameTime outPoint = 0;   // 结束帧（exclusive）
    FrameTime startTime = 0;  // 源时间偏移（Precomp 的源采样起点）
    double timeStretch = 1.0;

    bool visible = true;
    bool locked = false;

    EntityId parentId;  // 无效 = 无父级
    Transform transform;
    std::unique_ptr<LayerContent> content;

    BlendMode blendMode = BlendMode::Normal;
    std::vector<Mask> masks;

private:
    Mat3 worldTransform(FrameTime time, const Document& document, int depth) const;

    LayerType type_;
};

}  // namespace motion
