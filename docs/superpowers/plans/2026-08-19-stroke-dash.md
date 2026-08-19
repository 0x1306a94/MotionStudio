# Stroke Dash + Cap / Join UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shape 路径描边支持 Solid/Dashed 切换、虚线图案与可动画 offset，并补上 Cap / Join / Miter Limit 的 Inspector。

**Architecture:** `StrokeStyle.strokeMode` 与 `dashes[]` 分离；求值抄进 `StrokeOptions`；适配器在 Trim 之后对中心线调用 `tgfx::PathEffect::MakeDash`，再 Center stroke 或 Inside/Outside boolean。Cap/Join/Miter 走与 Position 相同的专用 Command + Bridge 枚举。

**Tech Stack:** C++17 core、GoogleTest、tgfx PathEffect、bridge `CF_CLOSED_ENUM`、SwiftUI `StrokesInspector`

**Spec:** `docs/superpowers/specs/2026-08-19-stroke-dash-design.md`

## Global Constraints

- `StrokeMode::{Solid, Dashed}`，字段名 `strokeMode`；默认 Solid；不升 `schemaVersion`
- 绘制 dash 仅当 `strokeMode == Dashed` 且 `NormalizeDashArray` 非空；`MakeDash(..., adaptive=false)`
- 效果顺序：先 Trim，再 Dash，再 stroke / Inside-Outside
- Solid 时保留 `dashes` / `dashOffset`；切 Dashed 且图案无效时同一命令写入 `{8, 8}`
- Cap / Join / Miter / `strokeMode` / `dashes` 静态，不上时间轴；仅 `dashOffset` 走 float PropertyPath
- 文本层 / LayerFx 不画 dash，Inspector 不展示这些行
- PAG Solid 不写 dashes；Dashed 最多 6 段
- 禁 `dynamic_cast` / 异常；undo 目标缺失静默跳过
- 本 plan 的 Commit step：用户未要求提交则跳过；勾选 plan checkbox 仍须随代码变更同步
- 源码 glob 进 `core`（`src/CMakeLists.txt` `add_files_by_extension`），新 `.h/.cpp` 无需改 CMake

---

## File Structure

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/model/StrokeMode.h` | 枚举 |
| `include/MotionStudio/model/StrokeDash.h` + `src/model/StrokeDash.cpp` | `NormalizeDashArray` / `NeedsDash` / `DefaultDashPattern` |
| `include/MotionStudio/model/LayerStyle.h` | `strokeMode` `dashes` `dashOffset` |
| `include/MotionStudio/render/StrokeOptions.h` | 求值快照补字段 |
| `src/render/SceneEvaluator.cpp` | 填 options |
| `src/model/PropertyPath.cpp` | `dashOffset` |
| `include/MotionStudio/undo/CommandKind.h` + 五个 Command | undo |
| `src/serialization/Dto.{h,cpp}` `Serializer.cpp` | JSON |
| `adapter/tgfx/src/TgfxCanvasAdapter.cpp` `TgfxPathCache.*` | MakeDash + miter + cache key |
| `src/export/pag/PagFileBuilder.cpp` `PagStrokeOutline.cpp` | 导出 |
| `src/import/svg/SvgStyle.*` `SvgWalk.cpp` | 导入映射 |
| `bridge/` + App Inspector | UI |

---

### Task 1: StrokeMode + NormalizeDashArray

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/model/StrokeMode.h`
- Create: `include/MotionStudio/model/StrokeDash.h`
- Create: `src/model/StrokeDash.cpp`
- Modify: `include/MotionStudio/model/LayerStyle.h`（`#include` + 三个字段）
- Test: `tests/model/StrokeDashTest.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `enum class StrokeMode : uint8_t { Solid = 0, Dashed = 1 };`
  - `std::vector<float> NormalizeDashArray(std::vector<float> dashes);`
  - `bool NeedsDash(StrokeMode strokeMode, const std::vector<float> &dashes);`
  - `std::vector<float> DefaultDashPattern();` → `{8.f, 8.f}`
  - `StrokeStyle::{strokeMode, dashes, dashOffset}`

- [x] **Step 1: 写失败测试**

```cpp
#include <gtest/gtest.h>
#include "MotionStudio/model/StrokeDash.h"

