# MP4 Video Export UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在编辑器 File 菜单提供 Export MP4…：质量设置 sheet → 后台导出临时文件（进度/取消）→ `UIDocumentPicker(forExporting:)`。

**Architecture:** UIKit 编排挂在 `EditorViewController`；纯逻辑（偶数尺寸、码率）放 `VideoExportOptionsBuilder` 便于单测；`MotionDocumentCore.exportVideo` 薄封装 `ms_video_export`；进度在后台线程回调里 `MainActor` 刷新。

**Tech Stack:** UIKit（Catalyst + iPad）、`MotionStudioBridging` / `ms_video_export`、Swift Testing / XCTest（App tests）、文件系统同步 Xcode 工程（新文件放 `MotionStudioApp/` 下自动入 target）。

**Spec:** `docs/superpowers/specs/2026-07-31-mp4-video-export-ui-design.md`

## Global Constraints

- 入口仅 File 菜单；无工具栏按钮。
- 分辨率=合成原始（奇数 evenFloor）；时间=整段；质量仅 Low/Medium/High。
- 流程：设置 → 进度（写临时 MP4）→ forExporting picker → 清理临时文件。
- 取消静默；失败才 alert。
- 导出用 `firstCompositionID`；文档 mutex 由 bridge 提供。
- Commit：英语一句、句号结尾；先更新本 plan checkbox 再提交。
- App UI 含交互：实现后本地验证，**人工确认再提交**（或按用户要求提交）。

## File Map

| 文件 | 职责 |
|---|---|
| `apps/.../Export/VideoExportOptionsBuilder.swift` | evenFloor、default bitrate、质量映射（纯函数） |
| `apps/.../Export/VideoExportSession.swift` | 临时目录、cancelFlag、调 core.exportVideo |
| `apps/.../Export/VideoExportSettingsViewController.swift` | 设置 sheet |
| `apps/.../Export/VideoExportProgressViewController.swift` | 进度 sheet |
| `apps/.../Editor/EditorViewController+Export.swift` | 编排 present / picker |
| `apps/.../Model/MotionDocumentCore.swift` | `exportVideo(...)` |
| `apps/.../App/AppDelegate.swift` | 菜单项 |
| `apps/.../Editor/EditorViewController.swift` | `isVideoExportInProgress` 等状态（若需要） |
| `apps/.../MotionStudioAppTests/VideoExportOptionsBuilderTests.swift` | 纯逻辑单测 |

---

### Task 1: VideoExportOptionsBuilder + 单测

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Export/VideoExportOptionsBuilder.swift`
- Create: `apps/MotionStudioApp/MotionStudioAppTests/VideoExportOptionsBuilderTests.swift`

**Interfaces:**
- Produces:
  - `enum VideoExportQuality: Int { case low, medium, high }`
  - `struct VideoExportResolvedSettings { width, height, bitrateBps, profile, durationFrames, frameRateNum, frameRateDen }`
  - `VideoExportOptionsBuilder.evenFloor(_:) -> Int`
  - `VideoExportOptionsBuilder.defaultBitrateBps(width:height:frameRate:) -> Int`
  - `VideoExportOptionsBuilder.resolve(size:duration:frameRate:quality:) -> VideoExportResolvedSettings`

- [x] **Step 1: 写失败测试**

```swift
import Testing
@testable import MotionStudioApp

struct VideoExportOptionsBuilderTests {
    @Test func evenFloorOddBecomesEven() {
        #expect(VideoExportOptionsBuilder.evenFloor(1921) == 1920)
        #expect(VideoExportOptionsBuilder.evenFloor(1080) == 1080)
        #expect(VideoExportOptionsBuilder.evenFloor(1) == 0)
    }

    @Test func defaultBitrateMatchesCoreFormula() {
        let bps = VideoExportOptionsBuilder.defaultBitrateBps(width: 1920, height: 1080, frameRate: 30)
        #expect(bps == 6_220_800)
    }

