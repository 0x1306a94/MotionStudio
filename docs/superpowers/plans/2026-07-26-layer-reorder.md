# Layer 拖拽调整绘制顺序 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在时间轴左侧 Layer 列支持多选拖拽重排（实时反馈）与排列菜单（右键 + Mac Catalyst 菜单栏），从而调整绘制顺序。

**Architecture:** Core/bridge 已有 `ms_command_move_layer`；Swift 增加纯函数计算目标序与 move 步骤，经 `beginDrag`/`endDrag` 合并为一次 undo；UI 在 `LayerColumn` 做整行拖拽与插入线，`EditorViewController` + `AppDelegate.buildMenu` 提供 Arrange 命令。

**Tech Stack:** SwiftUI + UIKit (Mac Catalyst)、`MotionDocumentCore` C bridge、Swift Testing（`MotionStudioAppTests`）

**Spec:** [`docs/superpowers/specs/2026-07-26-layer-reorder-design.md`](../specs/2026-07-26-layer-reorder-design.md)

## Global Constraints

- 模型序：`layers[0]` 最底，末项最上；UI 列表 `.reversed()`，顶 = 最前
- 锁定层仍可重排；属性子行不可拖
- 拖拽实时改模型；一次拖拽 / 一次菜单 = 一个 undo
- 不改 Core/bridge API（已有 `ms_command_move_layer`）
- 新 Swift 文件放在同步 Root Group 下即可，无需改 pbxproj
- 本仓库用户规则：未经用户明确要求不要 `git commit`；计划中的 Commit 步骤改为「暂存说明」，执行时跳过 commit 除非用户要求

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `Timeline/Root/TimelineReorder.swift`（新建） | 纯函数：目标序、move 步骤、UI↔model 槽换算、排列动作目标序 |
| `MotionStudioAppTests/TimelineReorderTests.swift`（新建） | 上述纯函数单测 |
| `Model/MotionDocumentCore.swift` | 封装 `moveLayer`；可选 `applyLayerOrder` |
| `Timeline/Sidebar/TimelineSidebarView.swift` | 侧栏图层拖放重排、右键菜单、排列 |
| `Editor/EditorViewController+Commands.swift` | `@objc` 排列动作 + 共用 `arrangeSelection` |
| `Editor/EditorViewController.swift` | `canPerformAction` / `keyCommands` |
| `App/AppDelegate.swift` | Catalyst Arrange 子菜单 |

---

### Task 1: TimelineReorder 纯函数 + 单测（TDD）

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineReorder.swift`
- Create: `apps/MotionStudioApp/MotionStudioAppTests/TimelineReorderTests.swift`

**Interfaces:**
- Produces:
  - `enum LayerArrangeAction: Sendable { case bringToFront, bringForward, sendBackward, sendToBack }`
  - `func reorderedLayerIDs(current: [UInt64], moving: Set<UInt64>, insertBeforeModelIndex: Int) -> [UInt64]`
  - `func moveSteps(from: [UInt64], to: [UInt64]) -> [(from: Int, to: Int)]`
  - `func modelInsertBeforeIndex(uiSlot: Int, layerCount: Int) -> Int`
  - `func arrangedLayerIDs(current: [UInt64], moving: Set<UInt64>, action: LayerArrangeAction) -> [UInt64]?`（无变化返回 `nil`）

- [x] **Step 1: 写失败测试**

```swift
// TimelineReorderTests.swift
import Foundation
@testable import MotionStudio
import Testing

struct TimelineReorderTests {
    @Test
    func reorderedLayerIDs_singleLayerToFront() {
        // model: 底→顶 A B C
        let current: [UInt64] = [1, 2, 3]
        // 把 A(1) 置顶 → insertBeforeModelIndex = count = 3（相对 remaining）
        let result = reorderedLayerIDs(current: current, moving: [1], insertBeforeModelIndex: 3)
        #expect(result == [2, 3, 1])
    }

