# 渲染抽象与导出

Core 层不含任何渲染后端。本文档定义：动画数据如何变成"绘制指令"，以及渲染器/导出器如何对接。这是"一份数据对接多种渲染器"的关键边界。

## 1. 求值流水线

```
FrameTime t
    │
    ▼
┌──────────────────┐
│ SceneEvaluator   │  遍历 Composition 全部图层，递归求值所有 Animatable<T>，
│ → SceneState     │  计算世界变换、父子继承、图层可见性/时间范围裁剪
└────────┬─────────┘
         ▼
┌──────────────────┐
│ CommandBuilder   │  扁平化为绘制指令序列（每层 ConcatTransform、
│ → DrawCommandList│  压入 save/restore、opacity、clip；path 保持 layer 局部）
└────────┬─────────┘
         ▼
┌──────────────────┐
│ RenderAdapter    │  Metal 实时预览 / CoreGraphics 离屏 / 未来的 GL、Vulkan
│ 或 Exporter      │  Lottie 导出（见 §5，不走此路径）
└──────────────────┘
```

**SceneState 是不可变的值快照**——求值完成后与 Document 完全脱钩，可安全跨线程传递（见 §6）。

## 2. SceneState

```cpp
struct EvaluatedShapeItem {
    BezierPath path;        // layer 局部坐标；世界位置由 EvaluatedLayer::worldTransform 表达
    Paint paint;            // 纯色（M3 首版），后续扩展渐变
    bool isStroke;
    float strokeWidth;
    LineCap cap;
    LineJoin join;
};

struct EvaluatedLayer {
    EntityId id;            // 保留 ID 供 UI 命中检测
    Mat3 worldTransform;    // CommandBuilder 发成 ConcatTransform
    float opacity;          // 已继承父级 opacity
    BlendMode blendMode;
    std::vector<EvaluatedShapeItem> shapeItems;
    std::optional<EvaluatedImageItem> imageItem;
    std::optional<EvaluatedTextItem> textItem;  // 与 shape / image 互斥
};

// 文本层求值只带原始字段；换行 / 缩字在 adapter/textlayout，不进入 Core。
// Fill/Stroke 按 Layer::styles 顺序收集为 styles（各自带 blend）；缺省黑 Fill。
struct TextDrawStyle {
    Color color;
    BlendMode blendMode;
    bool isStroke;
    float strokeWidth;
};

struct EvaluatedTextItem {
    std::string text;
    float fontSize;
    Vec2 containerSize;
    bool autoHeight;
    TextAlign align;
    std::string fontFamily;
    std::string fontStyle;
    std::vector<TextDrawStyle> styles;
    Vec2 hitSize;  // Core 初值为 containerSize；Bridge 可在 autoHeight 时写回测高
};

struct SceneState {
    std::vector<EvaluatedLayer> layers;   // 按渲染顺序，底 → 顶
    int viewportWidth, viewportHeight;
    Color backgroundColor;
};
```

## 3. DrawCommand 与 RenderAdapter

### 3.1 指令集

```cpp
enum class DrawCommandType {
    Save, Restore, ConcatTransform, SetOpacity, SetBlendMode,
    DrawPath,     // 填充
    StrokePath,   // 描边
    DrawImage,    // 图片层：path + container/intrinsic size + ImageScaleMode
    DrawText,     // 文本层：自包含排版输入（见 text* 字段）
    ClipPath,
    BeginLayer, EndLayer,   // 离屏组（mask / track matte）
    BeginMask, EndMask,     // coverage 记录与应用
    DrawMaskPath,           // PathCoverage 内单条 mask
};

struct DrawCommand {
    DrawCommandType type;
    Mat3 transform;         // ConcatTransform
    float opacity;          // SetOpacity
    BlendMode blendMode;    // SetBlendMode
    BezierPath path;        // DrawPath / StrokePath / ClipPath
    Paint paint;
    float strokeWidth;
    LineCap cap; LineJoin join;
    FillRule fillRule;
    // DrawImage
    std::string imagePath;
    Vec2 imageContainerSize;
    Vec2 imageIntrinsicSize;
    ImageScaleMode imageScaleMode;
    // DrawText（排版由 adapter/textlayout + tgfx GlyphMetrics 完成）
    std::string text;
    float textFontSize;
    Vec2 textContainerSize;
    bool textAutoHeight;
    TextAlign textAlign;
    std::string textFontFamily;
    std::string textFontStyle;
    std::vector<TextDrawStyle> textStyles;
};

using DrawCommandList = std::vector<DrawCommand>;
```

### 3.2 适配器接口（immediate-mode，栈式状态）

