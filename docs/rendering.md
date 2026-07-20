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
│ CommandBuilder   │  扁平化为绘制指令序列（展开 ShapeGroup、
│ → DrawCommandList│  压入 save/restore、transform、opacity、clip）
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
    BezierPath path;        // 已应用所有 shape transform 的世界空间路径
    Paint paint;            // 纯色（M3 首版），后续扩展渐变
    bool isStroke;
    float strokeWidth;
    LineCap cap;
    LineJoin join;
};

struct EvaluatedLayer {
    EntityId id;            // 保留 ID 供 UI 命中检测
    Mat3 worldTransform;
    float opacity;          // 已继承父级 opacity
    BlendMode blendMode;
    std::vector<EvaluatedShapeItem> shapeItems;  // ShapeGroup 已展开
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
    ClipPath,
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
    virtual void clipPath(const BezierPath& path, FillRule rule) = 0;
};

// 播放器：把指令序列喂给任意适配器
void playCommands(const DrawCommandList& cmds, RenderAdapter& r);
```

**为什么是 immediate-mode 而非 retained（场景图）**：动画工具每帧内容都在变，retained 模式的缓存价值很低；栈式接口与 Metal / CoreGraphics / Skia 的 API 形态天然对齐。Rive 的 Renderer 接口同此思路，已验证可行。

## 4. Metal 适配器（macOS 应用层）

`MetalRenderAdapter` 位于 `app/macos/`，**不属于 Core 层**：

- 渲染目标：`CanvasView`（NSView）内嵌 `CAMetalLayer`，由 `CVDisplayLink`（macOS 14+ 可用 `CADisplayLink`）驱动渲染循环
- 每帧流程：播放头时间 → `ms_document_evaluate` → DrawCommandList → `MetalRenderAdapter` 转 Metal draw call → `present(drawable)`
- **路径渲染**：Metal 不能直接画贝塞尔。第一版采用 **CPU 细分为三角形**（耳切/单调多边形剖分 + 描边转轮廓）；GPU 细分着色器是后续性能优化项，不在首版范围

桥接层为此提供 `ms_scene_get_command*` 系列 C ABI 函数（见 [architecture.md](architecture.md)）。

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
