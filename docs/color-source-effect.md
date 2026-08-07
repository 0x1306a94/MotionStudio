# ColorSourceEffect 与 RenderCache

`adapter/tgfx` 下的过程色填充：用户提供 GLSL `mainImage(uv)`，经 tgfx `RuntimeEffect` 生成色场纹理，再以 `ImageShader` 填 path / rect。本文说明职责拆分、绘制路径、缓存与调用约定。

相关代码：

- `adapter/tgfx/src/effects/ColorSourceEffect.{h,cpp}`
- `adapter/tgfx/src/RenderCache.{h,cpp}`
- `adapter/tgfx/tests/ColorSourceEffectTest.mm`

## 1. 职责拆分

| 层级 | 角色 | 生命周期 |
|---|---|---|
| `ColorSourceEffect` | 轻量实例：`shaderId`、`mainImage`、uniform 值、`sourceBounds`、指向 `RenderCache` | **每次绘制新建**；同帧不同参数用不同实例，可共享同一 `shaderId` |
| `RenderCache` | 重资源：pipeline（按 `shaderId`）、全屏三角 VBO | 跟 tgfx `Context` 绑定；context 切换时 `releaseAll` |
| tgfx `GlobalCache` | UBO bump 池（`findOrCreateUniformBuffer` / `resetUniformBuffer`） | 跟 `Context` 生命周期；`DrawingBuffer::encode` 末尾自动 `resetUniformBuffer` |
| tgfx Runtime 包装 | `ImageFilter::Runtime` → filtered `Image` → `ImageShader` | **按次创建**，不要当参数容器长期复用 |

```
ColorSourceEffect::Make(shaderId, mainImageGlsl, uniformDecls, sourceBounds, &cache)
        │  setData(...)                // 用户 uniform（每实例各自一份）
        │  setFrameContext(...)        // 每帧 iTime / iFrame（动画 fill）
        │  makeImageShader()           // 内部 MakeTrans(sourceBounds.origin)
        ▼
canvas->drawRect / drawPath(world XYWH, paint)
```

设计层：一个文档 shader（`EntityId`）可被多个填充复用，只是绘制参数不同；GPU `RenderPipeline` 按 `shaderId` 共享。源码或 uniform **布局**变更后，调用方先 `cache.invalidateColorSourcePipeline(shaderId)`，再建实例绘制。

## 1.1 Core 存储

文档侧过程色定义与 Fill/Stroke 引用属 **Core**（`Document.shaders` + 包内独立 `shader.json`；`document.json` 的 `schemaVersion` 仍为 1；color XOR shader 的 `StylePaintMode`）。`UniformFormat` 在 Core，adapter 共用。

**预览已接线**：`SceneEvaluator` 把 Shader fill/stroke 求值进 `Paint.shader` 快照并填充 `SceneState` 帧字段；`TgfxCanvasAdapter` 每帧经 `setColorSourceFrameContext` 注入后，用 `ColorSourceEffect` 画 path fill/stroke（编译失败则跳过绘制）。App 包读写与 Inspector/库 UI 见 [Color Source App UI](superpowers/specs/2026-08-07-color-source-app-ui-design.md)。

设计说明：[Color Source Core 存储](superpowers/specs/2026-08-07-color-source-core-storage-design.md)；数据模型摘要见 [data-model.md](data-model.md) §3 / Fill·Stroke。

## 2. 绘制路径（始终离屏）

当前实现**不会**把过程 shader 直接编进目标 canvas 的 fragment：

1. `makeImageShader()` 用 `ImageFilter::Runtime(this)` 包一层  
2. 真正上屏时，tgfx `RuntimeImageFilter` 按 `sourceBounds` 建离屏 RT  
3. `ColorSourceEffect::onDraw` 在该 RT 上画全屏三角，写出色场  
4. 结果纹理作为 `ImageShader` 填 path / rect（已带 bounds 原点矩阵）

因此：色场生成与形状填充是两段 GPU 工作；离屏纹理由 tgfx 按 **RuntimeEffect 对象身份** 缓存。

## 3. GLSL 约定

用户只提供 `mainImage` 体；`BuildFragmentShaderSource` 注入 UBO 与入口：

```glsl
// 系统注入（Shadertoy 核心命名）
layout(std140) uniform UniformBlock {
    vec3 iResolution;   // xy = sourceBounds 宽高（像素），z = 1.0
    float iTime;        // 播放时间（秒）
    float iTimeDelta;   // 距上一帧的时间（秒）
    int iFrame;         // 帧序号
    float iFrameRate;   // 合成帧率（Hz）
    // ... 用户声明的 uniforms
};

vec4 mainImage(vec2 uv);  // uv ∈ [0,1]，由 v_uv 传入

void main() {
    out_fragColor = mainImage(v_uv);
}
```

- 内置 uniform 由 `onDraw` 写入；时间相关值来自 `setFrameContext`  
- 用户 uniform：经 `Uniform` 列表声明，`getUniformData()->setData` 更新  
- Shadertoy 移植：入口改为 `vec4 mainImage(vec2 uv)`；`iDate` / `iChannel*` 等非动效必需项不在此注入，需要时在用户 uniform 列表自行声明

Pipeline 缓存键 = **`shaderId`（`EntityId`）**。同一 id 的多个绘制实例共享一个 `RenderPipeline`；不同 id 各编一份（不做内容指纹去重）。

## 4. RenderCache

### 4.1 Pipeline

