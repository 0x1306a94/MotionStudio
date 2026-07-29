# Timeline SwiftUI → UIKit 迁移实现计划

> **给执行代理：** 必需子技能：用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 按任务推进。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 用 UIKit `TimelineViewController` 替换编辑器底部 Timeline 的 `UIHostingController`；播放头经 `PlayheadClock` listener 更新；功能对齐；Inspector 仍用 SwiftUI / `@Observable`。

**架构：** `EditorViewController` 以 child VC 嵌入 `TimelineViewController`。复用纯逻辑（`TimelineSupport`、`TimelineReorder`、draft/segment）。播放头 UI 只走 listener（不进 SwiftUI environment）。`PlayheadClock` 暂留 `@Observable` 给 Inspector。

**技术栈：** UIKit；现有 `MotionDocumentCore` / `EditorState` / `PlayheadClock`；Mac Catalyst + iPad。

**设计文档：** [docs/superpowers/specs/2026-07-29-timeline-uikit-migration-design.md](../specs/2026-07-29-timeline-uikit-migration-design.md)

## 全局约束

- 范围：仅 Timeline；Inspector / ProjectPanel 仍 SwiftUI hosting。
- `PlayheadClock` 保留 `@Observable`（Inspector）；另加多播 listener 给 UIKit。
- 一次去掉 Timeline Hosting（可多分 commit）；功能对齐，不要求像素级。
- 长行优先；`if`/`switch` 体必须 `{}`（项目 coding-style）。
- 不主动 push；在 `feature/0x1306a94_timeline_uikit` 上按任务 commit。

## 目标文件地图

| 路径 | 职责 |
|---|---|
| `Model/PlayheadClock.swift` | 保留 `@Observable`；增加 `addListener` / `removeListener` |
| `Timeline/UIKit/TimelineViewController.swift` | 根 VC；revision reload；listener 生命周期 |
| `Timeline/UIKit/TimelineControlsView.swift` | 播放/暂停、帧号、缩放、预览背景 |
| `Timeline/UIKit/TimelineRulerView.swift` | 标尺绘制 |
| `Timeline/UIKit/TimelinePlayheadView.swift` | 播放头线 + 拖拽 scrub |
| `Timeline/UIKit/TimelineSidebarView.swift` | 图层/属性行 |
| `Timeline/UIKit/TimelineTracksView.swift` | 时间条 / 属性轨 / 关键帧轨 |
| `Timeline/UIKit/TimelineScrollCoordinator.swift` | 纵横向滚动与缩放协调 |
| `Timeline/Root/TimelineSupport.swift` | 保留；必要时去掉仅 SwiftUI 的 import |
| `Timeline/Root/TimelineReorder.swift` | 原样保留 |
| `Editor/EditorViewController+Layout.swift` | Hosting → child VC |
| `Editor/EditorSupportingViews.swift` | 无引用后删除 `UIKitTimelineHostView` |
| 对齐后删除 | `Timeline/` 下 SwiftUI `*View` 入口（保留可复用纯文件） |

---

### Task 1：PlayheadClock listeners

**文件：**
- 修改：`apps/MotionStudioApp/MotionStudioApp/Model/PlayheadClock.swift`
- 可选测试：有现成 app 单测则加；否则临时 listener 手测后删掉再提交

**接口：**
- 产出：
  - `func addListener(_ block: @escaping (Int64) -> Void) -> UUID`
  - `func removeListener(_ id: UUID)`
  - `publish` 仍先赋 `frame`（Observation），再通知 listeners

- [ ] **Step 1：实现 listener API**

保持 `@MainActor @Observable`。存储 `[UUID: (Int64) -> Void]`。`publish` 相等则 return，否则设 `frame` 再回调。注释写明：Timeline 必须用 listener；Inspector 可继续 Observation 读 `frame`。

- [ ] **Step 2：临时挂点或断点确认**

在 `EditorViewController.viewDidLoad` 临时 `addListener`，播放一次确认回调；提交前删掉临时代码。

- [ ] **Step 3：Commit**

```bash
git commit --only apps/MotionStudioApp/MotionStudioApp/Model/PlayheadClock.swift -m "Add PlayheadClock listeners for UIKit timeline playhead updates."
```