    @Test
    func reorderedLayerIDs_nonContiguousBecomesBlock() {
        let current: [UInt64] = [1, 2, 3, 4, 5]
        // 移动 1 与 3。insertBeforeModelIndex=4 → prefix 中非 moving 个数=2
        // remaining [2,4,5] 在 index 2 插入 → [2,4,1,3,5]
        let result = reorderedLayerIDs(current: current, moving: [1, 3], insertBeforeModelIndex: 4)
        #expect(result == [2, 4, 1, 3, 5])
    }

    @Test
    func modelInsertBeforeIndex_mapsUISlots() {
        #expect(modelInsertBeforeIndex(uiSlot: 0, layerCount: 4) == 4) // UI 顶之上 → 模型末
        #expect(modelInsertBeforeIndex(uiSlot: 4, layerCount: 4) == 0) // UI 底之下 → 模型头
        #expect(modelInsertBeforeIndex(uiSlot: 1, layerCount: 4) == 3)
    }

    @Test
    func moveSteps_reachesTarget() {
        let from: [UInt64] = [1, 2, 3]
        let to: [UInt64] = [2, 3, 1]
        var order = from
        for step in moveSteps(from: from, to: to) {
            let layer = order.remove(at: step.from)
            order.insert(layer, at: step.to)
        }
        #expect(order == to)
    }

    @Test
    func moveSteps_noopWhenEqual() {
        #expect(moveSteps(from: [1, 2], to: [1, 2]).isEmpty)
    }