    @Test func qualityScalesBitrateAndProfile() {
        let size = CGSize(width: 64, height: 64)
        let low = VideoExportOptionsBuilder.resolve(size: size, duration: 10, frameRate: 30, quality: .low)
        let med = VideoExportOptionsBuilder.resolve(size: size, duration: 10, frameRate: 30, quality: .medium)
        let high = VideoExportOptionsBuilder.resolve(size: size, duration: 10, frameRate: 30, quality: .high)
        #expect(low.profile == 1)
        #expect(med.profile == 2)
        #expect(high.profile == 2)
        #expect(low.bitrateBps <= med.bitrateBps)
        #expect(high.bitrateBps >= med.bitrateBps)
        #expect(med.width == 64 && med.height == 64)
    }
}
```

若 App target 模块名不是 `MotionStudioApp`，改为工程实际 `@testable import` 名（见现有 tests）。

- [x] **Step 2: 跑测试确认失败**

用 Xcode MCP `BuildProject`（优先）或：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -destination 'platform=macOS,variant=Mac Catalyst' -only-testing:MotionStudioAppTests/VideoExportOptionsBuilderTests test
```

Expected: 编译失败（类型不存在）。

- [x] **Step 3: 实现 Builder**

```swift
import CoreGraphics
import Foundation

enum VideoExportQuality: Int, CaseIterable {
    case low, medium, high
}

struct VideoExportResolvedSettings {
    var width: Int
    var height: Int
    var durationFrames: Int64
    var frameRateNum: Int
    var frameRateDen: Int
    var bitrateBps: Int
    var profile: Int
}

enum VideoExportOptionsBuilder {
    static func evenFloor(_ value: Int) -> Int {
        max(0, value - (value % 2))
    }

    static func defaultBitrateBps(width: Int, height: Int, frameRate: Double) -> Int {
        let raw = Double(width) * Double(height) * frameRate * 0.1
        return Int(min(max(raw, 1_000_000), 50_000_000))
    }

    static func resolve(size: CGSize, duration: Int64, frameRate: Double,
                        quality: VideoExportQuality) -> VideoExportResolvedSettings {
        let width = evenFloor(Int(size.width.rounded()))
        let height = evenFloor(Int(size.height.rounded()))
        let fps = frameRate > 0 ? frameRate : 30
        let base = defaultBitrateBps(width: width, height: height, frameRate: fps)
        let bitrate: Int
        let profile: Int
        switch quality {
        case .low:
            bitrate = max(1_000_000, Int(Double(base) * 0.5))
            profile = 1
        case .medium:
            bitrate = base
            profile = 2
        case .high:
            bitrate = min(50_000_000, Int(Double(base) * 2.0))
            profile = 2
        }
        // Approximate num/den: prefer integer fps when close.
        let den = 1
        let num = max(1, Int(fps.rounded()))
        return VideoExportResolvedSettings(
            width: width, height: height, durationFrames: max(0, duration),
            frameRateNum: num, frameRateDen: den, bitrateBps: bitrate, profile: profile)
    }
}
```

- [x] **Step 4: 测试通过后更新 plan 并提交**

```bash
# 跑 VideoExportOptionsBuilderTests
git add apps/MotionStudioApp/MotionStudioApp/Export/VideoExportOptionsBuilder.swift \
  apps/MotionStudioApp/MotionStudioAppTests/VideoExportOptionsBuilderTests.swift \
  docs/superpowers/plans/2026-07-31-mp4-video-export-ui.md \
  docs/superpowers/specs/2026-07-31-mp4-video-export-ui-design.md
git commit -m "Add video export options builder and unit tests."
```

---

### Task 2: MotionDocumentCore.exportVideo

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Create: `apps/MotionStudioApp/MotionStudioAppTests/VideoExportCoreTests.swift`（可选：用临时文档导出 1 帧；Metal 不可用则 skip）

**Interfaces:**
- Consumes: `ms_video_export`、`MSVideoExportOptions`
- Produces:
  - `enum VideoExportError: Error { case cancelled; case failed(String) }`
  - `nonisolated func exportVideo(compositionID:options:progress:isCancelled:) throws`

- [ ] **Step 1: 在 `MotionDocumentCore` 增加导出 API**

在文件顶部（或同文件内）增加：

```swift
enum VideoExportError: Error {
    case cancelled
    case failed(String)
}
```

在 `MotionDocumentCore` 内增加（字段类型按 Swift 导入的 `MSVideoExportOptions` 微调 `Int`/`Int32`）：

