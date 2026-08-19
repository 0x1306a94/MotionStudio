# Stroke Dash + Cap / Join UI — 设计说明

日期：2026-08-19  
状态：已确认  
关联：[Stroke Trim](./2026-07-28-stroke-trim-design.md)、[SVG 导入](./2026-08-19-svg-import-design.md)、[PAG 导出](./2026-07-31-pag-export-design.md)、[rendering.md](../../rendering.md)、[data-model.md](../../data-model.md)  
实现计划：[2026-08-19-stroke-dash.md](../plans/2026-08-19-stroke-dash.md)

## 结论（现状）

**路径描边（`Layer::styles` 里的 `StrokeStyle`）目前只能画实线。** Dash 不是适配器漏接，而是 Core 到 `StrokeOptions` 整条链都没有该字段。

| 层 | 现状 |
|---|---|
| 模型 `StrokeStyle` | 有 width / cap / join / miter / position / trim；**无 dash** |
| 求值 `StrokeOptions` | width / cap / join / position / trim；**无 dashes、无 miterLimit** |
| 适配器 `TgfxCanvasAdapter::strokePath` | Center：`PaintStyle::Stroke`；Inside/Outside：`Stroke::applyToPath` + 布尔。Trim 用 `PathEffect::MakeTrim`。**从不调用 `MakeDash`** |
| tgfx | **已有** `PathEffect::MakeDash(intervals, count, phase, adaptive=false)` |
| PAG / PAGX | `StrokeElement::dashes`（最多 6 段）+ `dashOffset`；PAGX `Stroke` 还有 `dashAdaptive` |
| SVG 导入 | 读到 `stroke-dasharray` 后记 `stroke.dash` diagnostic，**仍写成实线** |
| Inspector | Width + Trim + Position；**无** dash / offset / cap / join / miter |
| Layer Style `LayerStrokeStyle` | 对整层结果图做栅格描边，与路径 dash **无关** |
| 文本层 stroke | `DrawText` 路径，本阶段不画 dash |

实现应复用 Trim 的分工：**Core 只存/求值，几何效果放在 tgfx 适配器**（以及 PAG 导出里同一套 bake）。

## 目标

1. Shape 层路径描边支持虚线：间隔数组 + 可动画偏移。
2. 与已有 Trim、Inside/Outside、渐变/着色描边组合正确。
3. 文档 round-trip；PAG 导出能带上 dash；SVG 导入不再丢掉 `stroke-dasharray`。
4. Inspector：**Solid / Dashed 分段切换**；虚线时 Dash / Gap（可多对）+ Offset 关键帧；**Cap / Join**；Join=Miter 时显示 Miter Limit。

## 非目标

- `dashAdaptive`（PAGX「等比缩放使虚线均分」）；预览与导出一律 `adaptive=false`
- 每一段 dash/gap 长度单独打关键帧（PAG 支持最多 6 个 `Property<float>`；本阶段数组静态）
- Layer Style（`LayerStrokeStyle`）虚线
- 文本层 glyph stroke 虚线
- 命中检测按虚线镂空（仍按完整描边宽度）
- Core 自研弧长 dash / 把 dash 做成独立 `DrawCommand`
- 升 `schemaVersion`（缺省字段 = 实线，兼容旧文档）
- Cap / Join / Miter Limit 打关键帧（与 Position / Blend 一样静态）
- 文本层 glyph stroke 的 cap/join（`TextDrawStyle` 只有 width；UI 与 Trim 一样隐藏）

---

## 已锁定决策

