# NumberPropertyRow 步进按钮 — 设计说明

日期：2026-08-03  
状态：已确认，待实现计划  

## 目标

在 Inspector 数字行提供 **↑ / ↓** 微调（每次 ±1），减少手输。默认关闭；仅对需要像素级微调的字段开启。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 布局 | `label \| TextField \| ↑↓ \| ◆(关键帧)` |
| 键盘 | 本组件不拦截方向键（Figma 画布方向键挪图层为独立能力，非本需求） |
| 步进 | 固定 `step = 1`（`Float`） |
| 开关 | `showsStepButtons: Bool = false` |
| 开启范围 | Transform：Anchor X/Y、Position X/Y；Shape Size：Width / Height |
| 关闭范围 | Scale、Rotation、Opacity、Shape Radius、以及其余所有 `NumberPropertyRow` 调用方 |
| 不可编辑 | 步进按钮 `disabled`，与关键帧按钮一致 |

## 非目标

- 数字框聚焦时用键盘上下/左右微调
- 选中图层方向键平移（画布行为）
- Shift 加速（×10 等）
- 按属性配置不同 step（预留 `step` 默认 1 即可，调用方本次不传）
- 在 `NumberPropertyRow` 内做 undo merge（沿用各 Inspector 现有 `onCommit` / merge 约定）

## 核心接口

```swift
struct NumberPropertyRow: View {
    // 既有：label, value, hasKeyframeAtPlayhead, isEditable,
    //       showsKeyframeButton, onCommit, onToggleKeyframe

    var showsStepButtons = false
    var step: Float = 1

    // 内部：
    // nudge(+1) / nudge(-1)
    //   base = parsedDraft() ?? value
    //   next = base ± step
    //   更新 draft → onCommit(next)（若 next != value）
}
```

### 调用方改动

| 文件 | 行 | `showsStepButtons` |
|---|---|---|
| `TransformInspector.swift` | Anchor X/Y、Position X/Y | `true` |
| `ShapeSizeInspector.swift` | Width、Height | `true` |
| 其它 | — | 默认 `false`（不改） |

## 行为

1. 点击 ↑：`value/draft + step` 后立即提交（走 `onCommit`）。  
2. 点击 ↓：同上 − `step`。  
3. 草稿非法：以模型 `value` 为基准步进。  
4. `!isEditable`：按钮禁用。  
5. 与键帧按钮并存时，步进在左、键帧在右。

## 实现顺序（概要）

1. `NumberPropertyRow` 增加 `showsStepButtons` / `step` 与 ↑↓ UI + `nudge`  
2. Transform / ShapeSize 对应行打开开关  
3. 编译通过；人机点按验收  

## 验收

1. Transform Position：点 ↑ 后 X（或对应轴）增加 1，画布/数值同步。  
2. Shape Width：点 ↓ 后宽度减 1。  
3. Rotation / Opacity / Radius：无步进按钮。  
4. 锁定层（`isEditable == false`）：步进按钮不可点。  
