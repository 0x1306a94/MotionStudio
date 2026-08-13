# PAG RuntimeFilter 架构 + BrightnessContrast — 设计说明

日期：2026-08-13  
状态：待实现  
范围：adapter 侧图层后处理滤镜基类 + 移植一个 PAG effect；**不**改 Core / DrawCommand / App UI

相关：[`docs/color-source-effect.md`](../../color-source-effect.md)（过程色 fill，职责不同）、libpag `third_party/libpag/src/rendering/filters/`

## 目标

1. 在 `adapter/tgfx/src/effects` 落地 PAG 同构的 `RuntimeFilter` 基类（`tgfx::RuntimeEffect`：输入纹理 → 输出纹理）。
2. 移植 **BrightnessContrast** 作为第一个滤镜，shader 与 uniform 映射与 PAG 一致。
3. 用 `tgfx_adapter_test` 像素测试验证；预览/文档里暂时看不到效果。

## 非目标

- Core `Layer::effects`、SceneEvaluator、DrawCommand、`TgfxCanvasAdapter::endLayer` 接线
- App Inspector / 时间轴 UI
- PAG 导出写入 Effect 块
- PAG Layer Style（DropShadow / OuterGlow 等；`filters/layerstyle/` 另一套管线）
- 其余 PAG Effect（FastBlur / Mosaic / HueSaturation / Glow / …）
- 合并或改写 `ColorSourceEffect`（过程色 fill，不是图层后处理）
- 链接 `pag::RuntimeFilter` / `pag::RenderCache` / `pag::Effect`

## 已锁定决策

| 项 | 选择 |
|---|---|
| 做法 | 移植 PAG `RuntimeFilter` 到 `motion` 命名空间，不链 libpag 渲染内部 |
| 本阶段范围 | adapter + 像素测试（A1） |
| 第一个滤镜 | BrightnessContrast |
| 滤镜参数 | 直接传 `float brightness, float contrast`，不依赖 `pag::Effect` |
| Pipeline 缓存 | `motion::RenderCache` 按 `filterType()`（`uint32_t`）缓存 `FilterResources` |
| UBO 上传 | `RenderCache::acquireUniformSlice`（与 ColorSourceEffect 一致），不每帧 `createBuffer` |
| ColorSourceEffect | 生产代码不动；测试改用共享夹具 |
| Layer Style | 下一期 |
| 日志 | 失败返回 `false`；不引入 PAG `LOGE` |

---

## §1 职责拆分

PAG 把图层栅格成 `Image` 后链式 `makeWithFilter`。MotionStudio 本阶段只提供滤镜实现，调用方是测试（以及未来的 `endLayer`）。

| 类型 | 角色 | 生命周期 |
|---|---|---|
| `RuntimeFilter` | 基类：编 shader、建 pipeline、画输入纹理到输出 RT | 每次 Apply 新建实例（参数不同必须多实例；tgfx 按 effect 对象身份缓存离屏结果） |
| `BrightnessContrastFilter` | 两参数颜色调整；shader 从 PAG 原样移植 | 同上 |
| `RenderCache` | 重资源：按 `filterType` 共享 pipeline + sampler | 跟 tgfx `Context`；`releaseAll` 时一并丢掉 |
| `ColorSourceEffect` | 过程色 fill（无输入纹理 / Shadertoy） | **不动** |

```
input Image
    │  BrightnessContrastFilter::Apply(input, cache, brightness, contrast, &offset)
    ▼
ImageFilter::Runtime(filter) → makeWithFilter
    │  tgfx 建离屏 RT，调 RuntimeFilter::onDraw
    ▼
filtered Image
```

后续接线（不在本阶段）：`endLayer` 把 isolation picture 转 Image，再按求值后的 effect 列表链式 Apply。

---

## §2 文件布局

CMake 已 `add_files_by_extension` 扫描 `adapter/tgfx/src` 与 `tests`，新文件自动进库。

```
adapter/tgfx/src/effects/
  ColorSourceEffect.{h,cpp}          # 不动
  Uniform.{h,cpp} / UniformData.*    # 不动
  RuntimeFilter.{h,cpp}              # 新建：基类 + 默认 VS / onDraw / pipeline
  BrightnessContrastFilter.{h,cpp}   # 新建：FS + uniforms + Apply

adapter/tgfx/src/RenderCache.{h,cpp}  # 增量：filterResourcesMap

adapter/tgfx/tests/BrightnessContrastFilterTest.mm   # 新建
docs/pag-runtime-filter.md                           # 新建（调用约定，对标 color-source-effect.md）
```