| 项 | 选择 |
|---|---|
| 挂载 | `StrokeStyle`（路径描边），不进 `LayerFx` |
| 实线/虚线 | 显式 `StrokeMode::{Solid, Dashed}`（默认 Solid）；**不**用空 `dashes` 兼作开关 |
| 间隔 | 静态 `std::vector<float> dashes`（px）；Solid 时仍保留，切回虚线不丢图案 |
| 偏移 | `Animatable<float> dashOffset`（px；可负）；Solid 时仍保留，求值照抄但不生效 |
| 绘制 dash | **仅** `strokeMode == Dashed` 且归一后数组有效时调用 `MakeDash` |
| 切到 Dashed 且数组无效 | 同一条命令把 `dashes` 写成 `{8, 8}`（undo 一并恢复） |
| 切到 Solid | 只改 `strokeMode`，不清 `dashes` / `dashOffset` |
| 空数组 / 归一后不足 2 段 / 总和 ≤ 0 | 视为无效图案；Dashed 下看起来像实线（异常态），UI 不允许删光最后一对 |
| 奇数长度 | SVG 规则：再拼一份自身变成偶数 |
| 负间隔 | clamp 到 0 |
| 上限 | 模型不截断；**PAG 导出只写前 6 段**（libpag codec 上限），超出记 warning；Solid 导出不写 dashes |
| 效果顺序 | **先 Trim，再 Dash，再 stroke / Inside-Outside 布尔** |
| 适配器 | `tgfx::PathEffect::MakeDash`，与 Trim 同样作用在 stroke 中心线路径上 |
| 绘制 | Center：dashed 中心线 + `PaintStyle::Stroke`；Inside/Outside：dashed 中心线 `applyToPath` 后再与填充轮廓 Intersect/Difference |
| schema | 不升版；`strokeMode` 缺省 = `solid`；`dashes` / `dashOffset` 缺省 = `[]` / `0` |
| 文本层 UI | 与 Trim / Position 一样不展示 strokeMode、dash 参数、cap、join |
| PropertyPath | `styles[i].dashOffset`（float 通道）；`strokeMode` / `dashes` 用专用 undo 命令，不上时间轴 |
| Cap / Join | 已有 `StrokeStyle::{cap,join,miterLimit}`；新增专用命令 + Bridge 枚举，对标 `SetStrokePositionCommand` |
| Miter Limit | 静态 `float`，默认 4；**写入 `StrokeOptions` 并下到 tgfx**（Center `setStrokeMiter`；Inside/Outside `tgfx::Stroke` 构造），Join≠Miter 时 UI 隐藏、值仍保留 |

---

## 1. 当前流水线（dash 插入点）

```
StrokeStyle
    → SceneEvaluator::appendStroke → StrokeOptions + StrokePath 命令
        → TgfxCanvasAdapter::strokePath
              1. Resolve 中心线（优先 geometry.strokePath）
              2. NeedsTrim → MakeTrim
              3. Center: drawPath(Stroke)  |  Inside/Outside: applyToPath → boolean
```

缺口：步骤 2 之后没有 dash。PAG 导出的 `BakePositionedStrokeOutline` 同样只 trim、不 dash。

对照：

- Trim：裁可见弧长窗口（0–1 + 角度 offset）
- Dash：在**当前中心线**上按 px 开关间隔（phase = `dashOffset`）

二者正交。先 trim 再 dash：虚线画在剩余可见段上，实现与现缓存结构一致。不采用「先 dash 再 trim」（那会让 trim 窗口去切已打孔的图案，Inside/Outside bake 更绕）。

---

## 2. 数据模型

```cpp
enum class StrokeMode : uint8_t {
    Solid = 0,
    Dashed = 1,
};

class StrokeStyle : public LayerStyle {
    // ... 现有 paint / width / cap / join / miter / position / trim ...
    StrokeMode strokeMode = StrokeMode::Solid;
    std::vector<float> dashes;                 // 图案；与 strokeMode 独立保存
    Animatable<float> dashOffset{0.0f};        // 沿图案的相位，单位 px
};
```

```cpp
struct StrokeOptions {
    float width = 0;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.0f;  // 本阶段补进快照；此前适配器一直用 tgfx 默认 4
    StrokePosition position = StrokePosition::Center;
    float trimStart = 0;
    float trimEnd = 1;
    float trimOffset = 0;
    StrokeMode strokeMode = StrokeMode::Solid;
    std::vector<float> dashes;
    float dashOffset = 0;
};
```