```swift
/// Runs on the calling thread. `progress` may be invoked off the main actor.
nonisolated func exportVideo(
    compositionID: UInt64,
    outputPath: String,
    resolved: VideoExportResolvedSettings,
    progress: (@Sendable (Int64, Int64) -> Bool)?,
    cancelFlag: UnsafePointer<Int32>
) throws {
    final class ProgressBox: @unchecked Sendable {
        let progress: (@Sendable (Int64, Int64) -> Bool)?
        init(_ progress: (@Sendable (Int64, Int64) -> Bool)?) {
            self.progress = progress
        }
    }
    let box = ProgressBox(progress)
    try outputPath.withCString { path in
        var options = MSVideoExportOptions()
        options.outputPath = path
        options.startFrame = 0
        options.endFrame = resolved.durationFrames
        options.width = Int32(resolved.width)
        options.height = Int32(resolved.height)
        options.frameRateNum = Int32(resolved.frameRateNum)
        options.frameRateDen = Int32(resolved.frameRateDen)
        options.bitrateBps = Int32(resolved.bitrateBps)
        options.keyframeInterval = 0
        options.profile = Int32(resolved.profile)

        var error: UnsafeMutablePointer<CChar>?
        let ok = ms_video_export(
            handle,
            compositionID,
            &options,
            { ctx, completed, total in
                guard let ctx else { return true }
                let box = Unmanaged<ProgressBox>.fromOpaque(ctx).takeUnretainedValue()
                return box.progress?(completed, total) ?? true
            },
            Unmanaged.passUnretained(box).toOpaque(),
            UnsafeRawPointer(cancelFlag).assumingMemoryBound(to: Int32.self),
            &error
        )
        if ok {
            return
        }
        let message = Self.takeString(error) ?? "export failed"
        if message == "cancelled" {
            throw VideoExportError.cancelled
        }
        throw VideoExportError.failed(message)
    }
}
```

若 `cancelFlag` 参数类型与导入的 `UnsafePointer<Int32>?` / `UnsafeMutablePointer` 不匹配，改成与 `ms_video_export` 声明一致的一种，并在 Session 里用 class 持有 `var flag: Int32` 以保证地址稳定。

- [ ] **Step 2: 编译 App / 跑现有 tests**

优先 Xcode MCP Build。Expected: 成功。

- [ ] **Step 3: 更新 plan 并提交**

```bash
git commit -m "Expose nonisolated MP4 export on MotionDocumentCore."
```

---

### Task 3: Settings + Progress view controllers

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Export/VideoExportSettingsViewController.swift`
- Create: `apps/MotionStudioApp/MotionStudioApp/Export/VideoExportProgressViewController.swift`

**Interfaces:**
- Settings: `var quality: VideoExportQuality`；`var onExport: ((VideoExportQuality) -> Void)?`；`var onCancel: (() -> Void)?`；init 传入 summary 字符串（尺寸/时长）
- Progress: `func update(completed:total:)`；`var onCancel: (() -> Void)?`；`isModalInPresentation = true`

- [ ] **Step 1: 实现 Settings VC**

UIKit：`UINavigationController` 包一层，title `"Export MP4"`，右栏 Cancel，主内容：

- `UILabel` 多行 summary（如 `1920×1080 · 150 frames · 30 fps`）
- `UISegmentedControl`：Low / Medium / High
- 底部 primary `Export` button

Export 时若 `durationFrames == 0`：alert `"Composition has no frames to export."`

用 `.formSheet` / `.pageSheet` present。

- [ ] **Step 2: 实现 Progress VC**

- `UIProgressView`
- `UILabel`：`"42 / 150"`
- `UIButton` Cancel
- `isModalInPresentation = true`
- Cancel → `onCancel?()`

- [ ] **Step 3: 更新 plan 并提交**（UI 可先提交骨架，人工点看布局）

```bash
git commit -m "Add video export settings and progress view controllers."
```

---

### Task 4: VideoExportSession + EditorViewController+Export + 菜单

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Export/VideoExportSession.swift`
- Create: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Export.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift`（加 `videoExportSession` / `exportTemporaryDirectoryURL`）
- Modify: `apps/MotionStudioApp/MotionStudioApp/App/AppDelegate.swift`
- Modify: `EditorViewController+Saving.swift` 的 `UIDocumentPickerDelegate` **或** 在 Export extension 里用独立 delegate 对象，避免与 Save As 冲突

**Interfaces:**
- `VideoExportSession`：创建临时 URL、持有 `cancelFlag`、`start`/`cancel`
- `@objc func exportMP4()`
- `canPerformAction`：导出中禁用 Export / Save 可选

- [ ] **Step 1: Session**

```swift
@MainActor
final class VideoExportSession {
    private(set) var temporaryDirectoryURL: URL?
    private(set) var outputURL: URL?
    private var cancelFlag: Int32 = 0

    func prepareOutputURL(projectName: String) throws -> URL { ... }
    func requestCancel() { cancelFlag = 1 }
    func cancelFlagPointer() -> UnsafeMutablePointer<Int32> { withUnsafeMutablePointer... } // store on heap class

