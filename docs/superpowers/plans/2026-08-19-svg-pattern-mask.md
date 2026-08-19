# SVG Pattern Fill and Mask Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Figma 式 `fill="url(#pattern)"` 单图覆盖填充建成 `Image` 层；简单 `mask=` 写成 `Layer.masks`（与现有 clip-path 同路径）；均匀圆角走 `ShapeRect` / `cornerRadius` 而不是 path mask。

**Architecture:** 不改 Core / schema / bridge。`AddShapeLayer` 在写 FillStyle 之前尝试解析 pattern；共用 `WalkImage` 的 data URI 解码。`ApplyNodeEffects` 在 clip-path 之后尝试 `mask`，失败才报 `mask.skipped`。

**Tech Stack:** C++17、GoogleTest、现有 `svg_import` + tgfx `SVGDOM`。

**Spec:** [`docs/superpowers/specs/2026-08-19-svg-import-design.md`](../specs/2026-08-19-svg-import-design.md) §6.1、§7.5、§7.5.1、§8.3。

## Global Constraints

- 不改 Core 模型、schema、bridge、App。
- 禁止 `dynamic_cast` 与 C++ 异常；SVG 子类用 `tag()` + `static_cast`。
- 禁止 lambda；`if`/`switch` 分支必须 `{}`。
- Expected 用 `hasValue()` / `error()`。
- 同一 data URI 共用一个 `Asset`。
- 平铺 pattern、复杂 mask、filter 仍跳过。

## File Map

| 文件 | 职责 |
|---|---|
| `src/import/svg/SvgWalk.cpp` | pattern → Image；mask/clip → `cornerRadius` 或 `Layer.masks`；均匀 rect → `ShapeRect` |
| `src/import/svg/SvgTransform.cpp` | `ShapeRect` 局部包围盒；残差 bake 时转 Path |
| `tests/import/svg/SvgImporterTest.cpp` | 库测 |
| `docs/superpowers/specs/2026-08-19-svg-import-design.md` | 已改映射表 |

1×1 PNG data URI（与现有 `DataUriImageCreatesAsset` 相同）：

```
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==
```

---

### Task 1: pattern fill → Image 层

**Status:** ✅ Done

**Files:**
- Modify: `src/import/svg/SvgWalk.cpp`
- Test: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: `WalkImage` 解码路径、`ApplySvgMatrixToLayer`、`ShapePathFromNode`、`PathToVectorNetwork`
- Produces: `AddShapeLayer` 在简单 image pattern 时创建 `LayerType::Image`（可另留 stroke Shape）；`paint.unresolved` 仅用于无法导入的 pattern

- [x] **Step 1: 写失败测试**

`PatternImageFillCreatesImageLayer`：`<pattern width="1" height="1" patternContentUnits="objectBoundingBox">` + 内嵌 data URI `<image width="1" height="1">` + `rect fill="url(#p)"`。断言有 `LayerType::Image`、`assets` / `embeddedImages` 非空、无 `paint.unresolved`。

`PatternUseImageFillCreatesImageLayer`：pattern 内 `<use href="#img">`，defs 里 `<image id="img" href="data:...">`（Figma 结构）。同样断言 Image 层。

`TiledPatternFillStaysUnresolved`：`pattern width="0.5"` → 仍有 `paint.unresolved`，无 Image 层。

- [x] **Step 2: 跑测试确认失败**

```
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.Pattern*'
```

Expected: FAIL（用例不存在或 Image 层未建）。

- [x] **Step 3: 最小实现**

`WalkContext` 增加 `std::unordered_map<std::string, EntityId> imageAssetByHref`。抽出 `ImportDataUriImage`（解码 + 写 asset/embedded，命中缓存则复用）。

`TryImportPatternFill`：mapper 找 Pattern → 单子 Image 或 Use→Image → data URI → tile 覆盖盒 → 建 Image 层，矩阵见 spec §8.3，形状 path `transform(inverse(patternUserMatrix))` 写入 `masks[0]`。成功则 `AddShapeLayer` 清掉 fill；无 stroke 则不再建 Shape。

- [x] **Step 4: 跑测试确认通过**