TEST(StrokeDashTest, OddLengthDuplicates) {
    const std::vector<float> out = motion::NormalizeDashArray({5.0f});
    ASSERT_EQ(out.size(), 2u);
    EXPECT_FLOAT_EQ(out[0], 5.0f);
    EXPECT_FLOAT_EQ(out[1], 5.0f);
}

TEST(StrokeDashTest, ClampsNegativesAndRejectsZeroSum) {
    EXPECT_TRUE(motion::NormalizeDashArray({-1.0f, 0.0f}).empty());
}

TEST(StrokeDashTest, NeedsDashRequiresDashedAndValidPattern) {
    EXPECT_FALSE(motion::NeedsDash(motion::StrokeMode::Solid, {8.0f, 8.0f}));
    EXPECT_FALSE(motion::NeedsDash(motion::StrokeMode::Dashed, {}));
    EXPECT_TRUE(motion::NeedsDash(motion::StrokeMode::Dashed, {8.0f, 8.0f}));
}
```

- [x] **Step 2: 跑测试确认失败**

Run: `./build/tests/core_tests --gtest_filter='StrokeDashTest.*'`  
Expected: FAIL（头文件不存在）

- [x] **Step 3: 最小实现**

`StrokeMode.h`：上列枚举。  
`StrokeDash.h` 声明上述三个函数。  
`StrokeDash.cpp`：负值 clamp 到 0；奇数则 `insert(end, begin, end)`；size<2 或 sum<=0 返回 `{}`。  
`LayerStyle.h`：`#include "MotionStudio/model/StrokeMode.h"`，在 trim 字段后加：

```cpp
StrokeMode strokeMode = StrokeMode::Solid;
std::vector<float> dashes;
Animatable<float> dashOffset{0.0f};
```

- [x] **Step 4: 跑测试确认通过**

Run: `./build/tests/core_tests --gtest_filter='StrokeDashTest.*'`  
Expected: PASS（需先 `cmake --build build`）

---

### Task 2: StrokeOptions 求值 + dashOffset PropertyPath

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/render/StrokeOptions.h`
- Modify: `src/render/SceneEvaluator.cpp`（`appendStroke` 填字段）
- Modify: `src/model/PropertyPath.cpp`（`resolveStyleProperty` 增加 `"dashOffset"`）
- Test: `tests/render/SceneEvaluatorTest.cpp`
- Test: 若已有 PropertyPath 测 trim，同文件加 `dashOffset`

**Interfaces:**
- Consumes: Task 1 字段
- Produces: `StrokeOptions` 含 `miterLimit` `strokeMode` `dashes` `dashOffset`；`styles[i].dashOffset` 可 `setStaticFloat` / 关键帧

`StrokeOptions` 完整形态：

```cpp
struct StrokeOptions {
    float width = 0;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.0f;
    StrokePosition position = StrokePosition::Center;
    float trimStart = 0;
    float trimEnd = 1;
    float trimOffset = 0;
    StrokeMode strokeMode = StrokeMode::Solid;
    std::vector<float> dashes;
    float dashOffset = 0;
};
```

`appendStroke` 在现有 `StrokeOptions options{width, cap, join, position, trim...}` 初始化处改为包含 `stroke.miterLimit`、`stroke.strokeMode`、`stroke.dashes`、`stroke.dashOffset.evaluatePreview(time)`（C++17 按成员序或改成赋值，避免 designated init）。

- [x] **Step 1: 写失败测试**

在 `SceneEvaluatorTest` 追加：

```cpp
TEST(SceneEvaluatorTest, StrokeItemCarriesDashAndMiter) {
    RectScene scene;
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->miterLimit = 12.0f;
    stroke->strokeMode = motion::StrokeMode::Dashed;
    stroke->dashes = {10.0f, 4.0f};
    stroke->dashOffset.setStaticValue(3.0f);
    scene.layer->styles.push_back(std::move(stroke));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const auto &strokeItem = result->layers[0].shapeItems[1];
    EXPECT_FLOAT_EQ(strokeItem.stroke.miterLimit, 12.0f);
    EXPECT_EQ(strokeItem.stroke.strokeMode, motion::StrokeMode::Dashed);
    ASSERT_EQ(strokeItem.stroke.dashes.size(), 2u);
    EXPECT_FLOAT_EQ(strokeItem.stroke.dashOffset, 3.0f);
}
```

- [x] **Step 2: 跑测确认失败**

Run: `./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.StrokeItemCarriesDashAndMiter'`  
Expected: FAIL（无成员）