    @Test
    func arrangedLayerIDs_bringForwardAndBack() {
        let current: [UInt64] = [1, 2, 3, 4]
        // 选中 2：当前 collapse insertAt=1；forward → insertAt=2 → [1,3,2,4]
        // remaining [1,3,4]，层 2 当前 insertAt=1
        #expect(arrangedLayerIDs(current: current, moving: [2], action: .bringForward) == [1, 3, 2, 4])
        #expect(arrangedLayerIDs(current: current, moving: [2], action: .sendBackward) == [2, 1, 3, 4])
        #expect(arrangedLayerIDs(current: current, moving: [2], action: .bringToFront) == [1, 3, 4, 2])
        #expect(arrangedLayerIDs(current: current, moving: [2], action: .sendToBack) == [2, 1, 3, 4])
        #expect(arrangedLayerIDs(current: current, moving: [4], action: .bringToFront) == nil)
        #expect(arrangedLayerIDs(current: current, moving: [1], action: .sendToBack) == nil)
    }
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
xcodebuild test -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -destination 'platform=macOS,variant=Mac Catalyst' \
  -only-testing:MotionStudioAppTests/TimelineReorderTests 2>&1 | tail -40
```

Expected: 编译失败或测试失败（符号未定义）。

若 Xcode MCP 可用，优先用 MCP 跑测试。

- [ ] **Step 3: 实现 `TimelineReorder.swift`**

```swift
import Foundation

enum LayerArrangeAction: Sendable {
    case bringToFront
    case bringForward
    case sendBackward
    case sendToBack
}

/// UI 插入槽 → 模型 insertBefore（相对「抽出 moving 后的 remaining」索引语义见下）。
/// `uiSlot`：0 = 最前层之上，`layerCount` = 最底层之下。
func modelInsertBeforeIndex(uiSlot: Int, layerCount: Int) -> Int {
    let clamped = min(max(0, uiSlot), layerCount)
    return layerCount - clamped
}

/// 按模型序抽出 `moving`，插入 remaining，使块落在 `insertBeforeModelIndex`
///（相对 **start/current 全量数组** 的「插入点」：先算 remaining 中应对齐的位置）。
///
/// 换算：`insertAt = current.prefix(insertBeforeModelIndex).filter { !moving.contains($0) }.count`
/// 再钳制到 `0...remaining.count`。
func reorderedLayerIDs(current: [UInt64], moving: Set<UInt64>, insertBeforeModelIndex: Int) -> [UInt64] {
    guard !moving.isEmpty else { return current }
    let movingOrdered = current.filter { moving.contains($0) }
    guard !movingOrdered.isEmpty else { return current }
    let remaining = current.filter { !moving.contains($0) }
    let clampedBefore = min(max(0, insertBeforeModelIndex), current.count)
    let insertAt = current.prefix(clampedBefore).filter { !moving.contains($0) }.count
    var result = remaining
    result.insert(contentsOf: movingOrdered, at: min(insertAt, remaining.count))
    return result
}

func moveSteps(from: [UInt64], to: [UInt64]) -> [(from: Int, to: Int)] {
    guard from.count == to.count, Set(from) == Set(to) else { return [] }
    var order = from
    var steps: [(from: Int, to: Int)] = []
    for targetIndex in to.indices {
        let wanted = to[targetIndex]
        guard let sourceIndex = order.firstIndex(of: wanted) else { return [] }
        if sourceIndex == targetIndex { continue }
        let layer = order.remove(at: sourceIndex)
        order.insert(layer, at: targetIndex)
        steps.append((sourceIndex, targetIndex))
    }
    return steps
}

func arrangedLayerIDs(current: [UInt64], moving: Set<UInt64>,
                      action: LayerArrangeAction) -> [UInt64]?
{
    guard !moving.isEmpty else { return nil }
    let movingOrdered = current.filter { moving.contains($0) }
    guard !movingOrdered.isEmpty else { return nil }
    let remaining = current.filter { !moving.contains($0) }
    guard let firstMovingIndex = current.firstIndex(where: { moving.contains($0) }) else {
        return nil
    }
    let currentInsertAt = current.prefix(firstMovingIndex).filter { !moving.contains($0) }.count
    let targetInsertAt: Int
    switch action {
    case .bringToFront:
        targetInsertAt = remaining.count
    case .sendToBack:
        targetInsertAt = 0
    case .bringForward:
        targetInsertAt = min(currentInsertAt + 1, remaining.count)
    case .sendBackward:
        targetInsertAt = max(currentInsertAt - 1, 0)
    }
    var desired = remaining
    desired.insert(contentsOf: movingOrdered, at: targetInsertAt)
    return desired == current ? nil : desired
}
```

- [ ] **Step 4: 跑测试至通过**

同 Step 2 命令。Expected: TimelineReorderTests 全绿。

- [ ] **Step 5: 暂存说明（勿自动 commit）**

记录改动文件：`TimelineReorder.swift`、`TimelineReorderTests.swift`。

---

### Task 2: MotionDocumentCore.moveLayer + applyLayerOrder

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（在 `removeLayer` 附近）

**Interfaces:**
- Consumes: `ms_command_move_layer`；`moveSteps`
- Produces:
  - `func moveLayer(compositionID: UInt64, fromIndex: Int, toIndex: Int)`
  - `func applyLayerOrder(compositionID: UInt64, desired: [UInt64])` — 内部对当前 `layerIDs` 算 steps 并调用 `moveLayer`；若已相等则无操作

- [ ] **Step 1: 在 `removeLayer` 旁加入**

```swift
func moveLayer(compositionID: UInt64, fromIndex: Int, toIndex: Int) {
    ms_command_move_layer(handle, compositionID, Int32(fromIndex), Int32(toIndex))
    changed()
}

/// Applies an absolute model-order (bottom → top). No-op when already equal.
func applyLayerOrder(compositionID: UInt64, desired: [UInt64]) {
    let current = layerIDs(compositionID: compositionID)
    guard current != desired else { return }
    for step in moveSteps(from: current, to: desired) {
        ms_command_move_layer(handle, compositionID, Int32(step.from), Int32(step.to))
    }
    changed()
}
```

注意：循环内不要每次 `changed()`（避免多余刷新）；循环用 bridge 直接调，最后一次 `changed()`。上面实现已这样做。

- [ ] **Step 2: 编译 App（Catalyst）确认通过**

优先 Xcode MCP `BuildProject`；否则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -configuration Debug \
  -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64" \
  build 2>&1 | tail -30
```

Expected: BUILD SUCCEEDED。

- [ ] **Step 3: 暂存说明**

---

### Task 3: Editor 排列命令（共用逻辑）

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift`（`canPerformAction`、`keyCommands`）

**Interfaces:**
- Consumes: `arrangedLayerIDs`、`core.applyLayerOrder`、`editorState.selectedLayerIDs`
- Produces: `@objc bringLayersToFront` / `bringLayersForward` / `sendLayersBackward` / `sendLayersToBack`；`func canArrangeSelection(_ action: LayerArrangeAction) -> Bool`；`func arrangeSelection(_ action: LayerArrangeAction, actionName: String)`

- [ ] **Step 1: 在 Commands extension 增加**

```swift
func canArrangeSelection(_ action: LayerArrangeAction) -> Bool {
    let compositionID = document.core.firstCompositionID
    let current = document.core.layerIDs(compositionID: compositionID)
    let moving = Set(editorState.selectedLayerIDs)
    return arrangedLayerIDs(current: current, moving: moving, action: action) != nil
}

func arrangeSelection(_ action: LayerArrangeAction, actionName: String) {
    let compositionID = document.core.firstCompositionID
    let current = document.core.layerIDs(compositionID: compositionID)
    let moving = Set(editorState.selectedLayerIDs)
    guard let desired = arrangedLayerIDs(current: current, moving: moving, action: action) else {
        return
    }
    perform(actionName) {
        document.core.applyLayerOrder(compositionID: compositionID, desired: desired)
    }
}

@objc func bringLayersToFront() {
    arrangeSelection(.bringToFront, actionName: "Bring to Front")
}

@objc func bringLayersForward() {
    arrangeSelection(.bringForward, actionName: "Bring Forward")
}

@objc func sendLayersBackward() {
    arrangeSelection(.sendBackward, actionName: "Send Backward")
}

@objc func sendLayersToBack() {
    arrangeSelection(.sendToBack, actionName: "Send to Back")
}
```

- [ ] **Step 2: 更新 `canPerformAction` 与 `keyCommands`**

在 `EditorViewController.swift`：

`keyCommands` 数组追加：

```swift
UIKeyCommand(input: "]", modifierFlags: [.command], action: #selector(bringLayersForward)),
UIKeyCommand(input: "[", modifierFlags: [.command], action: #selector(sendLayersBackward)),
UIKeyCommand(input: "]", modifierFlags: [.command, .alternate], action: #selector(bringLayersToFront)),
UIKeyCommand(input: "[", modifierFlags: [.command, .alternate], action: #selector(sendLayersToBack)),
```

`canPerformAction` 增加 case：

```swift
case #selector(bringLayersToFront):
    canArrangeSelection(.bringToFront)
case #selector(bringLayersForward):
    canArrangeSelection(.bringForward)
case #selector(sendLayersBackward):
    canArrangeSelection(.sendBackward)
case #selector(sendLayersToBack):
    canArrangeSelection(.sendToBack)
```

- [ ] **Step 3: 编译通过**

- [ ] **Step 4: 暂存说明**

---

### Task 4: AppDelegate Arrange 菜单栏

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/App/AppDelegate.swift`

**Interfaces:**
- Consumes: Task 3 的四个 `@objc` selectors

- [ ] **Step 1: 在 `buildMenu` 的 Close 菜单逻辑之后插入**

```swift
let bringToFront = UIKeyCommand(title: "Bring to Front",
                                image: nil,
                                action: #selector(EditorViewController.bringLayersToFront),
                                input: "]",
                                modifierFlags: [.command, .alternate])
let bringForward = UIKeyCommand(title: "Bring Forward",
                                image: nil,
                                action: #selector(EditorViewController.bringLayersForward),
                                input: "]",
                                modifierFlags: .command)
let sendBackward = UIKeyCommand(title: "Send Backward",
                                image: nil,
                                action: #selector(EditorViewController.sendLayersBackward),
                                input: "[",
                                modifierFlags: .command)
let sendToBack = UIKeyCommand(title: "Send to Back",
                              image: nil,
                              action: #selector(EditorViewController.sendLayersToBack),
                              input: "[",
                              modifierFlags: [.command, .alternate])
let arrangeMenu = UIMenu(title: "Arrange",
                         children: [bringToFront, bringForward, sendBackward, sendToBack])
builder.insertSibling(arrangeMenu, afterMenu: .edit)
```

若 `insertSibling(..., afterMenu: .edit)` 在目标 SDK 不可用，改用 `builder.insertChild(arrangeMenu, atEndOfMenu: .edit)`。

- [ ] **Step 2: Catalyst 运行，确认菜单栏出现 Arrange，无选中时灰显，有选中时可执行**

- [ ] **Step 3: 暂存说明**

---

### Task 5: LayerColumn 拖拽重排 + 插入线 + 右键菜单

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Sidebar/TimelineSidebarView.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Sidebar/TimelineSidebarView.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineView.swift` / `TimelineContentView.swift`（如需向下传 `registerEdit`）

**Interfaces:**
- Consumes: `reorderedLayerIDs`、`modelInsertBeforeIndex`、`applyLayerOrder`、`beginDrag`/`endDrag`、`registerEdit`、`arrangeSelection` 等价回调
- Produces: 可拖拽 Layer 列；右键调用排列

#### 数据流

`TimelineContentView` 已有 `registerEdit`。将其传入 `LayerColumn`：

```swift
LayerColumn(rows: rows,
            perform: perform,
            registerEdit: registerEdit,
            clearSelection: clearSelection)
```

`LayerColumn` 使用 `@Environment(MotionDocumentCore.self)` 与 `@Environment(EditorState.self)`。

#### 拖拽状态

```swift
@State private var drag: LayerReorderDragState?

struct LayerReorderDragState {
    var movingIDs: Set<UInt64>
    var startOrder: [UInt64]
    var lastDesired: [UInt64]
    var insertionUISlot: Int?
    var isActive: Bool // 超过阈值后为 true
}
```

#### 块几何

从 `rows` 计算每个 layer 块的 Y 区间（layer 行 + 随后连续的同 `layerID` 属性行）：

```swift
struct LayerBlockFrame {
    let layerID: UInt64
    let minY: CGFloat
    let maxY: CGFloat
}

func layerBlockFrames(rows: [TimelineRow]) -> [LayerBlockFrame] {
    // 按 UI 序（rows 已是 reversed 后的层序）累积 height
}
```

`uiSlot(forY:frames:)`：落在块中线之上偏向该块前的槽，之下偏向后槽；夹在 `0...frames.count`。

#### 手势（挂在 LayerRow 整行）

阈值 4pt。逻辑：

```text
onChanged(layerID, translation, locationInColumn):
  if drag == nil:
    // 首次：若 layerID 已选则 moving=选中，否则 selectLayer 单选并 moving={id}
    beginDrag(); startOrder = core.layerIDs(...)
  if !drag.isActive && hypot(translation) < 4: return
  drag.isActive = true
  slot = uiSlot(y)
  insertBefore = modelInsertBeforeIndex(uiSlot: slot, layerCount: startOrder.count)
  desired = reorderedLayerIDs(current: startOrder, moving: movingIDs, insertBeforeModelIndex: insertBefore)
  if desired != drag.lastDesired:
    core.applyLayerOrder(...); lastDesired = desired
  insertionUISlot = slot

onEnded:
  if drag?.isActive == true:
    endDrag(); registerEdit(moving.count > 1 ? "Move Layers" : "Move Layer")
  else:
    // 未激活：当作 tap — LayerRow 已有 onTapGesture，需避免冲突：
    // 推荐：用 highPriorityGesture / 或拖拽用 DragGesture(minimumDistance: 4)
  clear drag; endDrag if begin 过但未 active（若已 beginDrag 必须 endDrag）
```

**手势冲突处理（必须）：**

- `DragGesture(minimumDistance: 4)` 挂在 `LayerRow` 的行背景上，eye/lock 使用 `Button`（默认抢手势）。
- 若 `began` 时调用了 `beginDrag` 但从未 active，在 `onEnded`/`onCancel` 必须 `endDrag()`，且不要 `registerEdit`。
- 更稳妥：仅在 `isActive` 首次为 true 时才 `beginDrag()`。

采用后者：

```text
首次超过阈值:
  beginDrag()
  startOrder = layerIDs()  // 此刻快照
  ...
onEnded:
  if began: endDrag(); registerEdit(...)
```

#### 插入线

`LayerColumn` 的 `ZStack` 按 `insertionUISlot` 与块 frames 画 2pt 高的 `accentColor` 横线。

#### 右键菜单（LayerRow）

```swift
.contextMenu {
    Button("Bring to Front") { arrange(.bringToFront) }
    Button("Bring Forward") { arrange(.bringForward) }
    Button("Send Backward") { arrange(.sendBackward) }
    Button("Send to Back") { arrange(.sendToBack) }
}
```

`arrange` 闭包由 `LayerColumn` 传入，实现与 Editor 相同：

```swift
func arrange(_ action: LayerArrangeAction) {
    let name: String = ...
    let compositionID = core.firstCompositionID
    let current = core.layerIDs(compositionID: compositionID)
    let moving = Set(editorState.selectedLayerIDs)
    // 若当前行未选中，先 selectLayer(layerID)
    guard let desired = arrangedLayerIDs(...) else { return }
    perform(name) { core.applyLayerOrder(compositionID: compositionID, desired: desired) }
}
```

右键打开前：若该行未选中，先单选该行（与常见宿主一致）。

- [ ] **Step 1: 接线 `registerEdit` 到 LayerColumn**
- [ ] **Step 2: 实现块 frames、插入线、拖拽状态机**
- [ ] **Step 3: LayerRow contextMenu + drag gesture**
- [ ] **Step 4: Catalyst 手动验收清单**

  1. 单层拖到顶部 → 画布该层到最前；Undo 一次恢复  
  2. Shift 多选非连续两层拖到中间 → 收成连续块且相对序不变  
  3. 锁定层可拖  
  4. 拖动中画布实时更新  
  5. 右键四命令可用  
  6. 菜单栏 Arrange + 快捷键可用；无选中灰显  
  7. eye/lock 点击不触发重排；轻点仍选中  

- [ ] **Step 5: 暂存说明**

---

### Task 6: 回归测试与收尾

**Files:** 无新文件（除非修 bug）

- [ ] **Step 1: 再跑 TimelineReorderTests**
- [ ] **Step 2: 全量 App 编译（Catalyst）**
- [ ] **Step 3: 对照 spec 需求 1–6 勾验收**
- [ ] **Step 4: 若用户要求再 commit**（建议信息）：

```text
feat: 时间轴 Layer 拖拽与 Arrange 菜单调整绘制顺序

复用 MoveLayerCommand，支持多选实时重排、右键与 Catalyst 菜单栏。
```

---

## Spec 覆盖自检

| Spec 需求 | Task |
|---|---|
| 多选拖拽成块 | Task 1 `reorderedLayerIDs` + Task 5 |
| 整行拖、按钮不拖、子行不拖 | Task 5 |
| 锁定可拖 | Task 5（无 lock 判断） |
| 实时反馈 | Task 5 `applyLayerOrder` on changed |
| 右键菜单 | Task 5 |
| Catalyst 菜单栏 + 快捷键 | Task 3–4 |
| 一次 undo | Task 5 `beginDrag`/`endDrag`；菜单走 `perform` |
| 单测 | Task 1 |
| `MotionDocumentCore.moveLayer` | Task 2 |

无占位符；类型名在各 Task Interfaces 一致。
