# Layer Styles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**Goal:** 叶子层支持 Drop Shadow / Outer Glow / Stroke 三条 Layer Style：effect 之后按 Behind → 原图 → Above 合成，再用图层 opacity / blend 叠回。

**Architecture:** `Layer::layerStyles[]` 与 `styles[]`、`effects[]` 并列。求值用 `LayerFx::snapshot(time)` bake 成静态值。`EndLayer` 同时带 effects 与 layerStyles。adapter 先跑 effect 链，再把装饰和原图合成一张 `composited`。SolidStroke / AlphaEdgeDetect 只服务本目录三种 style。

**Tech Stack:** C++17、GoogleTest、tgfx Metal、`ImageFilter::DropShadowOnly`、`RuntimeFilter`

**Spec:** `docs/superpowers/specs/2026-08-16-layer-styles-design.md`

## 全局约束

- 仅 Shape / Image / Text；Precomp / Group 上的 layerStyles 存盘但不求值、不渲染
- 基类名 `LayerFx`；Stroke 类名 `LayerStrokeStyle`（不要用 `StrokeStyle`）
- 分发用 `type()` + `static_cast`；禁止 `dynamic_cast` 与 C++ 异常
- 不升 `schemaVersion`；`layerStyles` 缺省 = `[]`；未知 JSON `type` 跳过该项
- 不做 Gradient Overlay、Inner Shadow / Glow / Bevel、导出、命中扩 bounds
- Outer Glow 不做 noise / 渐变色 / technique / jitter
- `if` / `switch` 分支必须 `{}`；成员 `= {}` 初始化
- CMake 已 glob `src/`、`include/`、`tests/`、`adapter/tgfx/src` 与 `tests`、`apps/.../Inspector/`，新文件自动进库
- Commit：英语一句、句号结尾、120 字符内、无其它标点；不 push
- 每完成一步立刻把本 plan 对应 `- [ ]` 改为 `- [x]` 并更新 Task `**Status:**`，与代码一并 commit

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/model/LayerFx.h` | `LayerFx` + DropShadow / OuterGlow / LayerStrokeStyle |
| `src/model/LayerFx.cpp` | `type()` / `drawPosition()` / `snapshot()` |
| `include/MotionStudio/model/Layer.h` | `layerStyles` 向量 |
| `src/serialization/Serializer.cpp` | 读写 `layerStyles` |
| `src/model/PropertyPath.cpp` | `layerStyles[i].*` |
| `include/MotionStudio/undo/CommandKind.h` + 六个命令 | 增删移 / enabled / blendMode / stroke position |
| `include/MotionStudio/render/EvaluatedLayer.h` | `vector<shared_ptr<const LayerFx>>` |
| `DrawCommand.h` / `RenderAdapter.h` / `RenderAdapter.cpp` / `CommandBuilder.cpp` | EndLayer 带 layerStyles |
| `adapter/tgfx/src/TgfxCanvasAdapter.cpp` | 合成 + 分发 |
| `adapter/tgfx/src/effects/SolidStrokeFilter.{h,cpp}` | spread / Stroke |
| `adapter/tgfx/src/effects/AlphaEdgeDetectFilter.{h,cpp}` | Stroke Inside/Center |
| `bridge/include/motionstudio_bridge.h` + 实现 | `MS_LAYER_FX` 与命令 |
| `LayerStylesInspector.swift` / `TimelineSupport.swift` / `PropertyPath.swift` | UI |
| `docs/data-model.md` / `docs/rendering.md` / `docs/pag-runtime-filter.md` | 文档 |

---

### Task 1: LayerFx 模型 + snapshot

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/model/LayerFx.h`
- Create: `src/model/LayerFx.cpp`
- Create: `tests/model/LayerFxTest.cpp`
- Modify: `include/MotionStudio/model/Layer.h`（`#include "MotionStudio/model/LayerFx.h"` + `layerStyles`）

