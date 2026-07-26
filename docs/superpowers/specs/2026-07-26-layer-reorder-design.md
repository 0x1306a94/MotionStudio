# 时间轴左侧 Layer 拖拽调整绘制顺序 — 设计说明

日期：2026-07-26  
状态：已确认，可进入实现计划

## 目标

在编辑器底部时间轴左侧 Layer 列中，通过拖拽调整图层顺序，从而改变绘制顺序。支持多选拖拽、拖动中实时画布反馈、Layer 行右键菜单，以及 Mac Catalyst 菜单栏 Arrange 命令。

## 现状

| 层级 | 能力 |
|---|---|
| `Document::moveLayer(compositionId, fromIndex, toIndex)` | 已有 |
| `MoveLayerCommand`（连续单层移动可 `mergeWith`） | 已有 |
| Bridge `ms_command_move_layer` | 已有 |
| `MotionDocumentCore.moveLayer` | 缺失 |
| 时间轴 Layer 列拖拽 / 排列菜单 | 缺失 |

绘制顺序：composition 的 `layers[0]` 最先绘制（最底），末项最上。

时间轴 UI 已对 layer 列表 `.reversed()`，即 **列表顶部 = 画面最前**（After Effects 习惯）。重排时必须在 UI 序与模型序之间正确换算。

## 需求

1. **多选拖拽**：拖任一已选中的 layer 行，整组作为一块移动；组内相对顺序不变；非连续选中在落点处收成连续块。
2. **整行拖拽**：无独立 drag handle。显示/锁定按钮不启动拖拽。属性子行不可拖；父层移动后随 `buildTimelineRows` 重建而跟随。
3. **锁定层**：锁定只禁止属性/动画编辑，仍可拖动改序。
4. **实时反馈**：拖动中插入位置变化时即写入模型，画布与时间轴经 `revision` 立即刷新。
5. **菜单（两处）**：
   - Layer 行 **右键菜单**：置于顶层 / 上移一层 / 下移一层 / 置于底层（iPad + Catalyst）。
   - Mac Catalyst **菜单栏**：同一组动作放在 Arrange 子菜单（经 `AppDelegate.buildMenu` 插入，对齐现有 Save/Close 写法）。
6. **撤销**：一次拖拽手势 = 一个 undo 单元；一次菜单动作 = 一个 undo 单元。

### 非目标

- 图层父子嵌套 / parenting
- 跨 composition 移动
- 拖到右侧轨道图区域改序
- 在 iPadOS 上额外做系统菜单栏（超出 Catalyst 已有 `UIMenuBuilder` 用法）

## 方案

不改 Core 绘制语义。复用 `MoveLayerCommand` + `beginMergeGroup` / `endMergeGroup`（Swift：`beginDrag` / `endDrag`）。用纯 Swift 辅助函数计算多层目标顺序与 `moveLayer` 步骤；在时间轴侧栏接入手势与菜单。

否决的备选：

- 把 `LayerColumn` 改成 `List` + `.onMove`：与左右列自定义 VStack 行高对齐冲突。
- 仅做菜单排列：弱于已要求的拖拽体验。

## 架构

```text
LayerColumn / LayerRow
  ├─ DragGesture（阈值约 4pt）
  │    began   → beginDrag()；记录 startOrder + movingIDs
  │    changed → insertSlot → desiredOrder → applyReorder（实时）
  │    ended   → endDrag()；registerEdit("Move Layer(s)")
  └─ contextMenu → Editor 排列动作

AppDelegate.buildMenu（Catalyst 主菜单）
  └─ Arrange 子菜单 → #selector(EditorViewController.bring…)
        └─ canPerformAction：有选中且该动作会改变顺序时才启用

共用：applyReorder / TimelineReorder 辅助函数
MotionDocumentCore.moveLayer → ms_command_move_layer
```

### 关键 API

```swift
// MotionDocumentCore
func moveLayer(compositionID: UInt64, fromIndex: Int, toIndex: Int)

// TimelineReorder（纯函数，可单测）
func reorderedLayerIDs(
    current: [UInt64],              // 模型序：底 → 顶
    moving: Set<UInt64>,
    insertBeforeModelIndex: Int     // == count 表示置顶
) -> [UInt64]

func moveSteps(from: [UInt64], to: [UInt64]) -> [(from: Int, to: Int)]
```

### 重排算法

