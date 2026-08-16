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
 ├─ shaders[]                  （过程色定义库；包内独立 shader.json）
 └─ Composition[]              （多个合成，支持互相引用 = 预合成）
     └─ Layer[]                （有序，index 0 = 最底层，向上渲染）
         ├─ Transform          （5 个可动画属性，必有）
         ├─ styles[]           （Fill / Stroke；Color / Gradient / Shader）
         ├─ effects[]          （后处理：BrightnessContrast / GaussianBlur）
         ├─ layerStyles[]      （图层装饰：DropShadow / OuterGlow / Stroke）
         └─ LayerContent       （多态：Shape / Image / Text / Null / Precomp）
             └─ ShapeElement[] （Shape 类型时：Path / Rect / Ellipse / TrimPath ...）
```

**所有权是严格的树**（`unique_ptr` 持有），**引用全部走 `EntityId`**。

### 3.1 Document 与 EntityIndex

```cpp
class Document {
public:
    EntityId id;
    std::string name;
    std::vector<std::unique_ptr<Composition>> compositions;
    std::vector<Asset> assets;                 // 图片等文档级资源
    std::vector<ShaderDefinition> shaders;     // 过程色定义；不进 EntityIndex

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

enum class AssetType { Image };

struct Asset {
    EntityId id;
    AssetType type = AssetType::Image;
    std::string name;
    std::string path;   // 相对 projectRoot，如 "assets/photo.png"
    int width = 0;      // 源图像素宽（导入时写入）
    int height = 0;
};

struct ShaderUniformDecl {
    std::string name;
    UniformFormat format;          // Float / Float2 / Float3 / Float4 / Color（Color≠Float4）
    int count = 1;
    bool animatable = true;        // false → Realign 拍平关键帧；Inspector 无钻石
    float defaultFloat = 0;
    Vec2 defaultFloat2{0, 0};
    Vec3 defaultFloat3{0, 0, 0};
    Vec4 defaultFloat4{0, 0, 0, 0};
    Color defaultColor{1, 1, 1, 1}; // 仅新建绑定 / Realign 新增项使用
};

struct ShaderDefinition {
    EntityId id;
    std::string name;
    std::string mainImage;                 // 用户 mainImage(uv) 体
    std::vector<ShaderUniformDecl> uniforms;
};
```

过程色定义由 `Serializer::serializeShaders` / `deserializeShaders` 读写包内 `shader.json`（**独立** `schemaVersion`，当前 = 1）；`document.json` **不**内嵌源码。查找用 `FindShader` 扫描 `Document.shaders`（v1 不扩展 EntityIndex）。

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
    Animatable<float> expansion{0};  // 扩张/收缩 px（可负）；负值=mask 内缩为更小实心（AE/PAG）。单 mask 镂空：Add + 负 expansion + inverted
};
```

`Layer::masks` 仍按数组序求值（小 index 先应用）。Inspector 列表**倒序**显示（`+` append，最上 = 最新）；可用 `MoveMaskCommand` 调序；「Mask N」按视觉序编号。

**Transform**——每个 Layer 必有，含 5 个可动画属性：

```cpp
struct Transform {
    Animatable<Vec2>  anchorPoint{{0, 0}};
    Animatable<Vec2>  position{{0, 0}};
    Animatable<Vec2>  scale{{1, 1}};
    Animatable<float> rotation{0};      // 度；正角度对齐 AE（屏幕 Y 向下时视觉为顺时针）
    Animatable<float> opacity{1};       // 0.0 ~ 1.0
};
```

> 从 Figma Design / Motion 手抄旋转与时间时见 [`figma-to-motionstudio.md`](figma-to-motionstudio.md)。

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

#### Position：数据层 vs UI 层

| 层 | 含义 | 落点 |
|---|---|---|
| **数据层（存储 / Bridge / 导出）** | AE 语义：`position` = **锚点**在父空间中的位置 | `Transform.position`、序列化、PAG、FreeTransform 等内部写回 |
| **UI 层（Inspector / 关键帧数值）** | 布局语义：数字 = 局部 AABB **左上角**（`localBounds.min`）在父空间中的位置 | App：`LayoutPosition` + `MotionDocumentCore.evaluateLayoutPosition` / `writeLayoutPosition` |

换算（不改存储模型；`anchor` / `scale` / `rotation` / `localBounds` 取当前帧）：

```
offset = R · S · (anchor − bounds.min)
layoutPosition  = storedPosition − offset   // 显示
storedPosition  = layoutPosition + offset    // 写入
```

约束：

- `anchorPoint` 仍是**局部坐标**（未做左上角换算）。新建 Rect/Ellipse 几何居中于局部原点时，锚点默认 `(0,0)`（中心）属正常；Image 等「内容从 (0,0) 起算」的层，中心锚点约为 `(w/2, h/2)`。
- `ShapeProperty.position`（形状内部偏移）与 `transform.position` 无关，不走上述 UI 换算。
- 无 `localBounds` 或空 rect 时 `offset = 0`，UI 退回显示存储 `position`。
- 画布运动路径描线、几何拖拽补偿继续使用**存储坐标**。

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
    Animatable<std::string> text{std::string{"Text"}};
    std::string fontFamily{"PingFang SC"};         // 系统字体族名
    std::string fontStyle{};                       // 族内 style 名（如 Bold）；空 = 默认/Regular
    float fontSize = 48;                           // 静态；框模式下为字号上限（可 shrink）
    Vec2 size{400, 120};                           // 静态；仅框文本排版/选中用；点文本忽略
    bool boxTextMode = false;                      // false：点文本；true：PAG 框文本（换行+shrink）
    TextAlign align = TextAlign::Left;             // Left / Center / Right
};
class PrecompContent : public LayerContent {
    EntityId compositionId;                 // 引用另一个 Composition
};
```

枚举：`enum class TextAlign : uint8_t { Left, Center, Right };`

新建空 Image 层：未绑定 asset、`size` 静态 `200×200`、`anchorPoint = (100,100)`、`position` = 合成中心。Inspector 可「重置为源尺寸」。选中框手柄一律改 `image.size`（可补偿 position/anchor）；`transform.scale` 仅属性面板可改。

新建 Text 层：文案 `"Text"`，默认**点文本**（`boxTextMode = false`），`size = 400×120`（占位，点文本排版忽略），`align = Left`，`fontFamily = "PingFang SC"`，`fontStyle` 空，`anchorPoint` = 当前字形包围盒中心，`position` = 合成中心，并附带黑色 Fill。绘制合成固定为先全部 Fill、再全部 Stroke（同类内部按 `styles[]` 出现顺序；各自 blend）；`styles[i]` 仍索引磁盘数组。Inspector 的 Fills/Strokes 列表按同类 index **倒序**显示（最上 = 绘制最顶）；可用 `MoveLayerStyleCommand` 在同类连续块内调序，禁止跨 Fill/Stroke。Stroke 的 Position / Trim 对文本无效。

点文本：选中/hit bounds 为字形测量；无角/边 resize 手柄。框文本：bounds = `size`；Inspector/手柄可改 `size`（`anchor' = (ax·w1/w0, ay·h1/h0)`，position 可补偿）。点→框时用当前字形测量写入 `size`。

