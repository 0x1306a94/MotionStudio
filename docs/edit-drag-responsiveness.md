# 编辑拖动跟手性（过渡方案）

Instruments（Animation Hitches / Time Profiler）显示：画布拖变换时主线程卡顿主要来自 **UI 对 `revision` 的全量反应**，而不是 Metal `PlayCommands` 本身。计划将 Inspector / ProjectPanel 全部迁到 UIKit；在此之前用下列过渡手段保证拖动跟手。UIKit 化完成后删除 `panelRevision` 及相关节流代码。

## 瓶颈分层（实测）

| 阶段 | 主因 | 处理 |
|---|---|---|
| 点文本 / 字体 | 每帧 `ResolvePointTextContainerSizes` → `ResolveTextTypeface` / `MeasurePointTextSize` | adapter 进程内 LRU（`TgfxTextTypeface` / `MeasurePointTextSize`） |
| 编辑刷新率 | 编辑态 `MTKView.preferredFramesPerSecond` 被压成合成帧率 | 编辑用屏幕最大刷新；播放仍用合成帧率 |
| Timeline | 任意 `revision` → `reloadFromDocument` → `rebuildRows` | merge group 期间推迟，松手再刷 |
| SwiftUI Inspector / ProjectPanel | 订阅即时 `revision` + `.id(...-revision)` 触发 AttributeGraph / `sizeThatFits` | 改订 `panelRevision`（见下） |

## `revision` vs `panelRevision`

实现：`MotionDocumentCore`（`apps/MotionStudioApp/.../Model/MotionDocumentCore.swift`）。

| 计数器 | 时机 | 订阅方 |
|---|---|---|
| `revision` | `changed()` 立即 +1，并同步 bridge `contentRevision` | Canvas、Timeline（UIKit）、命令探测、缓存失效 |
| `panelRevision` | **过渡**；非 merge 与 `revision` 同步；merge 中只记 pending；`endMergeGroup` 立刻 flush | 临时 SwiftUI：Inspector、ProjectPanel |

规则：

1. **不要**节流 bridge `contentRevision` / `revision`——画布与场景命令缓存依赖即时世代。
2. SwiftUI 面板只读 `panelRevision`（含 `.id(...)`）；点选立刻更新；拖动/scrub（merge）中不刷新，松手立刻对齐。无 idle debounce。
3. Timeline 另用 `mergeGroupDepth`：merge 中跳过 `reloadFromDocument`，depth→0 且有 pending 时刷一次（与 `panelRevision` 独立，但同属「拖动不重建结构 UI」）。

## 删除清单（SwiftUI → UIKit 后）

- `MotionDocumentCore.panelRevision`、`panelRevisionPending`、`flushPanelRevision`
- 所有 `core.panelRevision` 订阅（随面板 UIKit 化自然消失）
- 本文档可改为「已删除」短注或移除索引条目

## 相关文档

- [architecture.md](architecture.md) — 桥接变更通知总述
- [superpowers/specs/2026-07-29-playback-cpu-optimization-design.md](superpowers/specs/2026-07-29-playback-cpu-optimization-design.md) — 播放侧 CPU（与编辑拖动正交）
- [superpowers/specs/2026-07-29-timeline-uikit-migration-design.md](superpowers/specs/2026-07-29-timeline-uikit-migration-design.md) — Timeline 已迁 UIKit