- [x] **Step 3: 改 StrokeOptions + appendStroke + PropertyPath**

`resolveStyleProperty` Stroke 分支在 `trimOffset` 后：`if (name == "dashOffset") return &stroke->dashOffset;`

- [x] **Step 4: 跑测通过**

Run: `./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.StrokeItemCarriesDashAndMiter'`  
Expected: PASS

---

### Task 3: Undo 命令

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/undo/CommandKind.h` 在 `SetStrokePosition` 后追加：
  `SetStrokeMode, SetStrokeDashPattern, SetStrokeCap, SetStrokeJoin, SetStrokeMiterLimit`
- Create: 各 Command 的 `.h` / `.cpp`（对标 `SetStrokePositionCommand`，共用匿名 `FindStroke`）
- Test: `tests/undo/CommandsTest.cpp`

**Interfaces:**
- Consumes: `FindStroke` 模式；`NormalizeDashArray` `DefaultDashPattern`
- Produces:
  - `SetStrokeModeCommand(EntityId, int, StrokeMode)`：切 Dashed 且 `NormalizeDashArray(stroke->dashes).empty()` 时把 `dashes` 写成 `DefaultDashPattern()`，undo 恢复 mode **和** 旧 dashes
  - `SetStrokeDashPatternCommand(EntityId, int, std::vector<float>)`：Dashed 且归一后为空则 execute no-op；可 merge
  - `SetStrokeCapCommand` / `SetStrokeJoinCommand` / `SetStrokeMiterLimitCommand`：与 Position 同形，可 merge

- [x] **Step 1: 写失败测试**

```cpp
TEST(SetStrokeModeCommandTest, SeedsDefaultDashesWhenEmpty) {
    RectScene scene;
    scene.layer->styles.push_back(std::make_unique<StrokeStyle>());
    const EntityId id = scene.layer->id;
    scene.execute<SetStrokeModeCommand>(id, 0, StrokeMode::Dashed);
    auto *stroke = static_cast<StrokeStyle *>(scene.layer->styles[0].get());
    EXPECT_EQ(stroke->strokeMode, StrokeMode::Dashed);
    ASSERT_EQ(stroke->dashes.size(), 2u);
    scene.undoManager.undo();
    EXPECT_EQ(stroke->strokeMode, StrokeMode::Solid);
    EXPECT_TRUE(stroke->dashes.empty());
}

TEST(SetStrokeModeCommandTest, SolidKeepsDashes) {
    RectScene scene;
    auto style = std::make_unique<StrokeStyle>();
    style->strokeMode = StrokeMode::Dashed;
    style->dashes = {2.0f, 4.0f};
    scene.layer->styles.push_back(std::move(style));
    scene.execute<SetStrokeModeCommand>(scene.layer->id, 0, StrokeMode::Solid);
    auto *stroke = static_cast<StrokeStyle *>(scene.layer->styles[0].get());
    EXPECT_EQ(stroke->strokeMode, StrokeMode::Solid);
    ASSERT_EQ(stroke->dashes.size(), 2u);
}