不单独拆 `FilterHelper`：`ToVertexPoint` / `ToTexturePoint` 作为 `RuntimeFilter.cpp` 匿名命名空间函数。

---

## §3 关键接口

### 3.1 `RuntimeFilter`

```cpp
namespace motion {

#define DEFINE_RUNTIME_FILTER_TYPE              \
    uint32_t filterType() const override {      \
        static const uint32_t type = NextRuntimeFilterType(); \
        return type;                            \
    }

class RuntimeFilter : public tgfx::RuntimeEffect {
  public:
    explicit RuntimeFilter(RenderCache *cache,
                           const std::vector<std::shared_ptr<tgfx::Image>> &extraInputs = {});

    tgfx::Rect filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const override;
    bool onDraw(tgfx::CommandEncoder *encoder,
                const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                std::shared_ptr<tgfx::Texture> outputTexture,
                const tgfx::Point &offset) const override;

  protected:
    RenderCache *cache = nullptr;

    virtual uint32_t filterType() const = 0;
    virtual tgfx::Rect filterBounds(const tgfx::Rect &srcRect) const;
    virtual std::string onBuildVertexShader() const;
    virtual std::string onBuildFragmentShader() const;
    virtual int sampleCount() const;
    virtual std::vector<tgfx::Attribute> vertexAttributes() const;
    virtual std::vector<tgfx::BindingEntry> uniformBlocks() const;
    virtual std::vector<tgfx::BindingEntry> textureSamplers() const;
    virtual std::vector<float> computeVertices(const tgfx::Texture *source,
                                               const tgfx::Texture *target,
                                               const tgfx::Point &offset) const;
    virtual size_t vertexCount() const;
    virtual void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                                  const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                                  const tgfx::Point &offset) const;
    virtual void onConfigurePipeline(tgfx::RenderPipelineDescriptor *descriptor) const;
    virtual std::unique_ptr<FilterResources> onCreateFilterResources() const;
    virtual void onConfigureRenderPass(tgfx::RenderPassDescriptor *desc, FilterResources *resources,
                                       tgfx::GPU *gpu,
                                       const std::shared_ptr<tgfx::Texture> &outputTexture) const;

    FilterResources *getFilterResources(tgfx::GPU *gpu) const;
};

uint32_t NextRuntimeFilterType();

}  // namespace motion
```

行为对齐 PAG `RuntimeFilter.cpp`：

- 默认 VS：`aPosition` + `aTextureCoord` → `vertexColor`；`TriangleStrip` 4 顶点。
- `#version` 前缀与 ColorSourceEffect 相同：desktop `#version 150`，否则 `#version 300 es`（`gpu->info()->version` 是否含 `"OpenGL ES"`）。
- `onDraw`：取 pipeline → beginRenderPass(Clear) → setTexture(0, input, sampler) → `onUpdateUniforms` → draw。
- `cache == nullptr` 或 shader 编译失败：`onDraw` 返回 `false`（tgfx 则跳过该 filter）。
- `NextRuntimeFilterType()`：进程内递增 `uint32_t`（从 1 起），替代 PAG `UniqueID::Next()`。不放 Core。

`onUpdateUniforms` 默认空。子类写 UBO 时用 `cache->acquireUniformSlice(sizeof(Uniforms))`，再 `renderPass->setUniformBuffer(0, slice.buffer, slice.offset, size)`。

### 3.2 `RenderCache` 增量

```cpp
struct FilterResources {
    std::shared_ptr<tgfx::RenderPipeline> pipeline = nullptr;
    std::shared_ptr<tgfx::Sampler> sampler = nullptr;
};

FilterResources *findFilterResources(uint32_t type) const;
void addFilterResources(uint32_t type, std::unique_ptr<FilterResources> resources);
```

- 键：`filterType()`，与 ColorSource 的 `EntityId` 键分开。
- `releaseAll()` 同时 `filterResourcesMap_.clear()`。
- 不提供 invalidate：滤镜 shader 是编译期常量，无需按文档 id 失效。

`FilterResources` 定义在 `RenderCache.h`（与 `UniformBufferSlice` 并列）：只持 `RenderPipeline` / `Sampler` 的 `shared_ptr`，`RenderCache.h` 已有同类前向声明。`RuntimeFilter.h` include `RenderCache.h`。`RenderCache` 析构已在 cpp，`unique_ptr` 完整类型在 cpp 可见即可。

### 3.3 `BrightnessContrastFilter`

