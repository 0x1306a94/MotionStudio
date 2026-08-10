# Tgfx BitmapFrameSource — 设计说明

日期：2026-08-10  
状态：已确认，待实现  
范围：生产级 `BitmapFrameSource`（tgfx Metal 离屏 + `readPixels`）+ Bridge 自动注入；依赖已落地的 [PAG export `_bmp` 策略](./2026-07-31-pag-export-design.md)

相关：`TgfxVideoFrameSource`（视频离屏）、`PagExporter` / `PagBitmapFallback`（消费 RGBA）

## 目标

1. 在 `adapter/tgfx` 实现 `TgfxBitmapFrameSource`，满足 `BitmapFrameSource` 的 `prepare` / `prepareComposition` / `renderFrame` / `finish`
2. 输出预乘 **RGBA8**（行主序），供现有 AE 风格 WebP 序列编码使用
3. **层名 `_bmp`**：只渲目标层及其 Document 子树；宿主合成尺寸；**透明底**
4. **合成名 `_bmp`（或 Precomp 强制）**：渲整合成；合成背景色；不过滤层
5. Bridge `ms_pag_export`：**自动**创建并注入（有 `_bmp` 且 `allowBitmapExport`）；App 无感

## 非目标

- CVPixelBuffer / 与视频导出共享 IOSurface 路径（已否决；选纯 Surface + `readPixels`）
- 修改 Core `SceneEvaluator` 增加子集 API
- 像素黄金图 CI（首版烟测：尺寸 + 非全透明 / 层隔离抽样）
- 非 Apple 平台实现

## 已锁定决策

| 项 | 选择 |
|---|---|
| 层 `_bmp` 画什么 | 仅该层 + Group 子树；宿主画布；透明底 |
| 读回路径 | tgfx `Surface::Make` + `readPixels` |
| Bridge | `ms_pag_export` 内部自动注入 |
| 架构 | 方案 1：Evaluate → **过滤 `SceneState.layers`** → BuildCommands → PlayCommands |

---

## §1 架构

```
ms_pag_export
  ├─ DetectBmpSuffixInExportTree(document, compositionId)
  └─ if needsBitmap && allowBitmapExport:
         TgfxBitmapFrameSource source
         PagExporter::Export(doc, options, &source)
              └─ prepare* / renderFrame*
                     Evaluate → [filter layers] → BuildCommands → PlayCommands
                     Surface::readPixels → BitmapFrame
```

| 组件 | 路径 | 职责 |
|---|---|---|
| `TgfxBitmapFrameSource` | `adapter/tgfx` | Metal 离屏 + 读回 |
| Bridge | `motionstudio_bridge_pag_export.mm` | 检测 `_bmp`、生命周期、注入 |
| `HasBmpSuffix` | `include/MotionStudio/export/PagBmpSuffix.h` | Core / Bridge 共用（从 `src/export/pag/` 上移） |
| `PagExporter` | 不变 | 继续消费 `BitmapFrameSource*` |

模块依赖：

```
tgfx_adapter → core（SceneEvaluator / BuildCommands / PlayCommands）
bridge → tgfx_adapter + pag_export（Apple）
```

---

## §2 接口

### 2.1 TgfxBitmapFrameSource

```cpp
namespace motion {

class TgfxBitmapFrameSource : public BitmapFrameSource {
public:
    TgfxBitmapFrameSource();
    ~TgfxBitmapFrameSource() override;

    Expected<void, std::string> prepare(const Document &document, EntityId hostCompositionId,
                                        EntityId rootLayerId, TimeRange visibleRange,
                                        float bitmapScale) override;

    Expected<void, std::string> prepareComposition(const Document &document,
                                                   EntityId compositionId,
                                                   TimeRange visibleRange,
                                                   float bitmapScale) override;

    Expected<BitmapFrame, std::string> renderFrame(FrameTime time) override;

    void finish() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
```

约定：

- `prepare*` 可重复调用（内部先 `finish`）
- `renderFrame` 返回的 `rgba` 指向 `Impl` 内 buffer；至下次 `renderFrame` / `finish` 前有效
- `premultiplied = true`
- 像素尺寸：`ceil(compW * bitmapScale)` × `ceil(compH * bitmapScale)`（与 Fake / 编码器一致；maxResolution 仍由 `PagExportOptions` 在 Builder 侧通过传入的 `bitmapScale`/`factor` 体现——`prepare` 收到的已是有效 factor）

### 2.2 两种模式

| | Composition 模式 | Layer 模式 |
|---|---|---|
| 入口 | `prepareComposition` | `prepare` |
| Evaluate 目标 | `compositionId` | `hostCompositionId` |
| 层过滤 | 无 | 保留 `rootLayerId` ∪ Document 子孙（按 `parentId`） |
| 背景 | 合成 `backgroundColor` | 透明 `(0,0,0,0)` |
| `cornerRadius` | 强制 `0`（避免导出裁剪双重） | `0` |