PropertyPath：仅 `content.text` 可动画。`fontSize` / `size` / `fontFamily` / `fontStyle` / `boxTextMode` / `align` 经专用 undo 命令与 bridge API。

### 3.5 Shape 模型

每个 Shape Layer 持有**一个**几何（`ShapeContent::geometry`）。Fill/Stroke 在 `Layer::styles`。
`Layer::effects[]` 是整层光栅结果的后处理链（BrightnessContrast / GaussianBlur），与 `styles[]` 并列、不参与内容绘制。求值时 `snapshot(time)` bake 成静态值；`!enabled` 或恒等参数丢掉。PropertyPath：`effects[i].brightness|contrast|blurriness`。`enabled` / `repeatEdgePixels` 走专用命令。Precomp / Group 上的 effect 可存盘，求值与绘制忽略。PAG 导出：Shape / Image / Text 写入 `pag::BrightnessContrastEffect` / `pag::FastBlurEffect`（`!enabled` 跳过；恒等仍导出；拆层包 Precomp 后挂 host；`_bmp` 不重复挂）。

`Layer::layerStyles[]` 是 effect 之后的图层装饰（Drop Shadow / Outer Glow / Stroke），基类 `LayerFx`，不要和路径 `styles[]` / `MS_STYLE` 混用。求值同样 `snapshot(time)`；`!enabled` 或恒等参数丢掉。PropertyPath：`layerStyles[i].color|opacity|size|angle|distance|spread|range`。`enabled` / `blendMode` / Stroke `position` 走专用命令。仅 Shape / Image / Text 求值与绘制；Precomp / Group 上可存盘但忽略。PAG 导出：写入 `pag::DropShadowStyle` / `pag::OuterGlowStyle` / `pag::StrokeStyle`（`StrokePosition` 显式映射，序不一致；`!enabled` 跳过；拆层包 Precomp 后挂 host；`_bmp` 不重复挂）。
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