```
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。

- [x] **Step 5: 勾选本 Task 并更新 spec/plan 状态**

---

### Task 2: mask 属性 → Layer.masks

**Status:** ✅ Done

**Files:**
- Modify: `src/import/svg/SvgWalk.cpp`（`ApplySkippedEffects` / `ApplyNodeEffects`）
- Test: `tests/import/svg/SvgImporterTest.cpp`（改 `MaskAttributeIsSkipped`）

**Interfaces:**
- Consumes: Task 1 的 walk 上下文；现有 `ApplyClipPath` 的「单形状容器 → VectorNetwork」逻辑
- Produces: 简单 `mask=` 写入 `layer.masks`；仅复杂 mask 报 `mask.skipped`

- [x] **Step 1: 写失败测试**

把 `MaskAttributeIsSkipped` 改成 `MaskAttributeBecomesMask`：单 rect mask → `masks.size()==1`，无 `mask.skipped`。

新增 `ComplexMaskAttributeIsSkipped`：mask 内两个 rect → `mask.skipped`，`masks` 为空。

- [x] **Step 2: 跑测试确认失败**

```
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.Mask*'
```

Expected: `MaskAttributeBecomesMask` FAIL（仍跳过）。

- [x] **Step 3: 最小实现**

抽出 `CollectSingleMaskShape(container)`（与 clip 相同：禁止 Use/G/Svg，恰好一个 shape）。`ApplyMask`：IRI → `SVGMask`，`maskContentUnits` 必须是 UserSpaceOnUse，单形状 → `masks.push_back({path, Add})`。成功则不报 `mask.skipped`；否则保持现诊断。`ApplyNodeEffects`：`ApplyClipPath` 然后 `ApplyMask`，filter 仍 skip。

- [x] **Step 4: 跑测试确认通过**

```
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。

- [x] **Step 5: 勾选本 Task**

---

### Task 3: 均匀圆角 → ShapeRect / cornerRadius

**Status:** ✅ Done

**Files:**
- Modify: `src/import/svg/SvgWalk.cpp`、`src/import/svg/SvgTransform.cpp`
- Test: `tests/import/svg/SvgImporterTest.cpp`
- Docs: spec §6.1 / §7.5 / §8.3；本 plan；`2026-08-19-svg-import.md` 约束

**Interfaces:**
- Consumes: `SVGRect` rx/ry、pattern `inverse`、现有 `ApplyClipPath` / `ApplyMask`
- Produces: 均匀 rect → `ShapeRect`；填满图容器的圆角 pattern → `ImageContent.cornerRadius`；组上均匀圆角 clip/mask → `NullContent.cornerRadius`；其余仍 path mask

- [x] **Step 1: 写失败测试**

- `RectIsShapeRectCircleEllipseArePaths`：直角 rect → `ShapeType::Rect` 中心 position；circle/ellipse 仍 Path
- `UniformRoundedRectBecomesShapeRect`：`rx="8"` → `cornerRadius==8`，无 mask
- `UnequalRadiusRectStaysPath`：`rx≠ry` → Path，无 mask
- `PatternRoundedRectUsesImageCornerRadius`：圆角 pattern 填满图 → `image.cornerRadius`，`masks` 空
- `PatternOffsetImageKeepsPathMask`：图大于填色盒 → 仍 mask
- `RoundedGroupClipUsesCornerRadius`：组 clip 均匀圆角 rect → `NullContent.cornerRadius`，无 mask
- `SimpleClipPathBecomesMask`：直角小 clip 仍 mask

- [x] **Step 2: 跑测试确认失败**

```
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.RectIsShapeRect*:SvgImporterTest.UniformRounded*:SvgImporterTest.PatternRounded*:SvgImporterTest.RoundedGroup*'
```

Expected: FAIL（仍是 Path / 仍写 mask）。

- [x] **Step 3: 最小实现**

`ResolveSvgRectGeom` + `SvgRectHasUniformRadius`。`AddShapeLayer` 均匀 rect 建 `ShapeRect`。`TryImportPatternFill`：`size` = 宿主图形盒，忽略 pattern 内 transform，`scaleMode = Stretch`；均匀圆角写 `cornerRadius`。`ApplyClipPath` / `ApplyMask`：组上均匀圆角 → `NullContent.cornerRadius`。`LayerLocalBounds` 覆盖 `ShapeRect`；残差 bake 时转 Path。

- [x] **Step 4: 跑测试确认通过**

```
./build/src/import/svg/svg_import_tests
```

Expected: PASS。

- [x] **Step 5: 勾选本 Task 并更新 spec/plan**
