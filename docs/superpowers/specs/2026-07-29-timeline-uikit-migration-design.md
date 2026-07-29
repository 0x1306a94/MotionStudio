# Timeline SwiftUI → UIKit 迁移设计

> 分支：`feature/0x1306a94_timeline_uikit`  
> 日期：2026-07-29  
> 相关：[playback-cpu 设计](2026-07-29-playback-cpu-optimization-design.md)（Instruments：播放时 AG 主要来自 playhead → SwiftUI）

## 目标

逐步把编辑器从「UIKit 壳 + SwiftUI 面板」迁到 UIKit。**本阶段只迁底部 Timeline**，并让播放头刷新走 UIKit 显式回调，降低播放期 `AttributeGraph` / SwiftUI 成本。

## 已确认取舍

| 决策 | 选择 |
|---|---|
| 范围 | 仅 Timeline；Inspector / ProjectPanel 仍 SwiftUI |
| Playhead | Timeline 用 listener 显式更新；**暂留** `PlayheadClock` 的 `@Observable`（Inspector 仍用） |
| 交付形态 | 整块一次去掉 Timeline 的 `UIHostingController`（可多分 commit） |
| 验收标准 | **功能与交互对齐**；外观大致接近即可，不要求像素级 |

## 1. 架构与 playhead 数据流

### 1.1 结构

```
EditorViewController
  ├── CanvasViewController          // 已有；继续 playheadClock.publish(frame)
  ├── ProjectPanel (SwiftUI host)   // 本阶段不动
  ├── Inspector (SwiftUI host)      // 仍 .environment(playheadClock)
  └── TimelineViewController        // 新增 UIKit，替换 UIKitTimelineHostView hosting
        ├── controls / ruler / sidebar / tracks / playhead overlay
        └── 注册 playhead 监听：clock.addListener { updatePlayheadUI() }
```

### 1.2 PlayheadClock（过渡态）

```swift
@MainActor
@Observable
final class PlayheadClock {
    private(set) var frame: Int64 = 0
    private var listeners: [UUID: (Int64) -> Void] = [:]

    func publish(_ newFrame: Int64) {
        guard newFrame != frame else { return }
        frame = newFrame                 // 仍触发 Observation → Inspector
        listeners.values.forEach { $0(newFrame) }  // UIKit Timeline
    }

    @discardableResult
    func addListener(_ block: @escaping (Int64) -> Void) -> UUID { ... }
    func removeListener(_ id: UUID) { ... }
}
```

### 1.3 关键点

- Timeline **不**再把 `playheadClock` 放进 SwiftUI `environment`，只靠 listener / 读 `frame` + `setNeedsLayout` / `setNeedsDisplay`。
- 去掉 Timeline Hosting 后，播放期 AG 应主要只剩**打开的 Inspector** 订阅；比「Timeline + Inspector」轻很多。
- 本阶段**不**去掉 `@Observable`；等 Inspector 迁 UIKit 或改为非逐帧读 frame 时再拆。
- 可选后续（非本阶段）：`isPlaying` 时 Inspector 停读 `clock.frame`。

## 2. Timeline UIKit 模块与交互对齐

### 2.1 宿主替换

- `EditorViewController`：`UIHostingController<UIKitTimelineHostView>` → 子 VC `TimelineViewController`。
- 功能对齐后删除 `UIKitTimelineHostView`、SwiftUI `TimelineView` 入口及无引用的旧 Timeline SwiftUI 文件。

### 2.2 模块映射

| 现有 SwiftUI | UIKit 落点 |
|---|---|
| `TimelineControls` | `TimelineControlsView` |
| `TimelineRulerView` + playhead overlays | `TimelineRulerView` + `TimelinePlayheadView` |
| `LayerColumn` / `LayerRow` / `PropertySubRow` | `TimelineSidebarView` |
| `TrackRow` / Track / Keyframe 系列 | `TimelineTracksView` |
| pointer input / reorder | 手势 + 现有 reorder 逻辑迁到 VC |
| `KeyframeEasingPopover` | UIKit popover / 独立 VC（功能保留） |
| 竖向 scroller / 同步滚动 | sidebar 与 tracks `contentOffset` 协调 |

