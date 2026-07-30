# 动画数据结构

本文档定义 Core 层的动画数据模型、undo/redo 机制与序列化格式。这是整个项目的地基：模型必须能**准确完整地描述动画**（以对接多种渲染器/导出格式），且**天然支持 undo/redo**。

## 1. 时间表示

```cpp
// core/include/motionstudio/common/Time.h

using FrameTime = int64_t;   // 帧号，内部规范时间表示

struct TimeRange {
    FrameTime start;
    FrameTime end;           // exclusive
    bool contains(FrameTime t) const { return t >= start && t < end; }
};

struct FrameRate {
    uint32_t num;            // 分子，常见 24/25/30/60，或 30000（NTSC）
    uint32_t den;            // 分母，常见 1，NTSC 为 1001
    double    toSeconds(FrameTime f) const { return (double)f * den / num; }
    FrameTime fromSeconds(double s) const {
        return (FrameTime)std::llround(s * num / den);
    }
};
```

**选择帧号整数而非 double 秒的理由**：
- **精确**：动画编辑的基本单位是帧，整数运算无浮点漂移；时间轴吸附、帧步进天然对齐
- **可序列化**：JSON 中 `"time": 42` 确定且可读，不会出现 `1.7500000000000002`
- **代价极小**：仅在显示和渲染回调时换算为秒
- `num/den` 结构覆盖非整数帧率（29.97 drop-frame = 30000/1001）

## 2. 实体 ID

```cpp
// core/include/motionstudio/common/EntityId.h

struct EntityId {
    uint64_t value;          // 0 表示无效

    bool operator==(const EntityId&) const = default;
    bool isValid() const { return value != 0; }
    static EntityId generate();   // 安全随机数源
};
```

- 序列化时为 16 字符 hex 字符串
- 不用 128-bit UUID：单文档实体数 < 10⁵，64-bit 随机 ID 碰撞概率 < 10⁻⁹，而 map 键比较和内存占用更优

## 3. 层级模型

```
Document
 └─ Composition[]              （多个合成，支持互相引用 = 预合成）
     └─ Layer[]                （有序，index 0 = 最底层，向上渲染）
         ├─ Transform          （5 个可动画属性，必有）
         └─ LayerContent       （多态：Shape / Image / Text / Null / Precomp）
             └─ ShapeElement[] （Shape 类型时：Path / Fill / Stroke / Group ...）
```

**所有权是严格的树**（`unique_ptr` 持有），**引用全部走 `EntityId`**。

### 3.1 Document 与 EntityIndex

```cpp
class Document {
public:
    EntityId id;
    std::string name;
    std::vector<std::unique_ptr<Composition>> compositions;
    std::vector<Asset> assets;                 // 图片/字体等文档级资源

    // 非持久化：打开/保存包时由 App 设为包绝对路径。Asset.path 相对此根。
    std::string projectRoot;

    // ID → 实体的全局扁平索引，add/remove 时同步维护
    // 供 undo 命令和桥接层 O(1) 寻址目标
    class EntityIndex {
    public:
        Layer*        findLayer(EntityId id);
        Composition*  findComposition(EntityId id);
        ShapeElement* findShape(EntityId id);
        // 返回 nullptr 表示实体不存在（可能已被删除）
    };
    EntityIndex& entityIndex();
};

enum class AssetType { Image, Font };

struct Asset {
    EntityId id;
    AssetType type = AssetType::Image;
    std::string name;
    std::string path;   // 相对 projectRoot，如 "assets/photo.png"
    int width = 0;      // 源图像素宽（导入时写入；字体可为 0）
    int height = 0;
};
```

`EntityIndex` 是 undo 机制的关键：命令只持有 `EntityId`，执行时通过索引解析到当前指针；实体已删除则命令静默跳过。

### 3.2 Composition

```cpp
class Composition {
public:
    EntityId id;
    std::string name;
    FrameTime duration;          // 帧数
    FrameRate frameRate;
    int width, height;           // 画布尺寸
    Color backgroundColor;

    std::vector<std::unique_ptr<Layer>> layers;  // 有序，底 → 顶
};
```