**Interfaces:**
- Produces: `enum class LayerFxType { DropShadow, OuterGlow, Stroke }`
- Produces: `enum class LayerFxDrawPosition { Behind, Above }`
- Produces: `LayerFx::snapshot(PreviewTime) const → shared_ptr<const LayerFx>`
- Produces: `LayerFx::drawPosition() const` 默认 Behind；`LayerStrokeStyle` 覆盖为 Above
- Produces: `Layer::layerStyles` 为 `vector<unique_ptr<LayerFx>>`

- [x] **Step 1: 写失败测试**

`tests/model/LayerFxTest.cpp`：

```cpp
#include <gtest/gtest.h>

#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerFx.h"

using motion::BlendMode;
using motion::Color;
using motion::DropShadowStyle;
using motion::Layer;
using motion::LayerFxDrawPosition;
using motion::LayerFxType;
using motion::LayerStrokeStyle;
using motion::LayerType;
using motion::OuterGlowStyle;
using motion::StrokePosition;

TEST(LayerFxTest, DefaultDropShadowIsVisibleSnapshot) {
    DropShadowStyle style;
    EXPECT_EQ(style.type(), LayerFxType::DropShadow);
    EXPECT_EQ(style.drawPosition(), LayerFxDrawPosition::Behind);
    EXPECT_EQ(style.blendMode, BlendMode::Multiply);
    const auto snap = style.snapshot(0);
    ASSERT_NE(snap, nullptr);
    const auto &baked = static_cast<const DropShadowStyle &>(*snap);
    EXPECT_FLOAT_EQ(baked.distance.evaluate(0), 5.0f);
    EXPECT_FLOAT_EQ(baked.size.evaluate(0), 5.0f);
    EXPECT_FLOAT_EQ(baked.opacity.evaluate(0), 0.75f);
}

TEST(LayerFxTest, DropShadowIdentityIsNull) {
    DropShadowStyle style;
    style.distance.setStaticValue(0.0f);
    style.size.setStaticValue(0.0f);
    style.spread.setStaticValue(0.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
    style.enabled = false;
    style.distance.setStaticValue(5.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
}

TEST(LayerFxTest, OuterGlowClampsRangeAndSkipsZeroSize) {
    OuterGlowStyle style;
    EXPECT_EQ(style.blendMode, BlendMode::Screen);
    style.range.setStaticValue(0.0f);
    const auto snap = style.snapshot(0);
    ASSERT_NE(snap, nullptr);
    const auto &baked = static_cast<const OuterGlowStyle &>(*snap);
    EXPECT_FLOAT_EQ(baked.range.evaluate(0), 0.01f);
    style.size.setStaticValue(0.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
}

TEST(LayerFxTest, StrokeDrawsAboveAndSkipsZeroSize) {
    LayerStrokeStyle style;
    EXPECT_EQ(style.type(), LayerFxType::Stroke);
    EXPECT_EQ(style.drawPosition(), LayerFxDrawPosition::Above);
    EXPECT_EQ(style.position, StrokePosition::Outside);
    EXPECT_NE(style.snapshot(0), nullptr);
    style.size.setStaticValue(0.0f);
    EXPECT_EQ(style.snapshot(0), nullptr);
}

TEST(LayerFxTest, LayerHoldsLayerStylesVector) {
    Layer layer(LayerType::Shape);
    layer.layerStyles.push_back(std::make_unique<DropShadowStyle>());
    ASSERT_EQ(layer.layerStyles.size(), 1u);
}
```

- [x] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target core_tests && ./build/tests/core_tests --gtest_filter='LayerFxTest.*'
```

Expected: 编译失败（找不到 `LayerFx.h`）

- [x] **Step 3: 实现模型**

`LayerFx.h` 按 spec §1 写出三个子类。`snapshot` 规则：

- `size` / `distance` / `spread`：`<0` → `0`
- `spread` clamp `[0, 1]`；`range` clamp `[0.01, 1]`
- 丢掉：`!enabled`、`opacity<=0`、Shadow 三零、Glow/Stroke `size<=0`
- bake 时 `setStaticValue`，拷贝 `id` / `enabled` / `blendMode` / Stroke `position`

`Layer.h` 增加：

```cpp
#include "MotionStudio/model/LayerFx.h"
// ...
std::vector<std::unique_ptr<LayerFx>> layerStyles;
```

- [x] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target core_tests && ./build/tests/core_tests --gtest_filter='LayerFxTest.*'
```

Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/model/LayerFx.h src/model/LayerFx.cpp \
  include/MotionStudio/model/Layer.h tests/model/LayerFxTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Add LayerFx model snapshots for drop shadow glow and stroke."
