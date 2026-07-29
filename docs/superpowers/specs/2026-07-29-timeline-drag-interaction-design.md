# Timeline 拖动交互对齐（Figma Motion）

> 分支：`feature/0x1306a94_timeline_uikit`  
> 日期：2026-07-29  
> 相关：[Timeline UIKit 迁移](2026-07-29-timeline-uikit-migration-design.md)

## 目标

对齐 Figma Motion 的 timeline 拖动语义：

| 行 | 控件 | 拖动 | 写入范围 |
|---|---|---|---|
| layer | timeRangeBar | 仅左右把手 | 该 layer **所有** animated paths，**比例缩放** |
| propertySpan | 预置动画条 | 仅左右把手（开始/结束） | **仅当前 path** 首或尾帧，**直接移动** |
| keyframeTrack | 菱形 | 单个关键帧 | **仅该帧**，夹在邻帧之间 |

拖动过程中 timeRangeBar **始终**为该 layer 下所有 propertySpan / keyframeTrack 关键帧的 `min…max`（派生显示，无独立存储）。

## 已确认取舍

| 决策 | 选择 |
|---|---|
| 实现位置 | App 层拖动引擎 + 现有 `moveKeyframe`；不新增 core 命令 |
| timeRangeBar | 只把手，不拖 body |
| timeRangeBar 算法 | 锚住另一端，对该 layer 全部 keyframe **比例缩放**（见实测） |
| propertySpan | 只动该 path 开始或结束帧（预置动画通常两端） |
| keyframeTrack | 恢复单菱形拖动；不可越过邻帧 |
| 拖动中 UI | 挂起 `rebuildRows`，就地刷新；松手再完整 reload |

### 比例缩放实测（Figma Motion，拖 trailing）

```
拖动前: k0=0, k1=492, k3=600
拖动后: k0=0, k1=658, k3=802
```

`802/600 ≈ 1.337`，`492 × 1.337 ≈ 658` → 以固定端为锚点线性映射。

## 1. 交互语义

```
Layer row          → timeRangeBar（左右把手）
  propertySpan     → 预置条（左右把手，仅首/尾）
  keyframeTrack    → 线段 + 菱形（只拖单个菱形）
```

### 1.1 timeRangeBar（layerScale）

- 左把手：`anchor = origin.end`，`newEdge` 夹在 `[0, origin.end - 1]`（或与内部次外帧约束一致，保证至少两端可区分）
- 右把手：`anchor = origin.start`，`newEdge` 夹在 `[origin.start + 1, duration]`
- 对快照中每个 `(path, frame)`：
  - `to = round(anchor + (frame - anchor) * (newEdge - anchor) / (oldEdge - anchor))`
- 批量 `moveKeyframe`；同一 tick 内先算完全部目标再应用，避免中间态撞车
- 视觉：同步刷新该 layer 下所有 propertySpan / keyframeTrack，以及 timeRangeBar envelope

### 1.2 propertySpan（propertyEdge）

- 左/右把手对应该 path 当前 `min` / `max` 帧
- `moveKeyframe(path, from: edgeFrame, to: clampedTarget)`
- 中间帧不动
- 视觉：只刷新当前 propertySpan；**同时**刷新同 layer 的 timeRangeBar（envelope 可能变）

### 1.3 keyframeTrack（单帧）

- 拖菱形：`to ∈ [max(0, prev+1), min(duration, next-1)]`（无邻则用 `0…duration`）
- 视觉：只刷新当前 keyframeTrack + 同 layer timeRangeBar

### 1.4 Undo

- 沿用 `beginDrag` / `endDrag` + `registerEdit`
- 文案：`"Scale Time Range"` / `"Move Property Range"` / `"Move Keyframe"`

## 2. 架构

### 2.1 纯逻辑（可单测）

新建 `Timeline/Tracks/TimelineDragEngine.swift`（名称可微调）：

```swift
enum TimelineDragScope {
    case layerScale
    case propertyEdge(path: String)
    case keyframe(path: String, frame: Int64)
}

struct TimelineDragSession {
    let scope: TimelineDragScope
    let edge: TimeRangeDragEdge?          // keyframe 为 nil
    let originStart: Int64
    let originEnd: Int64
    let originFrames: [(path: String, frame: Int64)] // layerScale 快照
}

struct KeyframeMove: Equatable {
    let path: String
    let from: Int64
    let to: Int64
}

enum TimelineDragEngine {
    static func beginSession(...) -> TimelineDragSession?
    static func resolve(
        session: TimelineDragSession,
        pointerFrame: Int64,
        duration: Int64,
        currentEdgeFrame: Int64?,       // propertyEdge
        neighbors: (prev: Int64?, next: Int64?)? // keyframe
    ) -> [KeyframeMove]
}
```

### 2.2 UIKit

`TimelineTracksView` / `TimelineTrackRowView`：

- timeRangeBar / propertySpan 把手 → session + `resolve` + apply
- 恢复 `TimelineKeyframeDiamondView` pan（邻帧夹逼）
- 拖动中 `suspendsReload`；`onDragMoved` 就地刷新：
  - layerScale：同 layer 全部相关行
  - propertyEdge / keyframe：自身 + 该 layer 的 timeRangeBar 行

### 2.3 不变式

- timeRangeBar frame 范围 = `minmax(all keyframes on layer)`，无独立模型字段
- 拖动中禁止销毁持有 active pan 的 row view

## 3. 非目标

- timeRangeBar / propertySpan body 整体平移
- 越过邻帧、松手重排
- Core 新 undo 命令类型
- 像素级对齐 Figma 外观

## 4. 测试计划

- **单元**：`TimelineDragEngine` — trailing/leading 比例缩放（对齐上述 0/492/600 → 0/658/802）；propertyEdge 只动一端；keyframe 邻帧夹逼
- **手工**：拖 timeRangeBar → 下方 span/track 同步；拖 propertySpan → 仅该条变、bar envelope 跟随；拖菱形不过邻帧；undo/redo

## 5. 实现顺序（概要）

1. `TimelineDragEngine` + 单测  
2. 改 timeRangeBar 为 layerScale + 全量就地刷新  
3. propertySpan 左右把手  
4. 恢复菱形拖动 + envelope 刷新  
5. Catalyst 手测三条路径 + undo  