归一化（求值进 `StrokeOptions` 前做一次，适配器与导出共用同一 helper，放 Core 以免 adapter/export 各写一份）：

```cpp
// 伪代码
std::vector<float> NormalizeDashArray(std::vector<float> dashes) {
    for (float& value : dashes) value = std::max(0.0f, value);
    if (dashes.size() % 2 == 1) {
        dashes.insert(dashes.end(), dashes.begin(), dashes.end());
    }
    float sum = 0;
    for (float value : dashes) sum += value;
    if (dashes.size() < 2 || sum <= 0) return {};
    return dashes;
}

bool NeedsDash(const StrokeOptions& options) {
    return options.strokeMode == StrokeMode::Dashed &&
           !NormalizeDashArray(options.dashes).empty();
}
```

`SceneEvaluator::appendStroke` 把 `strokeMode`、`dashes`、`dashOffset.evaluatePreview(time)` 与 `miterLimit` 填进 `StrokeOptions`。

默认新描边：`strokeMode = Solid`，`dashes = {}`，与今天实线一致。UI 切到 Dashed 时若图案无效则写成 `{8, 8}`（不作为文件格式常量）。

---

## 3. 适配器

在 `TgfxCanvasAdapter::strokePath` 中，trim 之后、空路径/退化点判断之后：

```cpp
// 伪代码（插在现有 ResolveTrimmed 之后）
if (NeedsDash(options)) {
    const auto intervals = NormalizeDashArray(options.dashes);
    auto effect = tgfx::PathEffect::MakeDash(
        intervals.data(), static_cast<int>(intervals.size()),
        options.dashOffset, /*adaptive=*/false);
    if (effect) {
        effect->filterPath(&strokeGeometry);
    }
    if (strokeGeometry.isEmpty()) return;
}
```

之后逻辑不变：Center `drawPath`；Inside/Outside `ResolvePositionedOutline`（**必须把 dash 后的中心线**交给 `applyToPath`，不能对实心环再 dash）。

Center 路径补 `setStrokeMiter(options.miterLimit)`。Inside/Outside 的 `tgfx::Stroke` 把 `options.miterLimit` 传进构造函数（不再吃默认 4）。

`TgfxPathCache` 的 `DerivedPathCacheKey` presently 含 trim + position + width + cap + join。`strokeMode`、归一后的 `dashes`、`dashOffset`、`miterLimit` 都会改变派生路径，须编进 key。

Line cap 作用在**每一段 dash 端点**（tgfx 对打孔后的开放轮廓 stroke），这是预期。

---

## 4. 序列化 / undo / PropertyPath

`document.json` stroke 对象增加可选字段：

```json
"strokeMode": "dashed",
"dashes": [8, 4, 2, 4],
"dashOffset": { "static": 0 }
```

缺省或非法 `strokeMode` → `solid`；缺 `dashes` / `dashOffset` → `[]` / `0`。不升 `schemaVersion`。Solid 文档仍可带上 `dashes`（切回虚线用）。

- `strokeMode`：`SetStrokeModeCommand`。切到 `Dashed` 且当前 `NormalizeDashArray` 为空时，**同一命令**把 `dashes` 写成 `{8,8}` 并记下旧数组，undo 恢复 mode + 数组。切到 `Solid` 只改 mode。
- `dashOffset`：走现有 float 属性通道（`resolveStyleProperty` 增加 `"dashOffset"`），Inspector/时间轴与 width 相同。
- `dashes`：`SetStrokeDashPatternCommand(layerId, styleIndex, vector<float>)`，可 merge；不进时间轴。Dashed 时拒绝写成无效数组（no-op 或保持上一有效值）。
- `cap` / `join`：`SetStrokeCapCommand` / `SetStrokeJoinCommand`，形态与 `SetStrokePositionCommand` 相同（首次 execute 记下旧值，同 target merge）。
- `miterLimit`：`SetStrokeMiterLimitCommand`（静态 float，可 merge）；不进 float PropertyPath / 时间轴。

