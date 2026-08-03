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

- [ ] **Step 1: Wire `showsStepButtons: true`**

对以下调用在 `isEditable:`（或 `positionEditable`）参数后、trailing closure 前增加 `showsStepButtons: true`：

- `TransformInspector`：`anchorX`、`anchorY`、`positionX`、`positionY`
- `ShapeSizeInspector`：`width`、`height`

示例：

```swift
NumberPropertyRow(label: TransformField.positionX.label,
                  value: Float(position.dx),
                  hasKeyframeAtPlayhead: hasKeyframe(.position),
                  isEditable: positionEditable,
                  showsStepButtons: true)
{ newValue in
    setLayoutPosition(CGVector(dx: CGFloat(newValue), dy: position.dy))
} onToggleKeyframe: { _ in
    toggleVec2Keyframe(.position)
}
```

**不要**给 Scale / Rotation / Opacity / Radius 加。

- [ ] **Step 2: Build**。Expected: SUCCEEDED。

- [ ] **Step 3: Update this plan** — Task 2 Done。

- [ ] **Step 4: Commit**

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/ShapeSizeInspector.swift \
  docs/superpowers/plans/2026-08-03-number-step-buttons.md \
  -m "Enable step buttons on transform and shape size fields."
```

---

### Task 3: `MotionDocumentCore.nudgeLayersPosition`

**Status:** ⏳

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（紧邻 `alignLayers`）

**Interfaces:**
- Consumes: `mapCompositionDelta`、`evaluateVec2` / `setStaticVec2` / `addKeyframeVec2`、`TransformProperty.position`
- Produces:

```swift
/// Translates selected layers by a composition-space delta. Caller owns merge group.
func nudgeLayersPosition(compositionID: UInt64,
                         layerIDs: [UInt64],
                         delta: CGVector,
                         frame: Int64)
```

- [ ] **Step 1: Implement**

```swift
func nudgeLayersPosition(compositionID: UInt64,
                         layerIDs: [UInt64],
                         delta: CGVector,
                         frame: Int64)
{
    guard !layerIDs.isEmpty else { return }
    if abs(delta.dx) < 1e-6, abs(delta.dy) < 1e-6 { return }
    let path = TransformProperty.position.path
    for layerID in layerIDs {
        guard let deltaParent = mapCompositionDelta(compositionID: compositionID,
                                                    layerID: layerID,
                                                    frame: frame,
                                                    delta: delta)
        else {
            continue
        }
        let stored = evaluateVec2(entityID: layerID, path: path, frame: frame)
        let next = CGVector(dx: stored.dx + deltaParent.dx,
                            dy: stored.dy + deltaParent.dy)
        if keyframes(entityID: layerID, path: path).contains(where: { $0.frame == frame }) {
            addKeyframeVec2(entityID: layerID, path: path, frame: frame, value: next)
        } else {
            setStaticVec2(entityID: layerID, path: path, value: next)
        }
    }
}
```

注意：写的是**存储** AE position（与 Align / FreeTransform 一致），不是 layout-position UI 值。

- [ ] **Step 2: Build**。Expected: SUCCEEDED。

- [ ] **Step 3: Update this plan** — Task 3 Done。

- [ ] **Step 4: Commit**

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  docs/superpowers/plans/2026-08-03-number-step-buttons.md \
  -m "Add nudgeLayersPosition for composition-space keyboard nudges."
```

---

### Task 4: 方向键 → merge nudge

**Status:** ⏳

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift`（`keyCommands` / `canPerformAction`）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift`

**Interfaces:**
- Consumes: `nudgeLayersPosition`、`selectedLayerIDs`、`playheadClock.frame`、`beginMergeGroup`/`endMergeGroup`、`perform`
- Produces: 四向 `UIKeyCommand` + `nudgeSelection(dx:dy:)`

- [ ] **Step 1: Add key commands**

在 `keyCommands` 数组追加：

```swift
UIKeyCommand(input: UIKeyCommand.inputLeftArrow, modifierFlags: [], action: #selector(nudgeSelectionLeft)),
UIKeyCommand(input: UIKeyCommand.inputRightArrow, modifierFlags: [], action: #selector(nudgeSelectionRight)),
UIKeyCommand(input: UIKeyCommand.inputUpArrow, modifierFlags: [], action: #selector(nudgeSelectionUp)),
UIKeyCommand(input: UIKeyCommand.inputDownArrow, modifierFlags: [], action: #selector(nudgeSelectionDown)),
```

在 `canPerformAction` 中，对四个 nudge selector：仅当 `!editorState.selectedLayerIDs.isEmpty` 时返回 `true`（若该 switch 需要显式列出；否则依赖默认 + 空选 no-op 亦可，但推荐显式）。

- [ ] **Step 2: Implement actions**

```swift
@objc func nudgeSelectionLeft() { nudgeSelection(dx: -1, dy: 0) }
@objc func nudgeSelectionRight() { nudgeSelection(dx: 1, dy: 0) }
@objc func nudgeSelectionUp() { nudgeSelection(dx: 0, dy: -1) }
@objc func nudgeSelectionDown() { nudgeSelection(dx: 0, dy: 1) }

func nudgeSelection(dx: CGFloat, dy: CGFloat) {
    let layerIDs = editorState.selectedLayerIDs
    guard !layerIDs.isEmpty else { return }
    let compositionID = document.core.firstCompositionID
    let frame = playheadClock.frame
    perform("Nudge Position") {
        document.core.beginMergeGroup()
        document.core.nudgeLayersPosition(compositionID: compositionID,
                                          layerIDs: layerIDs,
                                          delta: CGVector(dx: dx, dy: dy),
                                          frame: frame)
        document.core.endMergeGroup()
    }
}
```

文本框聚焦时系统通常不把箭头交给这些 `UIKeyCommand`——无需额外 Focus 检测；人机验收第 8 条确认。

- [ ] **Step 3: Build**。Expected: SUCCEEDED。

- [ ] **Step 4: Update this plan** — Task 4 Done。

- [ ] **Step 5: Commit**

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift \
  docs/superpowers/plans/2026-08-03-number-step-buttons.md \
  -m "Nudge selected layer positions with arrow keys."
```

---

### Task 5: Spec 状态 + 验收

**Status:** ⏳

**Files:**
- Modify: `docs/superpowers/specs/2026-08-03-number-step-buttons-design.md`
- Modify: `docs/superpowers/plans/2026-08-03-number-step-buttons.md`

- [ ] **Step 1: Spec 状态改为** `已实现（待人机验收）`

- [ ] **Step 2: Manual checklist**

1. Transform Position：↑ 按钮 → 轴 +1  
2. Shape Width：↓ → 宽度 −1  
3. Rotation / Opacity / Radius：无步进按钮  
4. 不可编辑：步进禁用  
5. 单选 →：右移 1  
6. 多选 →：同移；一次 Undo 全还原  
7. 锁定层仍移动  
8. 数字框聚焦时方向键不挪层  

- [ ] **Step 3: Update this plan** — Step1/3 `[x]`；Step2 人机通过后勾选并将 Status → ✅ Done；spec → `已实现（已验收）`。

- [ ] **Step 4: Commit**（实现完成时先提交「待验收」；验收后再提交「已验收」）

```bash
git commit --only \
  docs/superpowers/specs/2026-08-03-number-step-buttons-design.md \
  docs/superpowers/plans/2026-08-03-number-step-buttons.md \
  -m "Mark number step buttons and nudge implemented pending acceptance."
```

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
