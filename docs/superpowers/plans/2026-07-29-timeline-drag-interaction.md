# Timeline 拖动交互对齐 Implementation Plan

> **给执行代理：** 必需子技能：用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 按任务推进。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 对齐 Figma Motion：timeRangeBar 全 path 比例缩放；propertySpan 只动首/尾；keyframeTrack 单帧拖动（不越邻帧）；拖动中 bar 始终为 envelope。

**架构：** 纯逻辑 `TimelineDragEngine` 产出 `[KeyframeMove]`；UIKit 手势调用 `moveKeyframe`；沿用 `suspendsReload` 就地刷新。不改 core / bridge。

**技术栈：** Swift；`MotionDocumentCore.moveKeyframe`；Swift Testing（`MotionStudioAppTests`）；UIKit Timeline。

**设计文档：** [docs/superpowers/specs/2026-07-29-timeline-drag-interaction-design.md](../specs/2026-07-29-timeline-drag-interaction-design.md)

## 全局约束

- App 层引擎 + 现有 `moveKeyframe`；不新增 core 命令。
- timeRangeBar 只左右把手；算法为锚点比例缩放。
- propertySpan 只直接移动该 path 首或尾帧。
- keyframe 夹在 `(prev+1)…(next-1)`，再夹 `[0, duration]`。
- 拖动中禁止 `rebuildRows`；松手 `endDrag` + `registerEdit`。
- `if`/`switch` 体必须 `{}`；长行优先。
- **不要自动 commit**；仅当用户明确要求时再提交。
- 不主动 push；在 `feature/0x1306a94_timeline_uikit` 上工作。

## 目标文件地图

| 路径 | 职责 |
|---|---|
| `Timeline/Tracks/TimelineDragEngine.swift` | 新建：session / resolve / KeyframeMove |
| `MotionStudioAppTests/TimelineDragEngineTests.swift` | 新建：引擎单测 |
| `Timeline/UIKit/TimelineTracksView.swift` | 改：三路手势 + 按 scope 就地刷新 |
| `Timeline/Tracks/TimeRangeDraft.swift` | 按需：若引擎复用则保持；否则不动 |
| `Timeline/Tracks/TimeRangeDragEdge.swift` | 复用 |

---

### Task 1：TimelineDragEngine + 单测（TDD）

**文件：**
- 新建：`apps/MotionStudioApp/MotionStudioApp/Timeline/Tracks/TimelineDragEngine.swift`
- 新建：`apps/MotionStudioApp/MotionStudioApp/MotionStudioAppTests/TimelineDragEngineTests.swift`
- 若工程非文件夹同步：把新 Swift 文件加入 App / Tests target（与 `TimelineReorder` 相同方式）

**接口：**
- 产出：
  - `struct KeyframeMove: Equatable { let path: String; let from: Int64; let to: Int64 }`
  - `enum TimelineDragScope { case layerScale; case propertyEdge(path: String); case keyframe(path: String, frame: Int64) }`
  - `struct TimelineDragSession`（见 spec §2.1）
  - `enum TimelineDragEngine`
    - `static func makeLayerScaleSession(edge: TimeRangeDragEdge, originStart: Int64, originEnd: Int64, originFrames: [(path: String, frame: Int64)]) -> TimelineDragSession?`
    - `static func makePropertyEdgeSession(path: String, edge: TimeRangeDragEdge, originStart: Int64, originEnd: Int64) -> TimelineDragSession?`
    - `static func makeKeyframeSession(path: String, frame: Int64) -> TimelineDragSession`
    - `static func resolve(session: TimelineDragSession, pointerFrame: Int64, duration: Int64, neighbors: (prev: Int64?, next: Int64)?) -> [KeyframeMove]`
- 约定：`from == to` 的 move 不返回；layerScale 从 **originFrames 快照**映射，不读当前文档。

- [ ] **Step 1：写失败单测（比例缩放对齐 Figma 数据）**