### 3.3 Layer

```cpp
enum class LayerType { Shape, Image, Text, Null, Precomp };

class Layer {
public:
    EntityId id;
    std::string name;
    LayerType type;

    // 时间控制
    FrameTime inPoint;           // 在宿主合成时间轴上的起始帧
    FrameTime outPoint;          // 结束帧（exclusive）
    FrameTime startTime = 0;     // 源时间偏移（Precomp 的源采样起点）
    double timeStretch = 1.0;

    bool visible = true;
    bool locked = false;

    EntityId parentId;           // 无效 = 无父级
    Transform transform;
    std::unique_ptr<LayerContent> content;

    BlendMode blendMode = BlendMode::Normal;
    std::vector<Mask> masks;
    EntityId trackMatteLayerId;              // 无效 = 无 track matte
    TrackMatteType trackMatteType = None;    // Alpha / AlphaInverted / Luma / LumaInverted
};
```

**Mask**（图层路径遮罩，AE Masks 子集）：

```cpp
struct Mask {
    Animatable<BezierPath> path;     // layer 局部，可动画
    MaskMode mode = Add;             // Add / Subtract / Intersect
    Animatable<float> opacity{1};
    bool inverted = false;
    Animatable<float> feather{0};    // 羽化半径 px
    Animatable<float> expansion{0};  // 扩张/收缩 px（可负）
};
```

**Transform**——每个 Layer 必有，含 5 个可动画属性：

```cpp
struct Transform {
    Animatable<Vec2>  anchorPoint{{0, 0}};
    Animatable<Vec2>  position{{0, 0}};
    Animatable<Vec2>  scale{{1, 1}};
    Animatable<float> rotation{0};      // 度
    Animatable<float> opacity{1};       // 0.0 ~ 1.0
};
```

**世界变换**（local = T · R · S · T(-anchor)，再左乘父级世界变换）：

```cpp
Mat3 Layer::localTransform(FrameTime t) const {
    return Mat3::translate(transform.position.evaluate(t))
         * Mat3::rotate(transform.rotation.evaluate(t))
         * Mat3::scale(transform.scale.evaluate(t))
         * Mat3::translate(-transform.anchorPoint.evaluate(t));
}

Mat3 Layer::worldTransform(FrameTime t, const Document& doc) const {
    Mat3 local = localTransform(t);
    if (parentId.isValid())
        return doc.entityIndex().findLayer(parentId)->worldTransform(t, doc) * local;
    return local;
}
```

> ⚠️ **防环**：`setParent()` 必须沿父链检测环路并拒绝会形成环的操作。

### 3.4 LayerContent（多态）

```cpp
enum class ImageScaleMode : uint8_t {
    None = 0,
    Stretch = 1,
    LetterBox = 2,  // 默认（对齐 PAGScaleMode）
    Zoom = 3,
};

class ShapeContent : public LayerContent {
    std::unique_ptr<ShapeElement> geometry;  // 单几何：Path / Rect / Ellipse / TrimPath
};
class ImageContent : public LayerContent {
    EntityId assetId;                          // 无效 = 未绑定
    Animatable<Vec2> size{Vec2{200, 200}};     // 容器尺寸（可关键帧）；绑定 asset 不自动改
    ImageScaleMode scaleMode = ImageScaleMode::LetterBox;
};
class TextContent : public LayerContent {
    Animatable<std::string> text;
    std::string fontFamily;
    Animatable<float> fontSize{24};
    // ...
};
class PrecompContent : public LayerContent {
    EntityId compositionId;                 // 引用另一个 Composition
};
```

新建空 Image 层：未绑定 asset、`size` 静态 `200×200`、`anchorPoint = (100,100)`、`position` = 合成中心。Inspector 可「重置为源尺寸」。拖角可在「容器 | 缩放」间切换（单选 Image 时）：容器模式写 `image.size`，缩放模式写 `transform.scale`。

### 3.5 Shape 模型