### 2.3 依赖注入

与现壳对齐，由 `EditorViewController` 注入：

- `MotionProjectState` / `MotionDocumentCore` / `EditorState` / `PlayheadClock`
- `perform` / `registerEdit` / `clearSelection`

### 2.4 刷新策略

- **模型**：`core.revision` 变化 → Timeline reload（暂停/编辑时为主）。
- **播放头**：仅 listener → 移动 playhead，**不** rebuild 行。
- **滚动/缩放**：本地 UI state，不进 Observation。

### 2.5 交互对齐清单（第一版必须）

- [ ] 播放 / 暂停 / 停在帧
- [ ] 标尺点击 / 拖拽 scrub
- [ ] 播放头随播放移动（listener，无 SwiftUI）
- [ ] 横向时间缩放、横向滚动；侧栏与轨道纵向同步
- [ ] 选层、展开属性行
- [ ] 图层时间条拖拽（入出点）
- [ ] 关键帧选中 / 拖拽移动
- [ ] 缓动编辑入口
- [ ] 与 `EditorState` / Canvas 选中联动
- [ ] undo 仍走 `perform` / `registerEdit`

### 2.6 目录（按职责拆分，无 `UIKit/` 子目录）

```
Timeline/
  Root/          TimelineViewController, ScrollCoordinator, Support, Reorder
  Controls/      TimelineControlsView
  Sidebar/       TimelineSidebarView
  Tracks/        TimelineTracksView + 拖拽引擎 / draft
  Overlays/      TimelineRulerCanvasView, TimelinePlayheadView
  Input/         TimelinePointerInputOverlay
  Easing/        TimelineEasingPopoverController（曲线 pad 可小范围 Hosting）
```

旧 SwiftUI Timeline 入口与无引用视图已删除；`PlayheadClock.@Observable` 保留（Inspector 与 UIKit listener 并存）。

## 3. 风险、验收、落地

### 3.1 风险

| 风险 | 应对 |
|---|---|
| 交互面大，易漏 | 以 §2.5 为验收；多分 commit，一次去掉 Hosting |
| 保留 `@Observable`，Inspector 打开时仍有 AG | 本阶段接受；Timeline 侧 AG 应明显下降 |
| 侧栏/轨道滚动不同步 | 单一 offset 协调或共用 scroll |
| 缓动 popover / 复杂手势回归 | 先搬逻辑，UIKit 壳；专项手测 |
| 双份代码并存过久 | 对齐后尽快删旧 SwiftUI Timeline |

### 3.2 验收

- §2.5 清单全部通过（功能对齐，外观大致接近）。
- 播放 Time Profiler：Timeline / `UIHosting` 相关栈接近消失；playhead 走 listener。
- 打开 Inspector 播放：允许少量 AG；关闭 Inspector 时更低。
- Mac Catalyst + iPad 模拟器主路径手测。
- 不回归：undo、选中联动、保存。

### 3.3 落地步骤

1. 分支 `feature/0x1306a94_timeline_uikit`（本文件）。
2. 实现计划（writing-plans）后编码。
3. `PlayheadClock` 增加 listener（保留 `@Observable`）。
4. 实现 `TimelineViewController` 并替换 Hosting。
5. 搬齐交互 → 删除旧 Timeline SwiftUI 入口。
6. Instruments + 手测 → 合并 `develop`。

### 3.4 本阶段不做

- Inspector / ProjectPanel UIKit 化
- 去掉 `PlayheadClock.@Observable`
- 画布 Metal flush 优化（独立议题）
- Timeline 虚拟化大重构（除非滚动性能不足）

## 成功指标

- 播放时 CPU：Timeline 引起的 SwiftUI/`AttributeGraph` 从主热点降为可忽略（Inspector 打开时除外）。
- 编辑体验：现有 Timeline 主路径功能不丢。