```swift
import Testing
@testable import MotionStudio

struct TimelineDragEngineTests {
    @Test
    func `layer scale trailing matches figma sample`() {
        let session = TimelineDragEngine.makeLayerScaleSession(
            edge: .trailing,
            originStart: 0,
            originEnd: 600,
            originFrames: [
                ("p", 0), ("p", 492), ("p", 600),
            ]
        )!
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: 802,
                                               duration: 10_000,
                                               neighbors: nil)
        let byFrom = Dictionary(uniqueKeysWithValues: moves.map { ($0.from, $0.to) })
        #expect(byFrom[0] == nil || byFrom[0] == 0) // 锚点不动可不发 move
        #expect(byFrom[492] == 658)
        #expect(byFrom[600] == 802)
    }
}
```

另加：`layer scale leading`（对称）、`property edge only moves edge`、`keyframe clamped by neighbors`。

- [ ] **Step 2：跑测确认失败**

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -destination 'platform=macOS,variant=Mac Catalyst' \
  -only-testing:MotionStudioAppTests/TimelineDragEngineTests test
```

Expected：编译失败或测试失败（类型未定义）。

- [ ] **Step 3：实现 `TimelineDragEngine`**

层缩放核心：

```swift
let anchor = (edge == .trailing) ? originStart : originEnd
let oldEdge = (edge == .trailing) ? originEnd : originStart
let newEdge = /* clamp pointer */
guard oldEdge != anchor else { return [] }
let scale = Double(newEdge - anchor) / Double(oldEdge - anchor)
// to = Int64((Double(anchor) + Double(frame - anchor) * scale).rounded())
```

propertyEdge：`from = edge 对应 originStart/End`，`to = clamp(pointer)`（leading：`[0, originEnd-1]`；trailing：`[originStart+1, duration]`），单条 move。  
keyframe：`to` 夹邻帧与 duration。

批量 move 应用顺序（UI 层 Task 2）：按 `from` 远离目标方向排序，或「全部算完 → 若路径冲突用临时帧」——引擎只保证输出正确目标列表；**同一 path 多帧时** resolve 应返回互不覆盖的 `from→to`（可用两阶段：先映射到字典 `from→to`，再输出）。若两帧 round 到同一 `to`，后者夹到合法空档或保持稳定排序后微调——实现时优先「round 后若碰撞则按原序挤开 ±1」，并在测试里覆盖；若过复杂，第一版保证 Figma 样本与无碰撞路径正确，碰撞用断言/注释标明 follow-up。

- [ ] **Step 4：跑测通过**

同上 `xcodebuild … -only-testing:…TimelineDragEngineTests test`。Expected：PASS。

- [ ] **Step 5：Commit（仅用户要求时）**

```bash
# 用户明确要求后再执行
git add apps/MotionStudioApp/MotionStudioApp/Timeline/Tracks/TimelineDragEngine.swift \
        apps/MotionStudioApp/MotionStudioAppTests/TimelineDragEngineTests.swift
git commit -m "$(cat <<'EOF'
Add TimelineDragEngine for Figma-style range and keyframe drag math.

EOF
)"
```

---

### Task 2：timeRangeBar 改为 layerScale + 全量就地刷新

**文件：**
- 修改：`apps/MotionStudioApp/MotionStudioApp/Timeline/UIKit/TimelineTracksView.swift`
  - `TimelineTrackRowView.updateRangeDrag` / `endRangeDrag`
  - `TimelineTracksView` 的 `onTimeRangeMoved` 刷新范围

**接口：**
- 消费：`TimelineDragEngine.makeLayerScaleSession` / `resolve`
- 产出：拖 timeRangeBar 时同 layer 所有 row 就地 `updateMetrics`；registerEdit 文案 `"Scale Time Range"`

- [ ] **Step 1：开始拖时建 session**

在 `dragStartRange == nil` 分支：收集 `timelineAnimatedPropertyPaths` 下全部 `(path, frame)` 为 `originFrames`；`makeLayerScaleSession(edge:originStart:originEnd:originFrames:)`；`beginDrag()`；`beginInteractiveEdit()`。

- [ ] **Step 2：每 tick resolve + apply**

```swift
let moves = TimelineDragEngine.resolve(session: session,
                                       pointerFrame: pointerFrame - dragFrameOffset /* 或直接 pointer，与现 offset 语义对齐 */,
                                       duration: duration,
                                       neighbors: nil)
