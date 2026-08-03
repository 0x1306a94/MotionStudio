# Number Step Buttons + Position Nudge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **每完成一个 Step/Task 必须立刻把本文件对应勾选改为 `[x]` 并更新 Task Status，随 commit 提交（见 AGENTS.md「按 plan 实现」）。**

**Goal:** Inspector 对 Anchor/Position/Shape Size 提供 ↑↓ ±1 步进；编辑器方向键对选中层做合成空间 Position ±1 微移（多选同位移 + undo merge）。

**Architecture:** `NumberPropertyRow` 可选步进按钮走既有 `onCommit`；键盘 nudge 复用 `mapCompositionDelta`，经 `MotionDocumentCore.nudgeLayersPosition` 写存储 position；`EditorViewController` 注册箭头 `UIKeyCommand` 并包 merge + `perform`。

**Tech Stack:** SwiftUI Inspector、UIKit `UIKeyCommand`、`MotionDocumentCore`、既有 Bridge `ms_layer_map_composition_delta`。

**Spec:** `docs/superpowers/specs/2026-08-03-number-step-buttons-design.md`

## Global Constraints

- 步进按钮布局：`TextField | ↑↓ | ◆`；默认 `showsStepButtons = false`。
- 开启：Transform Anchor X/Y、Position X/Y；Shape Size Width/Height。关闭：Scale/Rotation/Opacity/Radius 等。
- NumberPropertyRow **不**处理方向键。
- 方向键：←Δx−1 →Δx+1 ↑Δy−1 ↓Δy+1（合成空间）；只改 `transform.position`。
- 锁定/隐藏：nudge 仍移动（同 Align）。
- Undo：`beginMergeGroup` → 写各层 → `endMergeGroup` + `perform("Nudge Position")`。
- 提交：每任务结束 commit（不推送）；英语一句、句号结尾。
- 优先 Xcode MCP `BuildProject` / 测试；不可用再 `xcodebuild`。

## File Map

| 区域 | 文件 |
|---|---|
| 步进 UI | Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/NumberPropertyRow.swift` |
| 开启调用 | Modify: `TransformInspector.swift`、`ShapeSizeInspector.swift` |
| Nudge 逻辑 | Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift` |
| 快捷键 | Modify: `EditorViewController.swift`、`EditorViewController+Commands.swift` |
| Spec 状态 | Modify: `docs/superpowers/specs/2026-08-03-number-step-buttons-design.md` |
| 本 plan | Modify: `docs/superpowers/plans/2026-08-03-number-step-buttons.md` |

---

### Task 1: `NumberPropertyRow` 步进按钮

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/NumberPropertyRow.swift`

**Interfaces:**
- Consumes: 既有 `value` / `onCommit` / `isEditable` / `parsedDraft` / `formattedValue`
- Produces:
  - `var showsStepButtons = false`
  - `var step: Float = 1`
  - 内部 `nudge(direction: Float)`（`+1` / `-1`）

- [x] **Step 1: Add parameters + nudge + UI**

在 `showsKeyframeButton` 旁增加：

```swift
var showsStepButtons = false
var step: Float = 1
```

在 `TextField` 与 keyframe 按钮之间插入（仅 `showsStepButtons`）：

```swift
if showsStepButtons {
    VStack(spacing: 0) {
        Button { nudge(1) } label: {
            Image(systemName: "chevron.up")
                .font(.system(size: 9, weight: .semibold))
        }
        .buttonStyle(.plain)
        .disabled(!isEditable)

        Button { nudge(-1) } label: {
            Image(systemName: "chevron.down")
                .font(.system(size: 9, weight: .semibold))
        }
        .buttonStyle(.plain)
        .disabled(!isEditable)
    }
    .foregroundStyle(isEditable ? Color.secondary : Color.secondary.opacity(0.42))
    .frame(width: 18)
    .accessibilityElement(children: .contain)
}
```

```swift
private func nudge(_ direction: Float) {
    guard isEditable else { return }
    let base = parsedDraft() ?? value
    let next = base + direction * step
    guard next.isFinite else { return }
    draft = formattedValue(next)
    hasInvalidDraft = false
    if next != value {
        onCommit(next)
    }
}
```

为按钮设 accessibilityLabel：`"Increment"` / `"Decrement"`。

- [x] **Step 2: Build**（Xcode MCP `BuildProject` SUCCEEDED）

- [x] **Step 3: Update this plan** — Task 1 Done。

- [x] **Step 4: Commit**

---

### Task 2: 在 Transform / ShapeSize 开启步进

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/ShapeSizeInspector.swift`