```

---

### Task 2: 序列化 + PropertyPath

**Status:** ✅ Done

**Files:**
- Modify: `src/serialization/Serializer.cpp`（`LayerFxToJson` / `LayerFxFromJson`，`LayerToJson` / `LayerFromJson`）
- Modify: `src/model/PropertyPath.cpp`（`layerStyles[i]`）
- Test: `tests/serialization/SerializerTest.cpp` 或新建 `tests/serialization/LayerFxSerializerTest.cpp`
- Test: `tests/model/PropertyPathTest.cpp`（若已有 effect 路径测试则同文件加用例）

**Interfaces:**
- Consumes: Task 1 三个子类
- Produces: JSON `type` = `dropShadow` / `outerGlow` / `stroke`
- Produces: `ResolveAnimatable` 识别 `layerStyles[i].color|opacity|size|angle|distance|spread|range`

- [x] **Step 1: 写失败测试**

`tests/serialization/LayerFxSerializerTest.cpp`：建 Shape 层，push 默认 DropShadow + OuterGlow + LayerStrokeStyle，`Serialize` / `Deserialize`，断言三字段与 `blendMode` / `position` round-trip。再测 JSON 无 `layerStyles` 的旧文档 `layerStyles.empty()`。再测 `"type":"nope"` 被跳过、另外两项仍在。

PropertyPath：`ResolveAnimatable(doc, {layerId, "layerStyles[0].distance"})` 指向 DropShadow 的 distance。

- [x] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='LayerFxSerializerTest.*:PropertyPathTest.ResolvesLayerStyleDistance'
```

Expected: FAIL

- [x] **Step 3: 实现**

`LayerFxToJson` / `LayerFxFromJson` 对齐 `LayerEffectToJson`（约 `Serializer.cpp:1665`）。`blendMode` 用 `dto::ToString` / `blendModeFromString`；`position` 用 `dto::ToString` / `strokePositionFromString`。未知 type 返回空 `unique_ptr`（不是 error）。`LayerToJson` 在 `effects` 后写 `layerStyles`；`LayerFromJson` 同样跳过非数组。

`PropertyPath.cpp` 在 `effects` 分支旁加 `layerStyles`，`switch (fx->type())` 返回对应 `Animatable*`。`color` 是 `Animatable<Color>`。

- [x] **Step 4: 跑测试确认通过**

Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only src/serialization/Serializer.cpp src/model/PropertyPath.cpp \
  tests/serialization/LayerFxSerializerTest.cpp tests/model/PropertyPathTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Serialize layer styles and resolve their property paths."
```

---

### Task 3: Undo 命令

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/undo/CommandKind.h`（加 `AddLayerFx` `RemoveLayerFx` `MoveLayerFx` `SetLayerFxEnabled` `SetLayerFxBlendMode` `SetLayerFxStrokePosition`）
- Create: 六个 `include/MotionStudio/undo/*LayerFx*.h` + `src/undo/*LayerFx*.cpp`
- Create: `tests/undo/LayerFxCommandTest.cpp`

**Interfaces:**
- Produces: `AddLayerFxCommand(EntityId layerId, unique_ptr<LayerFx> style)`
- Produces: `RemoveLayerFxCommand(EntityId, int index)`
- Produces: `MoveLayerFxCommand(EntityId, int fromIndex, int toIndex)`（可 merge）
- Produces: `SetLayerFxEnabledCommand` / `SetLayerFxBlendModeCommand` / `SetLayerFxStrokePositionCommand`（可 merge；非 Stroke 的 position 命令 `execute` 空操作）