for move in moves where move.from != move.to {
    document.core.moveKeyframe(entityID: row.layerID, path: move.path,
                               from: move.from, to: move.to)
}
onTimeRangeMoved?() // 刷新同 layer 全部行 + bar
```

注意：比例缩放必须相对 **origin 快照**，不能相对「当前边」逐步挪（否则会累积误差）。`pointerFrame` 应映射为 `newEdge`，session 内 `originStart/End` 固定。

- [ ] **Step 3：刷新策略**

`onTimeRangeMoved`：对该 `layerID` 的 **所有** `rowViews` 调用 `updateMetrics`（含 propertySpan / keyframeTrack / layer bar）。勿 `isHidden` 切换 timeRangeBar（防取消 pan）。

- [ ] **Step 4：Catalyst 手测**

拖 trailing：下方关键帧按比例拉开；松手 undo 一次恢复。

- [ ] **Step 5：Commit（仅用户要求时）**

---

### Task 3：propertySpan 左右把手

**文件：**
- 修改：`TimelineTracksView.swift` — `TimelinePropertyBarView` 增加 leading/trailing handle（可仿 `TimelineTimeRangeBarView`）；`layoutPropertySpan` 接线势

**接口：**
- 消费：`makePropertyEdgeSession` / `resolve`
- 产出：只 `moveKeyframe` 当前 path 边帧；刷新自身 + 同 layer timeRangeBar；edit `"Move Property Range"`

- [ ] **Step 1：PropertyBar 把手 UI**

复用 timeRangeBar 把手尺寸/命中；body tap 仍选中 property。

- [ ] **Step 2：手势 → engine → apply**

session 仅含该 path 的 `originStart/End`（该 path min/max）。resolve 出 0…1 条 move。`beginInteractiveEdit`；moved 时：`reloadContent` 当前行 + `refreshTimeRangeOnly` 同 layer 的 layer 行。

- [ ] **Step 3：手测**

拖 propertySpan 右端：仅该条变长/变短；timeRangeBar envelope 跟随；其它 path 关键帧不动。

- [ ] **Step 4：Commit（仅用户要求时）**

---

### Task 4：恢复 keyframe 菱形拖动

**文件：**
- 修改：`TimelineTracksView.swift` — `TimelineKeyframeDiamondView` 恢复 pan；`layoutKeyframeTrack` 接线

**接口：**
- 消费：`makeKeyframeSession` / `resolve(..., neighbors:)`
- 产出：单帧 move；刷新当前 track + layer timeRangeBar；edit `"Move Keyframe"`

- [ ] **Step 1：Diamond pan**

`onMoved(translationX)` / `onMoveEnded`；began 时 `beginDrag` + `beginInteractiveEdit`；neighbors 从当前 path 排序帧计算。

- [ ] **Step 2：apply + 刷新**

每 tick resolve（`pointer` 由 originFrame + translation/pointsPerFrame）；move 后刷新本行 diamonds/segments + 同 layer bar。

- [ ] **Step 3：手测**

中间帧不可越过左右邻；拖最右帧拉大 envelope；undo 正常。

- [ ] **Step 4：Commit（仅用户要求时）**

---

### Task 5：收尾验收

- [ ] **Step 1：跑引擎单测**

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -destination 'platform=macOS,variant=Mac Catalyst' \
  -only-testing:MotionStudioAppTests/TimelineDragEngineTests test
```

- [ ] **Step 2：Catalyst 构建**

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -configuration Debug \
  -destination 'generic/platform=macOS,variant=Mac Catalyst,name=Any Mac' \
  ARCHS=arm64 build
```

- [ ] **Step 3：手工清单**

1. timeRangeBar trailing/leading → 全 path 比例缩放，下方同步  
2. propertySpan 把手 → 只动本 path 端点，bar envelope 更新  
3. 菱形 → 不越邻帧，bar envelope 更新  
4. 三条路径各自 undo/redo  
5. 拖动中无「卡死只动一点」（gesture 未被 rebuild 掐断）

- [ ] **Step 4：向用户汇报**；commit 等用户指示

---

## 执行方式

计划写好后，可选：

1. **Subagent-Driven（推荐）** — `superpowers:subagent-driven-development`，任务间复查  
2. **Inline** — `superpowers:executing-plans`，本会话按任务推进  

哪个都可以；用户未指定时默认在本会话按 Task 1→5 执行。