    // Better: class CancelState { var flag: Int32 = 0 }
}
```

用 `final class CancelState: @unchecked Sendable { var flag: Int32 = 0 }`，Cancel 按钮写 `flag = 1`，`ms_video_export` 传 `UnsafePointer(&state.flag)` 需固定地址——用 class 存储即可。

- [ ] **Step 2: `exportMP4` 编排**

```swift
@objc func exportMP4() {
    guard videoExportSession == nil else { return }
    let core = document.core
    let compositionID = core.firstCompositionID
    guard compositionID != 0 else { present alert; return }
    let size = core.size(compositionID: compositionID)
    let duration = core.duration(compositionID: compositionID)
    let fps = core.frameRate(compositionID: compositionID)
    let summary = "..."
    let settings = VideoExportSettingsViewController(summary: summary, durationFrames: duration)
    settings.onExport = { [weak self] quality in
        self?.dismiss(animated: true) {
            self?.beginVideoExport(quality: quality, compositionID: ..., size:..., duration:..., fps:...)
        }
    }
    settings.onCancel = { [weak self] in self?.dismiss(animated: true) }
    present(UINavigationController(rootViewController: settings), animated: true)
}

func beginVideoExport(...) {
    let session = VideoExportSession()
    videoExportSession = session
    let resolved = VideoExportOptionsBuilder.resolve(...)
    let url = try session.prepareOutputURL(projectName: ...)
    let progressVC = VideoExportProgressViewController()
    progressVC.onCancel = { session.requestCancel() }
    present(progressVC, animated: true)
    let core = document.core
    let cancelState = session.cancelState
    Task.detached(priority: .userInitiated) {
        do {
            try core.exportVideo(
                compositionID: compositionID,
                outputPath: url.path(percentEncoded: false),
                resolved: resolved,
                progress: { completed, total in
                    if cancelState.flag != 0 { return false }
                    Task { @MainActor in progressVC.update(completed: completed, total: total) }
                    return true
                },
                cancelFlag: &cancelState.flag // exact API from Task 2
            )
            await MainActor.run { self.finishVideoExportSuccess(outputURL: url, progressVC: progressVC) }
        } catch VideoExportError.cancelled {
            await MainActor.run { self.finishVideoExportCancelled(progressVC: progressVC) }
        } catch {
            await MainActor.run { self.finishVideoExportFailed(error, progressVC: progressVC) }
        }
    }
}
```

Success：dismiss progress → `UIDocumentPickerViewController(forExporting:[url], asCopy:true)` → cleanup on delegate。  
Cancelled：dismiss + cleanup，无 alert。  
Failed：dismiss + alert + cleanup。

- [ ] **Step 3: AppDelegate 菜单**

在 Save As 后插入：

```swift
let exportMP4 = UICommand(title: "Export MP4...",
                          image: nil,
                          action: #selector(EditorViewController.exportMP4))
// 放进 saveMenu children 或独立 inline menu after save
```

- [ ] **Step 4: picker delegate 不冲突**

Save As 与 Export 共用 `EditorViewController: UIDocumentPickerDelegate` 时，用枚举区分：

```swift
enum DocumentPickerPurpose { case saveAs, exportMP4 }
var documentPickerPurpose: DocumentPickerPurpose = .saveAs
```

`didPick` / `wasCancelled` 按 purpose 清理对应临时目录。

- [ ] **Step 5: 手动验证清单**

1. Catalyst：File → Export MP4… → Medium → Export → 进度走完 → Files picker 存盘 → 用 QuickTime 打开  
2. Cancel：导出中点 Cancel → 无失败 alert，无残留大文件  
3. duration=0：Export 按钮禁用或 alert  
4. 导出中再点菜单：无第二次进度（`videoExportSession != nil`）

- [ ] **Step 6: 人工确认后更新 plan 并提交**

```bash
git commit -m "Wire Export MP4 menu flow with progress and Files picker."
```

---

## Spec Coverage

| Spec | Task |
|---|---|
| Options builder / 质量映射 | 1 |
| core.exportVideo | 2 |
| Settings / Progress UI | 3 |
| 菜单 + 编排 + picker + 取消 | 4 |
| evenFloor / 临时文件 / 静默取消 | 1, 4 |

## 执行方式

Plan 写好后请选择：

1. **Subagent-Driven**（每任务新代理）  
2. **Inline Execution**（本会话；每步先改 plan checkbox 再提交）