每个 Shape Layer 持有**一个**几何（`ShapeContent::geometry`）。Fill/Stroke 在 `Layer::styles`。
需要共享 transform 的多几何用 `LayerType::Group` + `parentId` 组织，不在形状树内嵌套。

```cpp
class ShapePath : public ShapeElement {
    Animatable<BezierPath> path;            // 整条路径作为可动画值（layer 局部）
};
class ShapeRect    : public ShapeElement { /* position, size, cornerRadius */ };
class ShapeEllipse : public ShapeElement { /* position, size */ };
class ShapeTrimPath : public ShapeElement { /* start, end, offset */ };
// Fill / Stroke → LayerStyle（挂在 Layer::styles）
// 组变换 → LayerType::Group（NullContent）+ parentId
```

## 4. Animatable\<T\>——可动画属性

模型的核心抽象：任何属性要么是静态值，要么是一串关键帧。

```cpp
template<typename T>
class Animatable {
public:
    explicit Animatable(T staticValue);

    // 关键帧操作（只允许由 Command 调用，UI 不直接改模型）
    void addKeyframe(Keyframe<T> kf);       // 按 time 有序插入
    void removeKeyframe(FrameTime time);
    void updateKeyframe(FrameTime time, const Keyframe<T>& kf);
    void clearKeyframes();                  // 退回静态值

    T evaluate(FrameTime t) const;          // 见 timeline-evaluation.md

    bool isAnimated() const { return !keyframes_.empty(); }
    const T& staticValue() const;
    const std::vector<Keyframe<T>>& keyframes() const;

private:
    T value_;                               // 静态值（!isAnimated 时使用）
    std::vector<Keyframe<T>> keyframes_;    // 按 time 升序
};

template<typename T>
struct Keyframe {
    FrameTime time;
    T value;                                // 此关键帧处的值
    Easing easing = Easing::Linear();       // 到下一关键帧的插值方式
    std::optional<Vec2> spatialInTangent;   // 空间手柄（仅 Vec2 类型使用）
    std::optional<Vec2> spatialOutTangent;
};

struct Easing {
    enum class Type { Linear, Bezier, Hold };
    Type type = Type::Linear;
    float inX = 0, inY = 0, outX = 1, outY = 1;  // Bezier 控制点 (0,0)→(1,1)

    static Easing Linear();
    static Easing Hold();
    static Easing Bezier(float ix, float iy, float ox, float oy);
    static Easing EaseIn();   // Bezier(0.42, 0, 1, 1)
    static Easing EaseOut();  // Bezier(0, 0, 0.58, 1)
};
```

**插值策略**通过 trait 注入，`Animatable<T>` 不关心 T 的细节：

```cpp
template<typename T> struct Interpolator { static T lerp(const T& a, const T& b, float t); };

template<> struct Interpolator<float> { /* a + (b-a)*t */ };
template<> struct Interpolator<Vec2>  { /* 逐分量 */ };
template<> struct Interpolator<Color> { /* 线性 RGB 逐通道 */ };
template<> struct Interpolator<BezierPath> {
    // 逐顶点插值；要求两路径顶点数一致
    // M1 强制顶点数一致，M2 实现自动顶点插入匹配
};
```

## 5. Undo/Redo——Command 模式

### 5.1 选型

| 方案 | 结论 |
|---|---|
| **Command 模式** | ✅ 采用：内存高效，可合并连续操作，可组合 |
| 不可变快照 | ❌ 拖拽每帧一个快照，内存爆炸 |
| 操作日志/CRDT | ❌ 单机工具不需要协作，过度设计 |

### 5.2 接口

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Document& doc) = 0;   // 首次执行 + redo
    virtual void undo(Document& doc) = 0;
    virtual bool mergeWith(const Command& other) { return false; }  // 合并连续操作
    virtual std::string describe() const = 0;  // "Move Keyframe"
};

// 复合命令：多原子操作作为一个 undo 单元（execute 顺序、undo 逆序）
class CompositeCommand : public Command {
    std::vector<std::unique_ptr<Command>> commands_;
};
```

### 5.3 UndoManager

```cpp
class UndoManager {
public:
    explicit UndoManager(size_t maxHistory = 200);