- [x] **Step 1: 写失败测试**

抄 `AddLayerEffectCommand`：`execute` append，`undo` 按 `id` 拔出；Remove 插回原下标；Move 交换；Enabled / BlendMode / Position 改完 undo 恢复。`SetLayerFxStrokePositionCommand` 打在 DropShadow 上不改任何字段。

- [x] **Step 2: 跑测试确认失败**

Expected: 编译失败

- [x] **Step 3: 实现命令**

`AddLayerFxCommand` 与 `src/undo/AddLayerEffectCommand.cpp` 相同，只把 `effects` 换成 `layerStyles`。`describe()`：`Add Layer Style` / `Remove Layer Style` / `Move Layer Style` / `Set Layer Style Enabled` / `Set Layer Style Blend Mode` / `Set Layer Style Position`。

CMake glob 会收新文件。若有命令工厂 / switch(CommandKind) 注册表，一并加上新 kind。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/tests/core_tests --gtest_filter='LayerFxCommandTest.*'
```

Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/undo/ src/undo/ tests/undo/LayerFxCommandTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Add undo commands for layer style lists and static fields."
```

---

### Task 4: 求值 + EndLayer 签名

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/render/EvaluatedLayer.h`
- Modify: `src/render/SceneEvaluator.cpp`（`FillCommonLayerFields`）
- Modify: `include/MotionStudio/render/DrawCommand.h`
- Modify: `include/MotionStudio/render/RenderAdapter.h`
- Modify: `src/render/RenderAdapter.cpp`
- Modify: `src/render/CommandBuilder.cpp`
- Modify: `adapter/tgfx/include/TgfxCanvasAdapter.h` + `TgfxCanvasAdapter.cpp`（先只改签名，`layerStyles` 忽略）
- Test: `tests/render/SceneEvaluatorTest.cpp` / `CommandBuilderTest.cpp`（或新建 `tests/render/LayerFxEvaluateTest.cpp`）

**Interfaces:**
- Produces: `EvaluatedLayer::layerStyles`
- Produces: `DrawCommand::layerStyles`
- Produces: `RenderAdapter::endLayer(effects, layerStyles)`
- Produces: `needsIsolation` 含 `!layer.layerStyles.empty()`

- [x] **Step 1: 写失败测试**

默认 DropShadow 的 Shape 层：`Evaluate` 后 `evaluated.layerStyles.size()==1`。`enabled=false` 不进。Precomp 层带 DropShadow：子层列表无该 style、无组级 BeginLayer。

CommandBuilder：仅 layerStyles → 有 Begin/EndLayer，`EndLayer.layerStyles.size()==1`。同时有 GaussianBlur + DropShadow → EndLayer 两个向量都非空。

- [x] **Step 2: 跑测试确认失败**

Expected: FAIL（没有 `layerStyles` 字段）

- [x] **Step 3: 接线**

`FillCommonLayerFields` 在 effects 循环后：

```cpp
for (const auto &style : layer.layerStyles) {
    if (std::shared_ptr<const LayerFx> snap = style->snapshot(time)) {
        evaluated.layerStyles.push_back(std::move(snap));
    }
}
```

`needsIsolation` 加 `|| !layer.layerStyles.empty()`。`endLayer.layerStyles = layer.layerStyles`。`PlayCommands`：`adapter.endLayer(command.effects, command.layerStyles)`。

`TgfxCanvasAdapter::endLayer` 先加第二参数，暂不使用（现有 effect 路径保持）。所有 override 一起改。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/tests/core_tests --gtest_filter='LayerFxEvaluateTest.*:CommandBuilder*LayerFx*'
cmake --build build --target tgfx_adapter_test
```

Expected: core PASS；adapter 仍能编过

- [x] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/render/ src/render/ adapter/tgfx/include/TgfxCanvasAdapter.h \
  adapter/tgfx/src/TgfxCanvasAdapter.cpp tests/render/ \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Evaluate layer styles and pass them through EndLayer."