层过滤在 **`SceneState.layers`** 上按 `EvaluatedLayer.id` 完成，再 `BuildCommands`（避免破坏 Save/Restore 配对）。

伪代码（层模式收集 id）：

```
ids = {rootLayerId}
repeat until stable:
  for layer in hostComposition.layers:
    if layer.parentId in ids: ids.insert(layer.id)
```

若 `rootLayerId` 不在宿主合成中 → `prepare` 失败。

### 2.3 单帧

```
state = Evaluate(document, compositionId, time)
if layer mode: filter state.layers by ids
state.cornerRadius = 0
background = layerMode ? transparent : state.backgroundColor
beginFrame(pixelW, pixelH, background, 0)
setColorSourceFrameContext(state.timeSeconds, state.frameIndex, state.frameRate)
PlayCommands(BuildCommands(state), adapter)
endFrame / flush
readPixels(RGBA_8888 Premul) → buffer
return BitmapFrame{width, height, buffer.data(), rowBytes, premultiplied=true}
```

实现可复用 / 仿照 `TgfxCanvasAdapter` + `TgfxVideoFrameSource` 的 Metal device 获取方式；Surface 用 `tgfx::Surface::Make(context, w, h, RGBA_8888)`（或项目内等价 API），**不**走 CVPixelBuffer。

### 2.4 Bridge

```
needsBitmap = options.allowBitmapExport && ExportTreeHasBmpSuffix(document, compositionId)
source = needsBitmap ? make_unique<TgfxBitmapFrameSource>() : nullptr
result = PagExporter::Export(document, exportOptions, source.get())
// source 在栈上析构即可
```

`ExportTreeHasBmpSuffix`：与 Core `collectBitmapForcedCompositions` 规则一致——合成名或层名 `HasBmpSuffix`；Precomp 层名后缀也算（即使只为强制子合成）。

错误：优先使用 `result.error().message`（已结构化）。

### 2.5 HasBmpSuffix 上移

- 从 `src/export/pag/PagBmpSuffix.h` 移到 `include/MotionStudio/export/PagBmpSuffix.h`
- `pag_export` 与 bridge 均 include 公共头
- 测试 `PagBmpSuffixTest` 改 include 路径

---

## §3 错误

| 情况 | `Expected` 错误字符串（示例） |
|---|---|
| Metal / device 不可用 | `Metal unavailable for bitmap frame source` |
| Surface 创建失败 | `failed to create offscreen Surface (WxH)` |
| readPixels 失败 | `Surface::readPixels failed (WxH)` |
| Evaluate 失败 | 透传 |
| 层不在宿主合成 | `root layer not found in host composition` |
| 未 prepare | `bitmap frame source not prepared` |
| scale ≤ 0 | `bitmapScale must be > 0` |

---

## §4 测试计划

| 用例 | 期望 |
|---|---|
| Composition：单红 Rect，`prepareComposition` | 尺寸正确；存在非零像素 |
| Layer：红层 `_bmp` + 蓝层无后缀；`prepare` 红层 | 抽样无显著蓝色（隔离） |
| 未 prepare 调 `renderFrame` | 错误 |
| Bridge：合成名 `*_bmp` + allowBitmapExport | 导出成功；文件含 `BitmapComposition` |
| `allowBitmapExport=false` + `_bmp` | 仍 MappingFailed（不建 FrameSource 或建了也不该成功——与现 Core 一致） |

测试挂在 `tgfx_adapter` 测试目标（Metal）；与现有 `TgfxRenderAdapterTest` / `ColorSourceEffectTest` 同平台约束。

---

## §5 文件布局

```
include/MotionStudio/export/PagBmpSuffix.h          # 上移
adapter/tgfx/include/TgfxBitmapFrameSource.h
adapter/tgfx/src/TgfxBitmapFrameSource.mm
adapter/tgfx/tests/TgfxBitmapFrameSourceTest.mm     # 或 .cpp
bridge/src/apple/motionstudio_bridge_pag_export.mm  # 自动注入
src/export/pag/PagBmpSuffix.h                       # 删除或改为 #include 公共头
tests/export/pag/PagBmpSuffixTest.cpp               # include 公共头
```

CMake：`TgfxBitmapFrameSource.mm` 加入 `tgfx_adapter`；测试加入现有 tgfx 测试可执行文件。

---

## §6 实现阶段（建议）

1. 上移 `HasBmpSuffix` + 测绿  
2. `TgfxBitmapFrameSource` composition 模式烟测  
3. 层过滤 + 隔离测  
4. Bridge 自动注入 + bridge 集成测  

---

## 开放问题

无（本设计已锁定 Surface 路径与 Bridge 自动注入）。