**Fill / Stroke 绘制源（Color / Gradient / Shader 三态共存）**：

```cpp
enum class StylePaintMode : uint8_t { Color = 0, Shader = 1, Gradient = 2 };
enum class GradientType : uint8_t { Linear = 0, Radial = 1, Conic = 2, Diamond = 3 };

struct GradientStop {
    Animatable<Color> color;
    Animatable<float> position;  // [0,1]，首尾须为 0/1
};

struct GradientPaint {
    GradientType type = Linear;
    Animatable<Vec2> start;       // AABB 左上角空间 px；(0,0)=层局部 bounds.min；求值 +min → shape 空间
    Animatable<Vec2> end;         // 同上；Radial/Diamond：radius = |end-start|（求值后）
    Animatable<float> startAngle; // Conic
    Animatable<float> endAngle;   // Conic
    std::vector<GradientStop> stops;  // N≥2
};

class FillStyle : public LayerStyle {
    StylePaintMode paintMode = StylePaintMode::Color;
    Animatable<Color> color;               // 三态字段共存；仅当前 kind 参与绘制
    GradientPaint gradient;                // Gradient 模式；切走不清空
    EntityId shaderId;                     // Shader 模式；须 ∈ Document.shaders
    ShaderUniformValues uniformValues;     // Shader 模式；按 name 对齐 scheme
    FillRule fillRule = NonZero;
    BlendMode blendMode = Normal;
};
// StrokeStyle 同构（另含 width / cap / join / trim / …）
```

`document.json` 中 `paintMode` / `gradient` / `shaderId` / `uniformValues` 为**可选**字段：缺省 → `Color`（与旧文档兼容）；`schemaVersion` **保持 1**。非当前 kind 的字段仍可持久化（切回保留配置）。合并 `shader.json` 后可用 `ValidateShaderReferences` 校验引用。

**不变式 / 切换：** `SetStylePaintModeCommand` **只改 kind**，不清空 `color` / `gradient` / `shader*`；切到 Gradient/Shader 时目标路不可用则懒初始化。求值只读当前 kind；Gradient 非法（stops 少于 2、positions 不合法、Radial/Diamond radius≤0）或 Shader 解析失败 → **跳过该 style 绘制**（不回退其它 kind）。Gradient undo：`SetGradientTypeCommand` / `AddGradientStopCommand` / `RemoveGradientStopCommand`。

**PropertyPath（Gradient）：** `styles[i].gradient.start|end|startAngle|endAngle`、`styles[i].gradient.stops[j].color|position`。

**实例 uniform 值（可扩展 kind）：**

```cpp
enum class ShaderUniformValueKind : uint8_t {
    AnimFloat = 0,   // ← UniformFormat::Float
    AnimFloat2 = 1,  // ← Float2
    AnimFloat3 = 2,  // ← Float3（Animatable<Vec3>）
    AnimColor = 3,   // ← UniformFormat::Color（Animatable<Color>）
    AnimFloat4 = 4,  // ← UniformFormat::Float4（Animatable<Vec4>；非颜色）
};

struct ShaderUniformValue {
    std::string name;
    ShaderUniformValueKind kind;
    Animatable<float> floatValue;
    Animatable<Vec2> float2Value;
    Animatable<Vec3> float3Value;
    Animatable<Color> colorValue;
    Animatable<Vec4> float4Value;
};

struct ShaderUniformValues { std::vector<ShaderUniformValue> entries; };
```