```cpp
class BrightnessContrastFilter : public RuntimeFilter {
  public:
    static std::shared_ptr<tgfx::Image> Apply(std::shared_ptr<tgfx::Image> input, RenderCache *cache,
                                              float brightness, float contrast, tgfx::Point *offset);

    BrightnessContrastFilter(RenderCache *cache, float brightness, float contrast);

  protected:
    DEFINE_RUNTIME_FILTER_TYPE
    std::string onBuildFragmentShader() const override;
    std::vector<tgfx::BindingEntry> uniformBlocks() const override;
    void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                          const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                          const tgfx::Point &offset) const override;

  private:
    float brightness = 0.f;
    float contrast = 0.f;
};
```

`Apply`：

```
if (input == nullptr) return nullptr;
auto filter = make_shared<BrightnessContrastFilter>(cache, brightness, contrast);
return input->makeWithFilter(ImageFilter::Runtime(filter), offset);
```

`filterBounds` 不覆盖（输出 = 输入）。`processVisibleAreaOnly` 语义本阶段无调用方，不建模。

---

## §4 Shader 与参数映射（与 PAG 一致）

Fragment shader 从 `third_party/libpag/src/rendering/filters/BrightnessContrastFilter.cpp` **原样复制**（HSV 路径、`EPSILON`、预乘 `rgb * a`）。

Uniform 块名 `Args`，binding 0：

```
mBrightness = brightness > 0 ? brightness / 250.f : brightness / 650.f
mContrast   = 1.0f + contrast / 300.f
```

PAG 数据层 brightness/contrast 是 AE 风格数值（约 ±100 量级）。本阶段测试直接传这些原始值，不做 Core 求值。

`0, 0` 时 shader 仍跑一遍（恒等附近）；不做 early-out。未来 Core `visibleAt` 再跳过。

---

## §5 测试

共享 Metal 测试夹具抽到 `adapter/tgfx/tests/TgfxTestGPUEnvironment.{h,mm}`（`namespace tgfx_test`），`ColorSourceEffectTest` 与 `BrightnessContrastFilterTest` 共用。头文件纯 C++（PIMPL 藏 Metal）；实现是 `.mm`。

抽出：`TgfxTestGPUEnvironment`、`SaveWebp`、`OutputPath`、`Pixel` / `PixelAt`、`ChannelDelta`、`MakeSolidImage`、`ReadCenter`。  
不抽：`MakeFivePointStar`（仅 ColorSource）、`TgfxRenderAdapterTest` 里另一套 `Pixel`（走 `TgfxRenderAdapter`，本阶段不动）。

`OutputPath` 实现放 `.mm`，`__FILE__` 仍落在 `adapter/tgfx/tests/`，输出目录还是 `tests/out/`。

文件：`adapter/tgfx/tests/BrightnessContrastFilterTest.mm`（include 共享头，不再复制夹具）。

用例：

| 用例 | 行为 |
|---|---|
| `IdentityLeavesOpaqueColorUnchanged` | 8×8 不透明纯色（如 RGB 128,64,32），`Apply(0, 0)`，中心像素 RGB 与输入差 ≤ 2 |
| `PositiveBrightnessRaisesValue` | 同输入，`brightness = 100, contrast = 0`，中心像素 luma 明显高于 identity |
| `PositiveContrastPushesAwayFromMidGrey` | 中灰 128，`contrast = 100`，通道更远离 128 |
| `SharesPipelineAcrossInstances` | 两次 `Apply` 不同参数，`findFilterResources(type)` 非空且两次指针相同 |

断言：`readPixels` RGBA8；不比黄金 PNG（避免 GPU 精度绑死）。ASan 下无泄漏。

`cache == nullptr`：不测崩溃路径；测试始终 `attachToContext`。

---

## §6 文档

新增 `docs/pag-runtime-filter.md`：职责表、Apply 调用约定、pipeline 缓存键、与 ColorSourceEffect 的对比（fill vs 后处理）。`docs/README.md` 索引加一行。

`docs/rendering.md` 本阶段不改（尚未进入 DrawCommand）。

---

## §7 错误与边界

- `input == nullptr` → `Apply` 返回 `nullptr`。
- shader 编译失败 / 无 cache → `onDraw` 返回 `false`；`makeWithFilter` 结果由 tgfx 决定（测试不覆盖编译失败）。
- extraInputs 本阶段不用；DisplacementMap 以后再传。
- 禁止 `dynamic_cast` / 异常；成员 `= {}` 初始化。

---

## §8 后续（明确不做）

1. Core `Effect` 模型 + 求值进 `SceneState`
2. `endLayer` 链式 Apply
3. FastBlur（`tgfx::ImageFilter::Blur`）、HueSaturation、Mosaic、…
4. Layer Style 滤镜
5. PAG 导出 Effect 块
