# MP4 Video Export UI — 设计说明

日期：2026-07-31  
状态：已确认，待实现  
依赖：库层 `ms_video_export`（见 `2026-07-31-mp4-video-export-design.md`）  
分支：`feature/export_video`

## 目标

在编辑器 App 中提供最小可用的 MP4 导出 UI：

1. File 菜单 **Export MP4…**（⌥⌘E；⌘E 与系统 Find 冲突）；iPad 顶栏导出按钮
2. 设置 sheet：质量三档（分辨率=合成原始；时间=整段）
3. 后台导出 + 模态进度 / 取消
4. 成功后：iPad 用 Share sheet；Catalyst 用 `UIDocumentPicker(forExporting:)`

## 非目标

- Mac Catalyst 工具栏导出按钮（Catalyst 用 File 菜单 + ⌥⌘E）
- 自定义分辨率 / 起止帧 / bitrate / profile 细项 UI
- 导出后自动用系统播放器打开
- 音频、多合成选择（用 `firstCompositionID`）
- SwiftUI 重写编辑器壳（沿用 UIKit `EditorViewController`）

---

## §1 流程（方案 1）

```
File → Export MP4…
        │
        ▼
┌─────────────────────┐
│ Settings sheet      │  质量 Low/Medium/High；只读显示尺寸与时长
│ Export / Cancel     │
└──────────┬──────────┘
           │ Export
           ▼
┌─────────────────────┐
│ Progress sheet      │  写临时 .mp4；进度条 + 帧数；Cancel
│ (modal, 不可下滑关) │
└──────────┬──────────┘
           │ success
           ▼
┌─────────────────────┐
│ UIDocumentPicker    │  forExporting 临时文件
│ (forExporting)      │
└──────────┬──────────┘
           │ done / cancel
           ▼
     删除临时文件
```

失败（非取消）：dismiss 进度 → alert 错误文案。  
取消：`cancelFlag` / progress 返回 false → bridge 返回 `"cancelled"` → 静默关闭进度并清理临时文件（不弹失败 alert）。

---

## §2 设置与编码映射

| UI | 映射到 `MSVideoExportOptions` |
|---|---|
| 分辨率 | `width/height = evenFloor(composition size)`（奇数减 1）；`0` 亦可交给 core，但 UI 侧先算偶数以免困惑 |
| 时间 | `startFrame = 0`，`endFrame = duration`（或 `-1` 表示整段，与 bridge 约定一致） |
| 帧率 | `0/0` → composition |
| 质量 Low | `bitrateBps = max(1_000_000, Int(defaultEstimate * 0.5))`，`profile = 1`（Main） |
| 质量 Medium | `bitrateBps = defaultEstimate`，`profile = 2`（High） |
| 质量 High | `bitrateBps = min(50_000_000, Int(defaultEstimate * 2.0))`，`profile = 2`（High） |

`defaultEstimate` 与 core 默认公式一致：  
`clamp(width * height * fps * 0.1, 1e6, 5e7)`。

设置 sheet 只读展示：合成宽高（导出用偶数尺寸若不同则括号注明）、时长（秒或帧）、帧率。

---

## §3 App 结构

| 类型 | 文件（建议） | 职责 |
|---|---|---|
| 菜单 | `App/AppDelegate.swift` | `Export MP4…` → `#selector(EditorViewController.exportMP4)` |
| 编排 | `Editor/EditorViewController+Export.swift` | present settings / progress / picker；持有 session |
| 设置 UI | `Export/VideoExportSettingsViewController.swift` | 质量 segmented + Export/Cancel |
| 进度 UI | `Export/VideoExportProgressViewController.swift` | 进度条、标签、Cancel；`isModalInPresentation = true` |
| 会话 | `Export/VideoExportSession.swift` | 临时路径、options、`ms_video_export`、cancelFlag、进度回调 |

入口：`@objc func exportMP4()` on `EditorViewController`。  
Composition：`document.core.firstCompositionID`。

---

## §4 线程与文档安全

- `ms_video_export` 在 **后台**（`Task.detached` 或专用 `DispatchQueue`）执行。
- Bridge 文档 mutex：导出期间其它 C API 会阻塞 → 等价禁止编辑；UI 侧仍应避免在导出中发起会改文档的操作（进度模态挡住交互）。
- progress 回调（导出线程）：
  - 可读/写 `cancelFlag`（`Int32` / `atomic`）
  - UI 更新必须 `DispatchQueue.main.async` / `MainActor`
- `MotionDocumentCore.handle` 已是 `nonisolated(unsafe)` + bridge 锁；session 只传 `OpaquePointer` / 经 core 暴露的 `nonisolated` 导出方法更清晰——推荐在 `MotionDocumentCore` 增加：

```swift
nonisolated func exportVideo(compositionID: UInt64,
                             options: MSVideoExportOptions,
                             progress: (@Sendable (Int64, Int64) -> Bool)?,
                             cancelFlag: UnsafePointer<Int32>?) throws
```

内部调 `ms_video_export`；错误抛 `NSError`（`"cancelled"` 用独立错误码便于 UI 静默处理）。

---

## §5 临时文件与分享 / picker

- 目录：`FileManager.default.temporaryDirectory / "MotionStudioExport-<UUID>" / "<ProjectName>.mp4"`
- 项目名：与 Save As 相同（draft → `"Untitled"`）
- **iPad**：成功后 `UIActivityViewController`（相册 / Files / 其它 App）；`completionWithItemsHandler` 后再删临时目录；需 `NSPhotoLibraryAddUsageDescription`
- **Mac Catalyst**：`UIDocumentPickerViewController(forExporting:)`；done / cancel 都删临时目录

---

## §6 错误与边界

| 情况 | 行为 |
|---|---|
| 无 composition / duration==0 | 设置 Export 禁用或点 Export 时 alert |
| 奇数合成尺寸 | UI evenFloor；不弹错 |
| 导出失败 | alert，文案来自 bridge |
| 用户取消 | 无失败 alert |
| Share / picker 取消或完成 | 仅删临时文件 |
| 重复点 Export MP4 | 已有 session 进行中则忽略或菜单 / 按钮 disabled |
| 导出时正在实时预览 | 进入 Export 时暂停播放；结束后不自动恢复 |

---

## 决策记录

| 项 | 选择 |
|---|---|
| UI 深度 | 设置（质量）+ 进度 + Files 导出 |
| 分辨率 / 时间 | 仅合成原始 / 整段 |
| 入口 | File → Export MP4…（⌥⌘E）；iPad 顶栏另有导出按钮 |
| 存盘顺序 | 先渲临时文件；iPad Share sheet / Catalyst forExporting picker |
| 进度 UI | 模态 sheet + Cancel |