    void execute(Document& doc, std::unique_ptr<Command> cmd);
    void undo(Document& doc);
    void redo(Document& doc);
    bool canUndo() const;
    bool canRedo() const;
    std::string undoDescription() const;
    void endMergeGroup();          // 鼠标抬起时调用，关闭合并窗口

private:
    std::deque<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
};
```

`execute` 的行为：

1. 执行 `cmd->execute(doc)`
2. 若栈顶命令与新命令目标相同、时间间隔 < 合并窗口（如 500ms）、且 `mergeWith()` 成功 → 合并（不压栈）。**典型场景：拖拽关键帧产生几十次 Set 命令，合并为一个 undo 单元**
3. 否则压入 undo 栈，清空 redo 栈
4. 超过 `maxHistory` 时丢弃最旧命令

### 5.4 命令寻址：EntityId + PropertyPath

命令**不持有指针**（undo 时指针可能已失效），只持有 ID 和属性路径：

```cpp
struct PropertyPath {
    EntityId entityId;      // Layer 或 ShapeElement 的 ID
    std::string path;       // "transform.position"、"styles[0].color"、"size"
};

class MoveKeyframeCommand : public Command {
    EntityId layerId_;
    PropertyPath property_;
    FrameTime oldTime_, newTime_;

    void execute(Document& doc) override {
        auto* anim = resolveAnimatable(doc, layerId_, property_);
        if (!anim) return;                       // 实体已删除 → 跳过
        auto kf = anim->removeKeyframeAt(oldTime_);
        kf.time = newTime_;
        anim->addKeyframe(kf);
    }
    void undo(Document& doc) override { /* 对称：newTime_ → oldTime_ */ }
    bool mergeWith(const Command& other) override {
        // 同目标则吸收：newTime_ = other.newTime_
    }
};
```

**M1 需要的核心命令**（8 个）：`AddLayer`、`RemoveLayer`、`MoveLayer`（改顺序）、`SetStaticValue`、`AddKeyframe`、`RemoveKeyframe`、`MoveKeyframe`、`SetEasing`。

**删除类命令的所有权**：`RemoveLayerCommand` 执行时把 `unique_ptr<Layer>` 移入命令内部；undo 时移回文档——完整恢复子结构与关键帧。

### 5.5 一致性保障（高风险点）

- 命令执行前经 `EntityIndex` 校验目标存在，不存在则跳过
- debug 构建下，每次 undo/redo 后比对 Document 序列化 hash 与预期值（测试用）
- **undo 历史不持久化**：打开文件 → 空栈；保存不影响栈。行业标准做法（AE、Figma 同）

## 6. 序列化

### 6.1 DTO 与运行时模型分离

文件格式使用独立的 DTO（纯数据结构），不直接序列化运行时类：

```cpp
struct DocumentDTO {
    int schemaVersion;                        // 当前 = 1
    std::string name;
    std::vector<CompositionDTO> compositions;
    std::vector<AssetDTO> assets;
};
struct AnimatableDTO<T> {
    std::optional<T> staticValue;             // 有值 = 静态
    std::vector<KeyframeDTO<T>> keyframes;    // 否则关键帧序列
};
```

**理由**：运行时模型可自由重构（改类名/继承）而不破坏文件格式；迁移代码只操作 JSON 不依赖模型；反序列化时可做完整性校验。

### 6.2 读写与版本迁移

```cpp
class Serializer {
public:
    static std::string serialize(const Document& doc);           // 模型 → JSON
    static std::unique_ptr<Document> deserialize(const std::string& json);
};

// 迁移链：v1 → v2 → ... → current，每步是纯函数 JSON → JSON
class SchemaMigrator {
public:
    static json migrate(json data, int fromVersion, int toVersion);
};
```

JSON 库使用 nlohmann/json（见 [architecture.md](architecture.md)）。

## 相关文档

- 关键帧如何求值：[timeline-evaluation.md](timeline-evaluation.md)
- 模型如何变成画面：[rendering.md](rendering.md)