TEST(SetStrokeCapCommandTest, SetAndUndo) {
    RectScene scene;
    scene.layer->styles.push_back(std::make_unique<StrokeStyle>());
    scene.execute<SetStrokeCapCommand>(scene.layer->id, 0, LineCap::Round);
    EXPECT_EQ(static_cast<StrokeStyle *>(scene.layer->styles[0].get())->cap, LineCap::Round);
    scene.undoManager.undo();
    EXPECT_EQ(static_cast<StrokeStyle *>(scene.layer->styles[0].get())->cap, LineCap::Butt);
}
```

另写 `SetStrokeJoinCommand` / `SetStrokeMiterLimitCommand` / `SetStrokeDashPatternCommand` 的 SetAndUndo + merge 测（Dashed 下设 `{0,0}` 保持原数组）。`RectScene` 若已有 Fill 在 `[0]`，stroke 索引用 `styles.size()-1` 或与 `SetStrokePositionCommandTest` 相同 fixture。

- [x] **Step 2: 跑测失败**

Run: `./build/tests/core_tests --gtest_filter='SetStroke*CommandTest.*'`  
Expected: FAIL

- [x] **Step 3: 实现五个 Command**

`SetStrokeModeCommand` 私有：`strokeMode_`、`oldStrokeMode_`、`oldDashes_`、`didSeedDashes_`。execute 首次记下旧 mode/dashes；若新 mode 为 Dashed 且归一空则 seed。undo 恢复两者。merge 只吸收新 mode（已 seed 则不再改 oldDashes）。

- [x] **Step 4: 跑测通过**

Expected: PASS

---

### Task 4: 序列化

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/serialization/Dto.h`、`src/serialization/Dto.cpp`（`ToString` / `strokeModeFromString`，`"solid"` / `"dashed"`）
- Modify: `src/serialization/Serializer.cpp` `LayerStyleToJson` / `LayerStyleFromJson`
- Test: `tests/serialization/SerializerTest.cpp`（已有 trim 缺省测附近）

**Interfaces:**
- Consumes: Task 1 字段
- Produces: 可选 JSON `strokeMode` `dashes` `dashOffset`；缺省 Solid / `[]` / 0

- [x] **Step 1: 写失败测试**

Round-trip：Dashed + `{8,4}` + 动画 `dashOffset`。  
缺字段旧 stroke JSON（已有 erase trim 的测）：`strokeMode==Solid`、`dashes.empty()`。

- [x] **Step 2: 跑测失败**

Run: `./build/tests/core_tests --gtest_filter='SerializerTest.*Stroke*'`  
Expected: FAIL 或旧测仍过、新测 FAIL

- [x] **Step 3: 实现**

写出：`{"strokeMode", dto::ToString(stroke.strokeMode)}`、`{"dashes", stroke.dashes}`、`{"dashOffset", AnimatableToJson(stroke.dashOffset)}`。  
读入：字段均 optional；`strokeMode` 非法 → 当缺省 Solid（或返回 error；与 cap 必填不同，跟 trim 一样宽松：未知字符串 `Unexpected`）。`dashes` 须为 number 数组。

- [x] **Step 4: 跑测通过**

Expected: PASS

---

### Task 5: tgfx 适配器 dash + miter

**Status:** ✅ Done

**Files:**
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp` `strokePath`
- Modify: `adapter/tgfx/src/TgfxPathCache.h` / `.cpp`（`DerivedPathCacheKey` 加 `strokeMode`、dash hash、`dashOffset`、`miterLimit`；`BuildPositionedStrokeOutline` 传 `options.miterLimit`）
- Test: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`

**Interfaces:**
- Consumes: `NeedsDash(options.strokeMode, options.dashes)`、`NormalizeDashArray`
- Produces: Dashed 中心线虚线；Center `setStrokeMiter`；Inside/Outside 用 dashed 中心线再 `applyToPath`

在 trim 之后、`isEmpty`/退化点检查之后：

```cpp
if (NeedsDash(options.strokeMode, options.dashes)) {
    const std::vector<float> intervals = NormalizeDashArray(options.dashes);
    auto effect = tgfx::PathEffect::MakeDash(intervals.data(),
        static_cast<int>(intervals.size()), options.dashOffset, false);
    if (effect != nullptr) {
        effect->filterPath(&strokeGeometry);
    }
    if (strokeGeometry.isEmpty()) {
        return;
    }
}
```

Center：`tgfxPaint.setStrokeMiter(options.miterLimit);`（若 tgfx API 名为 `setMiterLimit` 则用实际方法）。  
`tgfx::Stroke(..., options.miterLimit)` 替换当前未传 miter 的构造。  
Solid + 非空 dashes **不得**调用 `MakeDash`。

- [x] **Step 1: 写失败测试**