Clone / 复制图层：随 `StrokeStyle` 值拷贝即可（vector + Animatable + 枚举）。

### Bridge / Swift 枚举

对标 `MS_STROKE_POSITION`：

```c
typedef CF_CLOSED_ENUM(int, MS_LINE_CAP) {
    MS_LINE_CAP_INVALID = -1,
    MS_LINE_CAP_BUTT = 0,
    MS_LINE_CAP_ROUND = 1,
    MS_LINE_CAP_SQUARE = 2,
};
typedef CF_CLOSED_ENUM(int, MS_LINE_JOIN) {
    MS_LINE_JOIN_INVALID = -1,
    MS_LINE_JOIN_MITER = 0,
    MS_LINE_JOIN_ROUND = 1,
    MS_LINE_JOIN_BEVEL = 2,
};
typedef CF_CLOSED_ENUM(int, MS_STROKE_MODE) {
    MS_STROKE_MODE_INVALID = -1,
    MS_STROKE_MODE_SOLID = 0,
    MS_STROKE_MODE_DASHED = 1,
};
```

查询：`ms_layer_style_stroke_cap_at` / `_join_at` / `_miter_limit_at` / `_stroke_mode_at`（失败枚举返回 `INVALID`，miter 返回 0 并靠 cap/join 哨兵判断）。  
命令：`ms_command_set_stroke_cap` / `_join` / `_miter_limit` / `_stroke_mode`；非法标签 clamp 到 Butt / Miter / Solid。

Swift：`MotionStudioBridgingExtension` 给 `MS_LINE_CAP` / `MS_LINE_JOIN` / `MS_STROKE_MODE` 补 `CaseIterable` + `label`（Butt / Round / Square；Miter / Round / Bevel；Solid / Dashed）。`allCases` 不含 `.INVALID`。

---

## 5. PAG 导出

`pag::StrokeElement` / `GradientStrokeElement` 已有 `dashes` + `dashOffset`（codec 最多 6 段）。

| MS 情况 | 导出 |
|---|---|
| Solid | 不写 `dashes` / `dashOffset`（PAG 实线） |
| Center 且无 trim，Dashed | 主层 `Stroke`/`GradientStroke` 写 dashes + dashOffset |
| Center + trim，Dashed | 现有平行层（Path + TrimPaths + Stroke）上同样写 dashes |
| Inside/Outside | `BakePositionedStrokeOutline`：Dashed 时 **trim 后 dash 再 expand+boolean** |

`dashOffset` 用现有 `ConvertFloat`。间隔静态，每段 `new Property<float>(value)`。超过 6 段截断并 warning。Shader 描边仍按现状跳过。

---

## 6. SVG 导入

废止「有 dasharray 就 diagnostic + 实线」。

- `stroke-dasharray` 非空 → `strokeMode = Dashed`，`dashes` = 该数组（奇数补偶）
- 无 dasharray 或空 → `strokeMode = Solid`
- `stroke-dashoffset` → `dashOffset` 静态值（tgfx 节点若无 getter 则保持 0）
- 成功映射后 **不再** 发 `stroke.dash`
- 导入后 `position = Center`（SVG 无内外描边）

须改：`SvgStyle` 从 `hasDash` bool 改为带上数组；`SvgWalk` 写入 `StrokeStyle`；更新 `SvgImporterTest`。

[SVG 导入 spec](./2026-08-19-svg-import-design.md) 中「本阶段跳过 dash」以本文为准。

---

## 7. App UI

`StrokesInspector`（非文本层），在 Width 与 Trim 之间，顺序固定：

1. **Cap**：分段 `Picker`（读到 `.INVALID` → `.BUTT`）
2. **Join**：分段 `Picker`（读到 `.INVALID` → `.MITER`）
3. **Miter**：仅 `join == .MITER` 时显示 `NumberPropertyRow`，无关键帧菱形
4. **Style**：分段 `Picker` Solid / Dashed（读到 `.INVALID` → `.SOLID`）
5. 仅 `strokeMode == .DASHED` 时：
   - **Dash / Gap**：编辑 `dashes[0]`、`dashes[1]`；更长时按对展开
   - 「+」增加一对（默认再追加 `8, 8`）；**至少保留一对**，不能清空成实线
   - **Offset**：`NumberPropertyRow` + 关键帧，path `styles[n].dashOffset`