- `getOrCreatePipeline`：按 `shaderId` 查找 / 编译 / 写入 `colorSourcePipelineMap_`
- `invalidateColorSourcePipeline(shaderId)`：源码或 uniform 布局编辑后丢弃旧 pipeline，下次 draw 再编译

### 4.2 Uniform buffer（走 tgfx `GlobalCache`）

Metal Shared UBO 若每帧原地 `map` 重写同一块，而上一帧 command buffer 未完成，GPU 会读到半写数据。tgfx 已在 `GlobalCache` 内用可增长 packet 池 + `frameTime` / `completedFrameTime` 解决。

`ColorSourceEffect::onDraw` 经 `RenderCache::acquireUniformSlice` 转发到：

```cpp
context->globalCache()->findOrCreateUniformBuffer(size, &offset);
```

帧边界由 tgfx `DrawingBuffer::encode` 末尾的 `resetUniformBuffer()` 推进（`flush` / `readPixels` 等会走到这条路径）。**不必**在 App 再维护一份 UBO 池或手动推进帧。

对照源码（vendored 相对仓库根）：

| 能力 | 位置 |
|---|---|
| `UniformBufferPacket` / `uniformBufferPool` / `activePacket` | `third_party/libpag/third_party/tgfx/src/gpu/GlobalCache.h` |
| `findOrCreateUniformBuffer` / `resetUniformBuffer` | `GlobalCache.cpp` |
| encode 后 reset | `third_party/libpag/third_party/tgfx/src/gpu/DrawingBuffer.cpp` |

`RenderCache.cpp` 为调用上述 API 包含 tgfx 私有头 `gpu/GlobalCache.h`（CMake 增加 `src/` 与必要 third_party 搜索路径）。

### 4.3 其它

- `getFullscreenVertexBuffer`：clip 空间全屏三角，context 级懒创建  
- `attachToContext`：`uniqueID` 变化时 `releaseAll`

## 5. 调用约定

### 基本用法

```cpp
const EntityId shaderId = /* 文档 shader 资产 id */ EntityId::Generate();

auto effect = ColorSourceEffect::Make(shaderId, mainImageGlsl, {{"rippleCount", UniformFormat::Float}},
                                      shapeBounds, &cache);
effect->setFrameContext({timeSeconds, frameIndex, frameRate});  // 动画 fill 每帧
effect->getUniformData()->setData("rippleCount", 5.f);

paint.setShader(effect->makeImageShader());     // 已含 origin 矩阵
canvas->drawRect(shapeBounds, paint);           // 或 drawPath，正常 XYWH
```

无需调用方再 `makeWithMatrix` / `translate`。实例不复用：下一帧再 `Make` 即可；pipeline 仍按 `shaderId` 命中。

### 编辑源码后

```cpp
cache.invalidateColorSourcePipeline(shaderId);
// 再用新 mainImage / 新 uniform 布局 Make(...) 并绘制
```

### 同帧多个填充

| 需求 | 做法 |
|---|---|
| 同 shader、不同 uniform / 尺寸 | **两个** `ColorSourceEffect` 实例，传入**同一** `shaderId`；pipeline 共享 |
| 同一实例改两次 uniform 再画 | 同一次 flush 内会撞上 RuntimeImageFilter 按 effect 身份的缓存，**两侧结果相同**；不要这么用 |
| 跨帧改参数 | 实例本就不复用；每帧 `Make` + `setData`；UBO 帧推进由 tgfx `flush`/`encode` 内 `resetUniformBuffer` 完成 |

## 6. 设计取舍摘要

1. **实例轻、缓存重**：每次绘制新建 `ColorSourceEffect`；pipeline / 全屏 VBO 跟 `RenderCache` 走（按 `shaderId` 共享 pipeline）；UBO 池复用 tgfx `GlobalCache`。  
2. **始终离屏**：受 tgfx `RuntimeEffect` + `ImageFilter` 模型约束；换「直填目标」需另套 API。  
3. **同帧多参数 = 多实例 + 同 `shaderId`**：tgfx 离屏结果按 effect 对象缓存，故参数不同必须多实例；GPU pipeline 仍可复用。  
4. **`sourceBounds` 构造时固定**：size 定 RT，origin 定 ImageShader 放置，业务侧保持 XYWH 绘制；AABB 变化需新建实例。  
5. **编辑 = invalidate + 新实例**：不在 effect 上提供 `setShaderSource`；源码变更后显式丢弃旧 pipeline。  
6. **CPU `uniformBytes_` 跟实例、不进 cache**：`setData` 发生在 draw API 之前，而 `onDraw` 由 tgfx 稍后驱动，必须有一块 CPU 侧 shadow 承接参数再在 `onDraw` 里拷进 GPU slice。  
7. **GPU UBO 交给 GlobalCache**：不自管三缓冲；与 tgfx 普通绘制共用同一 timeline 安全的 UBO 池。

## 7. 测试入口

`ColorSourceEffectTest`（`tgfx_adapter_test`）：

- `FillsStarPathWithEffect`：星形 path 填充  
- `RendersShadertoyXs3GWjFrames` / `RendersShadertoyCloudsFrames`：全屏样例导出 webp  
- `SharesPipelineAcrossTwoEffectInstances`：同 `shaderId`、两实例、同帧不同 `iTime`、左右 XYWH  
- `InvalidatesPipelineAfterShaderEdit`：`invalidate` 后同 id 新源码重建 pipeline  

产物目录：`adapter/tgfx/tests/out/`（gitignore）。