对标 `StrokeTrimKeepsOnlyPartialSegment`：水平线 y=50、x=20..80，width=6，蓝笔，`strokeMode=Dashed`，`dashes={10,50}`（大 gap），`dashOffset=0`。期望 x=25 附近有蓝、x=50 附近为白背景（具体采样点实现时用 `ReadPixels` 微调，但必须一侧 stroke 色一侧背景）。  
第二测：同样 dashes 但 `strokeMode=Solid` → 中点为蓝色（实线）。

- [x] **Step 2: 跑测失败**

Run: `./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxRenderAdapterTest.StrokeDash*'`  
Expected: Solid 测或可过；Dashed 测 FAIL（整段实线）

- [x] **Step 3: 实现 adapter + cache key**

- [x] **Step 4: 跑测通过**

Expected: PASS（无 Metal 则 `GTEST_SKIP`）

---

### Task 6: PAG 导出

**Status:** ✅ Done

**Files:**
- Modify: `src/export/pag/PagFileBuilder.cpp`（`appendCenterStroke` 及平行层上的 `StrokeElement` / `GradientStrokeElement`）
- Modify: `src/export/pag/PagStrokeOutline.cpp`（Inside/Outside bake：trim 后若 `NeedsDash` 则 `MakeDash` 再 `BuildPositionedOutline`）
- Test: `tests/export/pag/PagExporterTest.cpp`（若现有读回 Stroke 的测，断言 `dashes`）

**Interfaces:**
- Consumes: `NeedsDash`、`NormalizeDashArray`
- Produces: Solid 不写 `pagStroke->dashes`；Dashed 写最多 6 个 `new Property<float>(v)` + `dashOffset = ConvertFloat(...)`；超出 6 截断 + warning

辅助：

```cpp
void AssignPagDashes(pag::StrokeElement *pagStroke, const StrokeStyle &stroke,
                     std::vector<PagExportWarning> *warnings, EntityId layerId);
```

`GradientStrokeElement` 若同样有 `dashes`/`dashOffset` 字段则同样赋值；没有则只 Center 实色 Stroke 写 dash。

- [x] **Step 1: 写失败测试**

导出一层 Center Dashed `{8,8}` 的 shape，decode 后 `StrokeElement::dashes.size()==2`。Solid 带非空 `dashes` 的文档 → PAG stroke `dashes.empty()`。

- [x] **Step 2: 跑测失败**

Run: `ctest --test-dir build -R PagExporter --output-on-failure` 或对应 gtest filter  
Expected: FAIL

- [x] **Step 3: 实现导出 + bake dash**

- [x] **Step 4: 跑测通过**

Expected: PASS

---

### Task 7: SVG 导入

**Status:** 待开始

**Files:**
- Modify: `src/import/svg/SvgStyle.h`：`hasDash` 改为 `std::vector<float> dashes` + `float dashOffset = 0`
- Modify: `src/import/svg/SvgStyle.cpp`：从 `getStrokeDashArray()` 填 `dashes`；若有 `getStrokeDashOffset` 则填 offset
- Modify: `src/import/svg/SvgWalk.cpp`：写入 `StrokeStyle`；有有效 dashes 则 `strokeMode=Dashed`，**删除** `stroke.dash` diagnostic
- Test: `tests/import/svg/SvgImporterTest.cpp`（现有 `stroke.dash` 断言改为检查 `strokeMode`+`dashes`）

**Interfaces:**
- Consumes: Task 1
- Produces: `stroke-dasharray="2 2"` → Dashed + `{2,2}`，无 diagnostic

- [ ] **Step 1: 改现有失败语义的测试**

把 `if (d.code == "stroke.dash")` 换成：导入成功、无该 code、对应 `StrokeStyle::strokeMode==Dashed`、`dashes` 含 2,2。

- [ ] **Step 2: 跑测失败**

Run: `./build/tests/core_tests --gtest_filter='SvgImporterTest.*Dash*'`（或现测名）  
Expected: FAIL（仍 diagnostic / 实线）

- [ ] **Step 3: 映射实现**

`ApplyPaintStyles` 里 `stroke->dashes = style.dashes;` `stroke->dashOffset.setStaticValue(style.dashOffset);` `stroke->strokeMode = NormalizeDashArray(style.dashes).empty() ? Solid : Dashed;`

- [ ] **Step 4: 跑测通过**

Expected: PASS

---

### Task 8: Bridge