Position 行保持现状（Width 之上）。Cap / Join / Style 用 `.segmented`。

时间轴：仅 `dashOffset` 可上轨道（有关键帧时）。`strokeMode` / `dashes` / cap / join / miter 不上时间轴。

---

## 8. 测试

| 测例 | 期望 |
|---|---|
| `SceneEvaluator` 把 dashes / dashOffset 抄进 `StrokeOptions` | 值相等 |
| Serializer round-trip；缺 `strokeMode` 旧文档 | Solid |
| Solid 且 `dashes` 非空 | 预览/PAG 仍为实线；切回 Dashed 用原数组 |
| 切 Solid→Dashed 且 dashes 空 | 变成 `{8,8}`；undo 回到 Solid + 空数组 |
| `NormalizeDashArray`：`[5]` → `[5,5]`；`[0,0]` → 无效 | |
| Adapter：水平线 `dashes=[10,10]` width 足够粗 | 中点附近一侧重色、一侧背景（对标 Trim 回归） |
| Trim 半段 + dash | 只在可见半段出现虚线 |
| Inside/Outside + dash | 轮廓为虚线段的内外环，不是实心环打孔 |
| PAG：Dashed Center 出现在 `StrokeElement::dashes`；Solid 不写 dashes | |
| SVG：`stroke-dasharray="2 2"` → Dashed + dashes，无 `stroke.dash` diagnostic | |
| undo：改 strokeMode / pattern / offset / cap / join / miter 可撤销 | |
| `SceneEvaluator` 抄 `miterLimit`；Center 预览尖角随 miter 变化 | |

---

## 9. 文件改动（实现时）

| 路径 | 职责 |
|---|---|
| `include/MotionStudio/model/StrokeMode.h` + `LayerStyle.h` | `StrokeMode` + `dashes` / `dashOffset` |
| `include/MotionStudio/render/StrokeOptions.h` | 求值快照 |
| `src/render/` helper（新小文件或放现有 stroke util） | `NormalizeDashArray` / `NeedsDash` |
| `src/render/SceneEvaluator.cpp` | 填 options |
| `src/model/PropertyPath.cpp` | `dashOffset` |
| `src/undo/` + CommandKind | `SetStrokeModeCommand`、`SetStrokeDashPatternCommand`、`SetStrokeCapCommand`、`SetStrokeJoinCommand`、`SetStrokeMiterLimitCommand` |
| `src/serialization/Serializer.cpp` | 可选 JSON |
| `adapter/tgfx/src/TgfxCanvasAdapter.cpp` + `TgfxPathCache.*` | MakeDash + cache key |
| `src/export/pag/PagFileBuilder.cpp` + `PagStrokeOutline.cpp` | 导出 / bake |
| `src/import/svg/SvgStyle.cpp` `SvgWalk.cpp` | 映射数组 |
| `bridge/include/motionstudio_bridge.h` + 实现 / `bridge_test` | `MS_LINE_CAP` / `MS_LINE_JOIN` / `MS_STROKE_MODE` + get/set |
| `apps/.../StrokesInspector.swift` `MotionStudioBridgingExtension.swift` `PropertyPath.swift` `TimelineSupport.swift` | UI |
| `docs/data-model.md` | StrokeStyle 补 dashes |
| 测试 | 上表 |

Bridge：float 通道已覆盖 `dashOffset`；`dashes` 需 C API（例如 `ms_stroke_dashes_get/set`）或经现有 blob——实现 plan 里按现有 style 专用 API 风格补，不把 `vector<float>` 塞进通用 float path。

---

## 进度

文档已确认 → 实现计划见 [2026-08-19-stroke-dash.md](../plans/2026-08-19-stroke-dash.md)。