**Interfaces:**
- Consumes: `showsStepButtons`
- Produces: Anchor/Position/Width/Height 行开启；其它行保持默认

- [x] **Step 1: Wire `showsStepButtons: true`**（Anchor/Position + Width/Height）

- [x] **Step 2: Build**（SUCCEEDED）

- [x] **Step 3: Update this plan** — Task 2 Done。

- [x] **Step 4: Commit**

---

### Task 3: `MotionDocumentCore.nudgeLayersPosition`

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（紧邻 `alignLayers`）

**Interfaces:**
- Consumes: `mapCompositionDelta`、`evaluateVec2` / `setStaticVec2` / `addKeyframeVec2`、`TransformProperty.position`
- Produces: `nudgeLayersPosition(compositionID:layerIDs:delta:frame:)`

- [x] **Step 1: Implement**（写存储 AE position + `mapCompositionDelta`）

- [x] **Step 2: Build**（SUCCEEDED）

- [x] **Step 3: Update this plan** — Task 3 Done。

- [x] **Step 4: Commit**

---

### Task 4: 方向键 → merge nudge

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift`（`keyCommands` / `canPerformAction`）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift`

**Interfaces:**
- Consumes: `nudgeLayersPosition`、`selectedLayerIDs`、`playheadClock.frame`、`beginMergeGroup`/`endMergeGroup`、`perform`
- Produces: 四向 `UIKeyCommand` + `nudgeSelection(dx:dy:)`

- [x] **Step 1: Add key commands** + `canPerformAction`（有选中才可）

- [x] **Step 2: Implement actions**（merge + `perform("Nudge Position")`）

- [x] **Step 3: Build**（SUCCEEDED）

- [x] **Step 4: Update this plan** — Task 4 Done。

- [x] **Step 5: Commit**

---

### Task 5: Spec 状态 + 验收

**Status:** 🔄 in progress（待人机验收）

**Files:**
- Modify: `docs/superpowers/specs/2026-08-03-number-step-buttons-design.md`
- Modify: `docs/superpowers/plans/2026-08-03-number-step-buttons.md`

- [x] **Step 1: Spec 状态改为** `已实现（待人机验收）`

- [ ] **Step 2: Manual checklist**

1. Transform Position：↑ 按钮 → 轴 +1  
2. Shape Width：↓ → 宽度 −1  
3. Rotation / Opacity / Radius：无步进按钮  
4. 不可编辑：步进禁用  
5. 单选 →：右移 1  
6. 多选 →：同移；一次 Undo 全还原  
7. 锁定层仍移动  
8. 数字框聚焦时方向键不挪层  

- [x] **Step 3: Update this plan** — Step1/3 已勾；Step2 人机通过后勾选并将 Status → ✅ Done；spec → `已实现（已验收）`。

- [x] **Step 4: Commit**（待验收）

---

## Spec Coverage Self-Review

| Spec | Task |
|---|---|
| `showsStepButtons` + ↑↓ 布局 | Task 1 |
| Transform Anchor/Position 开启 | Task 2 |
| Shape Width/Height 开启 | Task 2 |
| 其它行默认关 | Task 2（不改） |
| NumberPropertyRow 不拦方向键 | Task 1（无键盘逻辑） |
| 方向键合成空间 ±1 Position | Task 3 + 4 |
| 多选同位移 + merge undo | Task 4 |
| 锁定仍移动 | Task 3（不过滤） |
| 写存储 position + mapCompositionDelta | Task 3 |

无 TBD / placeholder。