**Status:** 待开始

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/BridgeInternals.{h,cpp}`（`MakeLineCap` `MakeLineJoin` `MakeStrokeMode`，非法 clamp）
- Modify: `bridge/src/common/motionstudio_bridge_layer.cpp`（查询）
- Modify: `bridge/src/common/motionstudio_bridge_commands.cpp`
- Test: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Consumes: Task 3 命令
- Produces: 见 spec §4 Bridge 枚举；另：
  - `int ms_layer_style_stroke_dash_count(...)` 失败 0
  - `float ms_layer_style_stroke_dash_at(..., int dashIndex)` 越界 0
  - `void ms_command_set_stroke_dashes(..., const float *values, int count)`；`values==nullptr && count==0` 视为空数组
  - `float ms_layer_style_stroke_miter_limit_at` 失败 0

- [ ] **Step 1: 写失败测试**

对标 `ms_command_set_stroke_position`：null 文档；非 stroke 返回 `MS_*_INVALID`；set cap Round 再 get；非法 99 clamp Butt；set Dashed 空图案后 `dash_count==2`。

- [ ] **Step 2: 跑测失败**

Run: `./build/bridge/bridge_test --gtest_filter='*Stroke*'`  
Expected: FAIL（符号不存在）

- [ ] **Step 3: 实现枚举、get/set、dashes 缓冲拷贝**

set dashes：`std::vector<float>(values, values+count)` 再 `SetStrokeDashPatternCommand`。

- [ ] **Step 4: 跑测通过**

Expected: PASS

---

### Task 9: App UI + 文档

**Status:** 待开始

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Bridge/MotionStudioBridgingExtension.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Bridge/PropertyPath.swift`（`StyleProperty.dashOffset`）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/StrokesInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift`（`dashOffset` 已随 `allCases`）
- Modify: `docs/data-model.md` StrokeStyle 一句补 `strokeMode` / `dashes` / `dashOffset`

**Interfaces:**
- Consumes: Task 8 C API
- Produces: 非文本层 Inspector：Width 下 Cap、Join、条件 Miter、Style(Solid/Dashed)、Dashed 时 Dash/Gap 对 + Offset 关键帧

Miter / Dash 长度用 `NumberPropertyRow(..., showsKeyframeButton: false)`。  
`setStrokeMode(.DASHED)` 走 bridge 命令（内部 seed `{8,8}`）。  
Dashed 至少一对：改 Gap 不得把两段都写成 0（commit 时若 `Normalize` 会空则忽略，保持旧值——Core 命令已 no-op）。

- [ ] **Step 1: 扩展枚举 `label` / `allCases`**

`MS_LINE_CAP`：Butt Round Square。`MS_LINE_JOIN`：Miter Round Bevel。`MS_STROKE_MODE`：Solid Dashed。不含 INVALID。

- [ ] **Step 2: Core 包装**

`strokeCap` `setStrokeCap` `strokeJoin` `setStrokeJoin` `strokeMiterLimit` `setStrokeMiterLimit` `strokeMode` `setStrokeMode` `strokeDashes() -> [Float]` `setStrokeDashes`。

- [ ] **Step 3: StrokesInspector 布局**

顺序见 spec §7。`isTextLayer` 时不显示 Cap/Join/Style/Dash（与 Position/Trim 相同）。

- [ ] **Step 4: 人机验证**

实线↔虚线切换图案保留；Cap Round 端点变圆；Join Miter 显示 Limit；时间轴仅 Offset 可关键帧。验证后把本 Task **Status** 标 ✅。

---

## Spec 覆盖

| Spec | Task |
|---|---|
| StrokeMode + dashes 分离 | 1–3 |
| 求值 StrokeOptions + miterLimit | 2、5 |
| MakeDash 顺序 Trim→Dash | 5 |
| JSON 可选 / 不升 schema | 4 |
| PAG / SVG | 6、7 |
| Cap Join Miter UI + Solid/Dashed | 8、9 |
| 文本/LayerFx 非目标 | 9 隐藏；不改 DrawText / LayerFx |

无占位 TBD。类型名全程 `StrokeMode` / `strokeMode` / `SetStrokeModeCommand` / `MS_STROKE_MODE`。