`KindForFormat`：`Float4 → AnimFloat4`，`Color → AnimColor`（旧 `"format":"float4"` **不**自动迁移为 color）。辅助：`MakeDefaultUniformValues` / `RealignUniformValues`（`animatable==false` 时 `evaluate(0)` 后 `clearKeyframes`）；改 scheme 后由 `UpdateShaderDefinitionCommand` 对所有引用样式 Realign。`PropertyPath`：`styles[0].uniformValues.<name>`（仅 Shader 模式）。内置 Shadertoy uniform（`iTime` 等）不进 scheme。设计细节见 [color-source Core 存储 spec](superpowers/specs/2026-08-07-color-source-core-storage-design.md) 与 [shader uniform defaults design](superpowers/specs/2026-08-10-shader-uniform-defaults-animatable-design.md)；绘制仍在 adapter，见 [color-source-effect.md](color-source-effect.md)。

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
template<> struct Interpolator<Vec3>  { /* 逐分量 */ };
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
    std::string path;       // "transform.position"、"styles[0].color"、
                            // "styles[0].gradient.start"、
                            // "styles[0].uniformValues.rippleCount"、"size"
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

过程色 / 渐变相关命令：`AddShaderCommand`、`RemoveShaderCommand`（仍被引用则跳过删除）、`UpdateShaderDefinitionCommand`（改 name/mainImage/uniforms 并对引用 Realign）、`SetStylePaintModeCommand`、`SetGradientTypeCommand`、`AddGradientStopCommand`、`RemoveGradientStopCommand`。

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
    int schemaVersion;                        // 当前 = 1（过程色字段为可选，不升版）
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

工程包还可含独立的 `shader.json`（DTO `SHADER_SCHEMA_VERSION = 1`，与 `document.json` 分开演进），由 `Serializer::serializeShaders` / `deserializeShaders` 处理；App 打开时合并进 `Document.shaders` 后再 `ValidateShaderReferences`（包读写属后续 App 计划）。示例：

```json
{
  "schemaVersion": 1,
  "shaders": [
    {
      "id": 123456789,
      "name": "Ripple",
      "mainImage": "vec4 mainImage(vec2 uv){ return vec4(uv,0.0,1.0); }",
      "uniforms": [
        { "name": "rippleCount", "format": "float", "count": 1, "animatable": true, "default": 4 },
        { "name": "tint", "format": "color", "count": 1, "animatable": false, "default": "#FFFFFFFF" }
      ]
    }
  ]
}
```

### 6.2 读写与版本迁移

```cpp
class Serializer {
public:
    static std::string serialize(const Document& doc);           // document.json
    static std::unique_ptr<Document> deserialize(const std::string& json);
    static std::string serializeShaders(const Document& doc);    // shader.json
    static Expected<std::vector<ShaderDefinition>, std::string>
        deserializeShaders(const std::string& json);
};

// 迁移链：v1 → v2 → ... → current，每步是纯函数 JSON → JSON
// document schema 仍为 1 时 SchemaMigrator 无需为过程色升版
class SchemaMigrator {
public:
    static json migrate(json data, int fromVersion, int toVersion);
};
```

JSON 库使用 nlohmann/json（见 [architecture.md](architecture.md)）。

## 相关文档

- 关键帧如何求值：[timeline-evaluation.md](timeline-evaluation.md)
- 模型如何变成画面：[rendering.md](rendering.md)
- 过程色 Core 存储设计：[superpowers/specs/2026-08-07-color-source-core-storage-design.md](superpowers/specs/2026-08-07-color-source-core-storage-design.md)
- adapter 过程色绘制：[color-source-effect.md](color-source-effect.md)