```

---

### Task 5: adapter Behind/Above 合成 + Shadow/Glow spread=0

**Status:** ✅ Done

**Files:**
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`（`endLayer`、`ApplyLayerFx`）
- Test: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`

**Interfaces:**
- Consumes: Task 4 `endLayer(effects, layerStyles)`
- Produces: 无 layerStyles 时行为与现在相同
- Produces: spread=0 的 Shadow / Glow 用 `ImageFilter::DropShadowOnly`

- [x] **Step 1: 写失败测试**

抄 `GaussianBlurSoftensRectEdge`：100×100 黑底，20×20 白块居中。

`DropShadowKeepsFillAndCastsOffset`：默认 DropShadow（angle 135、distance 5）。中心仍近白；沿 135° 外侧（约 x=50-4, y=50-4 相对块外）出现非零偏黑像素。

`OuterGlowTintsAroundRect`：默认 OuterGlow。中心仍近白；块外四向出现偏黄非零。

`LayerOpacityFadesDropShadow`：图层 opacity 0.5 + 默认 Shadow。阴影像素 alpha 约为 opacity 1 时的一半（容差 ±20）。

- [x] **Step 2: 跑测试确认失败**

```bash
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxRenderAdapterTest.DropShadow*:TgfxRenderAdapterTest.OuterGlow*:TgfxRenderAdapterTest.LayerOpacityFadesDropShadow'
```

Expected: FAIL（layerStyles 被忽略）

- [x] **Step 3: 实现合成**

`endLayer` 在 effect 链之后：

```
若 layerStyles 空：现路径 parent.draw(image)
否则：
  对每条 style 算 filter bounds 并入 composited 矩形
  surface = renderCache_->acquireSurface(w, h, RGBA_8888)
  Behind：ApplyLayerFx 画到该 canvas
  画原 image（Normal / 1）
  Above：ApplyLayerFx
  parent.draw(composited) 用图层 opacity / blend