1. 拖拽开始时快照模型序：`startOrder`。
2. 若按下的层已在选中集合中，则 `movingIDs` = 当前选中；否则 `movingIDs = {pressedID}` 并单选该层。
3. 将指针 Y 映射到层 **块**之间的 UI 插入槽（一层行 + 其属性子行 = 一块）。UI 槽为 `0...count`：`0` 在最前层之上，`count` 在最底层之下。
4. UI 槽 → 模型 `insertBeforeModelIndex`：`count - uiSlot`（UI 槽 0 → 插到模型末尾/最前；UI 槽 `count` → 插到模型 index 0/最后）。
5. `desired = reorderedLayerIDs(startOrder, movingIDs, insertBeforeModelIndex)`。
6. `applyReorder(desired)`：
   - 计算 `moveSteps(currentModelOrder, desired)`；
   - 逐步调用 `core.moveLayer`；
   - 在已打开的 merge group 内，步骤合并为一个 undo 单元（`MoveLayerCommand.mergeWith` 和/或 `CompositeCommand` 吸收）。

菜单动作根据当前选中计算目标序，在 `perform(...)` 内调用同一套 `applyReorder`（不走拖拽 merge group）。

`reorderedLayerIDs`：按现有模型序抽出 moving 层，再插入到剩余数组中，使块首元素落在 `insertBeforeModelIndex`（钳制到 `0...remaining.count`）。

### 选中与手势细节

- 移动超过约 4pt 才进入重排，避免吞掉 tap 选中与 Shift 多选。
- 拖动中：层块之间显示插入线；移动中的层块可略降透明度。
- 右侧轨道无需单独同步：`revision` 变化后与左侧共用同一份 `buildTimelineRows` 结果。

### 排列动作（右键 + Catalyst 菜单栏）

| 动作 | 行为 | Catalyst 快捷键 |
|---|---|---|
| 置于顶层（Bring to Front） | 选中块移到模型末尾（UI 顶） | ⌥⌘] |
| 上移一层（Bring Forward） | 选中块向顶移动一档（越过一个未选层） | ⌘] |
| 下移一层（Send Backward） | 选中块向底移动一档 | ⌘[ |
| 置于底层（Send to Back） | 选中块移到模型开头（UI 底） | ⌥⌘[ |

选中为空，或该动作不会改变顺序时禁用。

#### Catalyst 菜单栏接线

对齐现有 Save/Close 模式：

1. `AppDelegate.buildMenu(with:)` 插入 **Arrange** `UIMenu`（相对 `.edit` 使用 `insertChild` / `insertSibling`，使 Mac Catalyst 菜单栏可见）。
2. 各项为 `UIKeyCommand` / `UICommand`，指向 `EditorViewController` 的 `@objc` 方法（`bringLayersToFront`、`bringLayersForward`、`sendLayersBackward`、`sendLayersToBack`）。
3. `EditorViewController.canPerformAction` 按当前选中判断是否可执行（菜单项正确灰显）。
4. 实现经 `perform("Bring to Front" | …) { … }` 调用共用重排逻辑；SwiftUI 右键菜单走同一 `arrangeSelection(_:)` 辅助，避免两套实现。

iPad 保留右键菜单，并可按需把相同快捷键挂到 `keyCommands`；菜单栏 builder 条目在 iPadOS 上无害。

## 测试

- 在 `MotionStudioAppTests` 为 `reorderedLayerIDs` / `moveSteps` 加单测：单层；连续/非连续多选；置顶/置底；目标等于当前时为空操作。
- Core 侧 `Document::moveLayer` 与 `MoveLayerCommand` 的 merge/undo 沿用现有测试。

## 预计改动文件

- `apps/.../Model/MotionDocumentCore.swift` — 封装 `ms_command_move_layer`
- `apps/.../Timeline/Sidebar/LayerColumn.swift` / `LayerRow.swift` — 拖拽、插入线、右键菜单
- `apps/.../App/AppDelegate.swift` — `buildMenu` 增加 Arrange 子菜单
- `apps/.../Editor/EditorViewController.swift`（及 Commands）— `@objc` 排列动作、`canPerformAction`、可选 `keyCommands`
- 时间轴下新增辅助（如 `TimelineReorder.swift`）
- 除非实现中发现缺口，否则不改 Core/bridge API

## 已决议

- 多选：是，AE 风格在落点收成连续块
- 拖拽：整行 layer 行
- 锁定：仍可改序
- 反馈：拖动中实时改模型
- 菜单：Layer 行右键 **以及** Mac Catalyst 菜单栏 Arrange（含快捷键）