---

### Task 2：骨架 TimelineViewController + 换宿主

**文件：**
- 新建：`apps/MotionStudioApp/MotionStudioApp/Timeline/UIKit/TimelineViewController.swift`
- 修改：`EditorViewController.swift`（属性类型）
- 修改：`EditorViewController+Layout.swift`（`configureTimeline`）
- 若工程非文件夹同步，按现有方式把新文件加入 Xcode target

**接口：**
- 消费：与 `UIKitTimelineHostView` 相同注入（`document`、`editorState`、`playheadClock`、`perform`、`registerEdit`、`clearSelection`）
- 产出：填满 `timelinePanel`（把手下方）的 child VC

- [ ] **Step 1：加 `TimelineViewController` stub**

占位背景 + 文案。`viewDidAppear`/`deinit` 注册/移除 playhead listener（回调可先空）。revision 刷新方式对齐现有 UIKit（后续再接；stub 可先不做全量 reload）。

- [ ] **Step 2：在 `configureTimeline()` 替换 Hosting**

去掉 `UIHostingController<UIKitTimelineHostView>`。`addChild` + 约束钉在把手下。`TimelineGrabberView` 高度拖拽逻辑不变。

- [ ] **Step 3：Catalyst 编译运行**

预期：编辑器能开；底部是 stub；不崩；Inspector/Project 仍可用。

- [ ] **Step 4：Commit**

```bash
git commit --only <touched files> -m "Embed UIKit TimelineViewController stub in place of SwiftUI hosting."
```

---

### Task 3：控件 + 标尺 + 播放头（播放主路径）

**文件：**
- 新建：`Timeline/UIKit/TimelineControlsView.swift`
- 新建：`Timeline/UIKit/TimelineRulerView.swift`
- 新建：`Timeline/UIKit/TimelinePlayheadView.swift`
- 新建：`Timeline/UIKit/TimelineScrollCoordinator.swift`（`scrollX` / `pointsPerFrame` 与 `EditorState` 对齐）
- 修改：`TimelineViewController.swift`
- 复用：`TimelineSupport.swift`（`timelineX`、`timelineFrame`、常量）

**接口：**
- Listener **只**更新播放头位置 + 控件帧号
- Scrub 写 `playheadClock.publish`，并与现 SwiftUI scrub 一样更新编辑器状态
- 播放/暂停接现有 `TimelineControls` / `togglePlayback` 同源状态

- [ ] **Step 1：移植控件（播放/暂停、帧号、缩放、预览背景）**

动作接到现有 editor/document API。

- [ ] **Step 2：移植标尺绘制 + 播放头**

`UIView.draw` 或 `CALayer`。横向状态用 `editorState.timelineScrollX` / `timelinePointsPerFrame`（单一数据源）。

- [ ] **Step 3：接 playhead listener**

帧变化：更新 playhead x、帧号。**不要** rebuild 侧栏/轨道。

- [ ] **Step 4：标尺 + 播放头 scrub 手势**

对齐 `TimelineContentView.scrubGesture` / `GraphPlayheadOverlay`。

- [ ] **Step 5：手测播放**

播放头移动、帧号更新、画布仍播。可选 Instruments：栈里无 SwiftUI `TimelineView`。

- [ ] **Step 6：Commit**

```bash
git commit --only <touched files> -m "Port timeline controls ruler and playhead to UIKit with clock listeners."
```

---

### Task 4：侧栏（图层 / 属性）

**文件：**
- 新建：`Timeline/UIKit/TimelineSidebarView.swift`（+ cell）
- 修改：`TimelineViewController.swift`
- 复用：`buildTimelineRows`、`TimelineReorder.swift`、`LayerColumn` 菜单逻辑

- [ ] **Step 1：用 `buildTimelineRows` 渲染行**

`UITableView` 或 `UICollectionView`。选中更新 `editorState.selectedLayerID` / 属性选中。

- [ ] **Step 2：显隐 / 锁定 + 上下文菜单删除/排列**

从 `LayerRow` / `LayerColumn` 经 `perform` / `registerEdit` 移植。

- [ ] **Step 3：图层拖拽重排**