```

`ApplyLayerFx`：`switch (type())`。本 Task 只实现 Shadow / Glow 的 `spread==0`：

```cpp
// DropShadow
radians = (angle - 180) * PI / 180
offsetX = cos(radians) * distance
offsetY = -sin(radians) * distance
filter = tgfx::ImageFilter::DropShadowOnly(offsetX, offsetY, size/2, size/2, ToTgfxColor(color))
// OuterGlow
filter = tgfx::ImageFilter::DropShadowOnly(0, 0, size/range/2, size/range/2, ToTgfxColor(color))
```

`color` 只用 RGB。装饰 `paint.setAlpha(opacity)` + `style.blendMode`。Stroke 本 Task 跳过。

角度用现有工程里的 deg→rad 写法（搜 `DegreesToRadians` 或 `M_PI`）。

- [x] **Step 4: 跑测试确认通过**

Expected: PASS（无 Metal 则 SKIP）

- [x] **Step 5: Commit**

```bash
git commit --only adapter/tgfx/src/TgfxCanvasAdapter.cpp adapter/tgfx/tests/TgfxRenderAdapterTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Composite drop shadow and outer glow behind isolated layer content."
```

---

### Task 6: SolidStroke + AlphaEdgeDetect + Stroke + spread

**Status:** ✅ Done

**Files:**
- Create: `adapter/tgfx/src/effects/SolidStrokeFilter.{h,cpp}`
- Create: `adapter/tgfx/src/effects/AlphaEdgeDetectFilter.{h,cpp}`
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`（`ApplyLayerFx` spread / Stroke 分支）
- Test: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp` + 可选 `SolidStrokeFilterTest.mm`

**Interfaces:**
- Produces: `SolidStrokeFilter::Create(cache, option, mode, originalImage)` → `ImageFilter`
- Produces: `AlphaEdgeDetectFilter` RuntimeFilter
- Consumes: PAG 常量 `STROKE_MAX_SPREAD_SIZE=25`、`STROKE_SPREAD_MIN_THICK_SIZE=12`
- Consumes: PAG shader 原文（`SolidStrokeFilter.cpp` / `AlphaEdgeDetectFilter.cpp`），UBO 改走 `acquireUniformSlice`

- [x] **Step 1: 写失败测试**

`DropShadowSpreadChangesPixels`：同一白块，Shadow `spread=0` vs `spread=1`（size=8, distance=0），`readPixels` 不全等。

`StrokeOutsideDrawsAroundRect`：`LayerStrokeStyle` 默认 Outside、红色、size=6。块外出现偏红像素；中心仍近白。

`BlurThenDropShadowHasShadowInBleed`：先 GaussianBlur 16 再默认 Shadow。模糊渗出区（已有 `BlurBleedsOutsideAddMask` 的采样点思路）也有阴影分量。

- [x] **Step 2: 跑测试确认失败**

Expected: FAIL（spread 被当成 0；Stroke 被跳过）

- [x] **Step 3: 移植滤镜并接线**

从 `third_party/libpag/src/rendering/filters/layerstyle/SolidStrokeFilter.cpp` 拷 Normal / Thick shader 与 `filterBounds` / `computeVertices`。`onUpdateUniforms` 用 `RenderCache::acquireUniformSlice`，不要 `gpu->createBuffer`。`position` Invalid 时三个 isOutside/Center/Inside 都为 0。

`AlphaEdgeDetectFilter` 拷 PAG shader。

`ApplyLayerFx` 按 spec §3 三段公式。Stroke：

- Outside：`SolidStroke(..., original=null)`
- Inside/Center：`Compose(AlphaEdgeDetect, SolidStroke(..., original=image))`，spreadSize 乘 0.8 / 0.4

Shadow thick：`size * spread >= 12`。Glow / Stroke thick：`size >= 12`。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxRenderAdapterTest.DropShadow*:TgfxRenderAdapterTest.OuterGlow*:TgfxRenderAdapterTest.Stroke*:TgfxRenderAdapterTest.BlurThenDropShadow*:TgfxRenderAdapterTest.LayerOpacityFadesDropShadow'
```

Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only adapter/tgfx/src/effects/ adapter/tgfx/src/TgfxCanvasAdapter.cpp \
  adapter/tgfx/tests/ \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Port solid stroke filters for layer style spread and stroke."
```

---

### Task 7: Bridge

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/` 里 layer query + command 文件（对标 `ms_layer_effect_*`）
- Modify: `apps/.../Bridge/MotionStudioBridgingExtension.swift`
- Modify: `apps/.../Model/MotionDocumentCore.swift`
- Test: `bridge` 测试（对标 `BridgeTest.LayerEffectRepeatEdgeQuery`）

**Interfaces:**
- Produces: `MS_LAYER_FX` = INVALID / DROP_SHADOW / OUTER_GLOW / STROKE
- Produces: count / type / enabled / blend / stroke_position 查询
- Produces: add 三条、remove、move、set enabled、set blend、set stroke position

- [x] **Step 1: 写失败测试**

`BridgeTest.LayerFxDropShadowRoundTrip`：add drop shadow → count 1、type DROP_SHADOW、enabled true、blend MULTIPLY。set enabled false。add outer glow、add stroke → count 3。stroke position 默认 OUTSIDE，set INSIDE。remove index 0。

- [x] **Step 2: 跑测试确认失败**

```bash
./build/bridge/bridge_test --gtest_filter='BridgeTest.LayerFx*'
```

Expected: 编译失败

- [x] **Step 3: 实现 Bridge + Core 包装**

命令内部 `document.execute(make_unique<AddLayerFxCommand>(...))`。查询越界：type INVALID、enabled false、blend NORMAL、position INVALID。

`MS_LAYER_FX` 扩展：

```swift
public static var allCases: [MS_LAYER_FX] {
    [.DROP_SHADOW, .OUTER_GLOW, .STROKE]
}
var label: String { /* Drop Shadow / Outer Glow / Stroke */ }
```