```cpp
class RenderAdapter {
public:
    virtual ~RenderAdapter() = default;

    virtual void beginFrame(int width, int height, Color clearColor) = 0;
    virtual void endFrame() = 0;

    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void concatTransform(const Mat3& matrix) = 0;
    virtual void setOpacity(float opacity) = 0;
    virtual void setBlendMode(BlendMode mode) = 0;

    virtual void drawPath(const BezierPath& path, const Paint& paint) = 0;
    virtual void strokePath(const BezierPath& path, const Paint& paint,
                            float width, LineCap cap, LineJoin join) = 0;
    virtual void drawImage(const std::string& path, Vec2 container, Vec2 intrinsic,
                           ImageScaleMode mode) = 0;
    virtual void drawText(const std::string& text, float fontSize, Vec2 containerSize,
                          bool autoHeight, TextAlign align, const std::string& fontFamily,
                          const std::string& fontStyle,
                          const std::vector<TextDrawStyle>& styles) = 0;
    virtual void clipPath(const BezierPath& path, FillRule rule) = 0;
};

// 播放器：把指令序列喂给任意适配器
void playCommands(const DrawCommandList& cmds, RenderAdapter& r);
```

**为什么是 immediate-mode 而非 retained（场景图）**：动画工具每帧内容都在变，retained 模式的缓存价值很低；栈式接口与 Metal / CoreGraphics / Skia 的 API 形态天然对齐。Rive 的 Renderer 接口同此思路，已验证可行。

### 3.3 已实现后端：tgfx（Metal）

`adapter/tgfx/TgfxRenderAdapter`（`motionstudio_tgfx_adapter` 静态库，仅 Apple 平台）基于 tgfx 2D 渲染引擎的 Metal 后端实现该接口：渲染到离屏纹理，`ReadPixels` 回读 RGBA8 像素，用于快照测试与序列帧导出。文本绘制走 `TgfxGlyphMetrics` + `adapter/textlayout`（换行 / 对齐 / 固定高缩字），再按行 `TextBlob` 依 `styles` 顺序填充与描边（各 style 自带 blend）；`autoHeight == false` 时 clip 到容器。字体解析：`Typeface::MakeFromName(fontFamily, fontStyle)` → PingFang SC → Helvetica。Core 层不依赖 tgfx，后续增加其他后端（CoreGraphics/OpenGL）不影响现有代码。

## 4. 上屏适配器（应用层预览）

`TgfxOnScreenAdapter`（`adapter/tgfx/`，与离屏适配器同基类 `TgfxCanvasAdapter`）**不属于 Core 层**，基于 tgfx 的 `MetalWindow`：

- 渲染目标：SwiftUI 画布里的 `MTKView`；`tgfx::MetalWindow::MakeFrom(MTKView*)` 包装其 drawable，`beginFrame` 取 window surface，`endFrame` 提交并 present
- 每帧流程：播放头时间 → `ms_canvas_draw_frame`（桥接层内部完成 `SceneEvaluator::Evaluate` → `autoHeight` 文本测高写回 `hitSize` → `BuildCommands` → `PlayCommands`）→ tgfx 光栅化 → present。**DrawCommand 不越过 C ABI 边界**
- **路径渲染**：由 tgfx 直接光栅化贝塞尔路径（GPU），应用层无需自行细分三角形
- 播放驱动：`CADisplayLink`（macOS 14+ / iOS 均支持）按合成帧率推进播放头；暂停时仅在播放头/模型变化时按需重绘（MTKView paused + setNeedsDisplay）

桥接层为此提供 `ms_canvas_create` / `ms_canvas_draw_frame` / `ms_canvas_destroy` 三个函数（见 [architecture.md](architecture.md)）。

## 5. 导出器边界

**Lottie 导出不走 DrawCommand 路径**。原因：DrawCommand 是单帧烘焙结果，丢失了关键帧结构；而 Lottie 文件格式本身描述的就是关键帧动画，必须从 Document 模型直接转换：

```cpp
class LottieExporter {
public:
    // Document 模型 → Lottie JSON（保留关键帧/缓动，不烘焙）
    static nlohmann::json exportToLottie(const Document& doc,
                                         EntityId compositionId);
};
```

模型映射（Motion Studio → Lottie）：Composition → `comp`，Layer → `layer`，Transform 五属性 → `ks`，Shape 元素 → `it` 数组，Easing → `o/i` 贝塞尔手柄。

**一致性验证**：导出后用 `SceneEvaluator` 对原场景逐帧求值渲染，与 Lottie 参考渲染器（lottie-web）的输出做像素对比，验收标准为视觉一致性 > 95%。

**序列帧 PNG 导出**：走渲染路径——Metal offscreen（`MTLTexture` render target）逐帧渲染 `SceneState` 后回读像素编码 PNG。

## 6. 线程模型

- **M3 首版**：UI 与求值都在主线程同步执行（简单优先，场景规模小时足够）
- **后续优化**：双缓冲——主线程修改 Document 并异步求值下一帧的 `SceneState`，渲染线程只读上一帧的快照。`SceneState` 是纯值类型（可拷贝/移动），天然适合跨线程传递；Document 本身始终只在主线程访问，无需加锁

## 相关文档

- 求值细节：[timeline-evaluation.md](timeline-evaluation.md)
- 桥接层 C ABI：[architecture.md](architecture.md)