复用 `TimelineReorder`；编排逻辑从 `LayerColumn` 搬。

- [ ] **Step 4：手测**

选层 ↔ Canvas；重排；删除；展开属性行。

- [ ] **Step 5：Commit**

```bash
git commit --only <touched files> -m "Port timeline layer sidebar interactions to UIKit."
```

---

### Task 5：轨道（时间条 / 属性 / 关键帧）

**文件：**
- 新建：`Timeline/UIKit/TimelineTracksView.swift`（+ 子视图）
- 复用：`TimeRangeDraft`、`KeyframeSegment`、现有拖拽换算
- 修改：`TimelineScrollCoordinator` 与侧栏纵向同步

- [ ] **Step 1：轨道行与侧栏行高对齐**

共享纵向 offset。

- [ ] **Step 2：图层时间条入出点拖拽**

移植 `TimeRangeTrackView`，经 core API + `perform`。

- [ ] **Step 3：属性 span + 关键帧菱形**

选中 / 拖拽移动；用现有帧 clamp 辅助函数。

- [ ] **Step 4：手测**

入出点、关键帧移动、与 Inspector 选中联动（适用处）。

- [ ] **Step 5：Commit**

```bash
git commit --only <touched files> -m "Port timeline track bars and keyframe editing to UIKit."
```

---

### Task 6：滚动 / 缩放 / 指针输入对齐

**文件：**
- 移植或包装：`Timeline/Input/TimelinePointerInputView.swift`（trackpad 捏合/滚动/hover，若仍需要）
- 修改：coordinator + contentSize

- [ ] **Step 1：横向滚动 + 捏合缩放并保留播放头锚点**

对齐 `TimelineContentView.preservePlayheadDuringZoom`。

- [ ] **Step 2：侧栏 ↔ 轨道纵向同步**

- [ ] **Step 3：侧栏宽度分割条**（若 SwiftUI 版有）

- [ ] **Step 4：Catalyst 上手测 trackpad + 鼠标**

- [ ] **Step 5：Commit**

```bash
git commit --only <touched files> -m "Match timeline scroll zoom and pointer input behavior in UIKit."
```

---

### Task 7：缓动 popover + 剩余对齐

**文件：**
- UIKit present 缓动编辑；最快路径可用 **仅 popover 内** 小范围 `UIHostingController` 包 `CubicBezierPad`，或稍后纯 UIKit。选 hosting-for-pad-only 时在 commit 说明例外。

- [ ] **Step 1：关键帧 / 段点击 → 缓动编辑**

接到 `KeyframeEasingPopover` 所用的 `EasingInfo` / core API。

- [ ] **Step 2：按设计 §2.5 清单通测并补洞**

- [ ] **Step 3：Commit**

```bash
git commit --only <touched files> -m "Add UIKit timeline easing editing and finish interaction parity gaps."
```

---

### Task 8：删除 SwiftUI Timeline 宿主面

**文件：**
- 删除无引用：`UIKitTimelineHostView`、`TimelineView.swift` 及其他无引用 SwiftUI Timeline 视图（保留 `TimelineSupport` / `TimelineReorder` / 纯模型）
- 删除前 grep 引用
- 必要时更新 `docs/README`

- [ ] **Step 1：grep 残留符号**

```bash
rg -n "TimelineView|UIKitTimelineHostView|TimelineContentView" apps/MotionStudioApp --glob '*.swift'
```

- [ ] **Step 2：删死文件；修工程引用**

- [ ] **Step 3：Catalyst + iOS Simulator 全量编译**

- [ ] **Step 4：Commit**

```bash
git commit --only <touched files> -m "Remove SwiftUI timeline hosting after UIKit timeline parity."
```

---

### Task 9：验收交接

- [ ] **Step 1：手测** 设计 §2.5 + §3.2
- [ ] **Step 2：可选 Instruments 播放** — Timeline/`UIHosting` 栈应接近消失；Inspector 打开时 AG 可残留
- [ ] **Step 3：记录残留问题**；未要求则不合并

---

## 执行说明

推荐 **subagent-driven-development**，一任务一评审闸门。Task 2 之后应用必须能带着 stub Timeline 跑起来，便于后续增量。