`MotionDocumentCore` 薄包装一律 `changed()`。

- [x] **Step 4: 跑测试确认通过**

Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only bridge/ apps/MotionStudioApp/MotionStudioApp/Bridge/ \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Expose layer style commands and queries on the bridge."
```

---

### Task 8: Inspector + 时间轴

**Status:** 未开始

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/LayerStylesInspector.swift`
- Modify: `InspectorView.swift`（Effects 之后插入，仅 Shape/Image/Text）
- Modify: `apps/.../Bridge/PropertyPath.swift`（`LayerStyleFxProperty`）
- Modify: `apps/.../Timeline/Root/TimelineSupport.swift`

**Interfaces:**
- Consumes: Task 7 Core API
- Produces: `LayerStyleFxProperty.path(at:)` → `layerStyles[i].color|opacity|size|angle|distance|spread|range`
- Produces: `timelineLayerStyleTracks` 挂进 `timelineAnimatedPropertyPaths` 与 `buildTimelineRows`

- [ ] **Step 1: 写 Inspector**

抄 `EffectsInspector.swift`：倒序列表、`+` 菜单 `MS_LAYER_FX.allCases`、enabled / 上下移 / 删除。ColorPicker 抄 `FillsInspector` 关键帧。Number 行抄 `effectRow`。Blend 抄 Fill。Stroke 另加 Position picker（`MS_STROKE_POSITION`，不要 INVALID）。

- [ ] **Step 2: 挂 InspectorView**

在 `EffectsInspector(...)` 之后再放 `LayerStylesInspector(...)`，同一叶子层条件。

- [ ] **Step 3: 时间轴**

```swift
enum LayerStyleFxProperty: String {
    case color, opacity, size, angle, distance, spread, range
    func path(at index: Int) -> String { "layerStyles[\(index)].\(rawValue)" }
}
```

按 type 收集已有关键帧路径；label = `"\(type.label) \(actionLabel)"`。`timelineAnimatedPropertyPaths` 与 `buildTimelineRows` 都 `append(contentsOf: timelineLayerStyleTracks(...))`。

- [ ] **Step 4: 用 Xcode MCP 编 MotionStudioApp**

`user-xcode` ready 则 `XcodeListWindows` + `BuildProject`。失败用 `GetBuildLog`。

Expected: BUILD SUCCEEDED

- [ ] **Step 5: Commit**

```bash
git commit --only apps/MotionStudioApp/ \
  docs/superpowers/plans/2026-08-16-layer-styles.md \
  -m "Add the layer styles inspector and timeline tracks."
```

---

### Task 9: 文档

**Status:** 未开始

**Files:**
- Modify: `docs/data-model.md`
- Modify: `docs/rendering.md`
- Modify: `docs/pag-runtime-filter.md`
- Modify: `docs/superpowers/specs/2026-08-16-layer-styles-design.md`（状态改为已实现）

- [ ] **Step 1: 改文档**

`data-model.md`：`layerStyles[]` 与 `styles[]` / `effects[]` 职责表。  
`rendering.md`：isolation 条件、EndLayer 两向量、Behind → 原图 → Above、再图层 opacity。  
`pag-runtime-filter.md`：SolidStroke / AlphaEdgeDetect / 三种 style 调用链。  
spec 状态：`已实现`。

- [ ] **Step 2: Commit**

```bash
git commit --only docs/ \
  -m "Document layer styles in the data model and render pipeline."
```

---

## Spec 覆盖核对

| Spec | Task |
|---|---|
| §1 模型 / snapshot / JSON / PropertyPath | 1–2 |
| §2 求值 / EndLayer | 4 |
| §3 合成 / Shadow / Glow / Stroke / SolidStroke / AlphaEdgeDetect | 5–6 |
| §4 Undo / Bridge / Inspector / 时间轴 | 3、7、8 |
| §5 测试 | 各 Task 内 |
| §6 文档 | 9 |
| §7 错误边界 | 5（失败跳过）、4（空 EndLayer 保持空操作） |
