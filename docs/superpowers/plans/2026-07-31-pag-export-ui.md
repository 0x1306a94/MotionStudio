# PAG Export UI Implementation Plan

> **For agentic workers:** Implement task-by-task. Steps use checkbox (`- [ ]`) syntax. **Do not auto-commit** (user request).

**Goal:** File 菜单 Export PAG… + iPad 顶栏 Export 菜单选 MP4/PAG；设置（bitmap fallback 开关）→ 后台导出临时 `.pag` → Share / Files picker。

**Architecture:** 镜像 MP4 UI。`ms_pag_export` 薄封装 `PagExporter`（`frameSource=nullptr`）；App 用 UIKit Session/Settings/Progress；顶栏 `UIButton.menu` 选格式。

**Tech Stack:** C bridge + `pag_export`、UIKit、`MotionDocumentCore`。

**Spec:** `docs/superpowers/specs/2026-07-31-pag-export-ui-design.md`

## Global Constraints

- 矢量优先；`allowBitmapFallback` 默认 false；无生产 FrameSource。
- 缺 FrameSource 且需降级 → 错误文案 `Bitmap fallback is not available yet`。
- iPad 顶栏一颗 Export → 菜单 MP4 / PAG；Catalyst 仅 File 菜单。
- 与 `videoExportSession` / `pagExportSession` 互斥。
- **不自动 git commit。**

## File Map

| 文件 | 职责 |
|---|---|
| `bridge/include/motionstudio_bridge.h` | `MSPagExportOptions` + `ms_pag_export` |
| `bridge/src/apple/motionstudio_bridge_pag_export.mm` | 实现 |
| `bridge/CMakeLists.txt` | Apple 链 `pag_export` |
| `bridge/tests/PagExportBridgeTest.cpp` | null / empty path |
| `MotionDocumentCore.swift` | `exportPAG` |
| `Export/PagExportSession.swift` | 临时路径 + 调 core |
| `Export/PagExportSettingsViewController.swift` | 摘要 + switch |
| `Export/PagExportProgressViewController.swift` | 不确定进度 |
| `EditorViewController+PagExport.swift` | 编排 |
| `AppDelegate.swift` / `EditorViewController.swift` / `+Layout.swift` | 菜单、快捷键、顶栏 menu |
| `EditorViewController+Saving.swift` | documentPickerPurpose `.exportPAG`（若需要） |

---

### Task 1: Bridge `ms_pag_export` + 测试

- [x] 头文件声明 + `.mm` 实现 + CMake 链 `pag_export`
- [x] MappingFailed + allowBitmapFallback → 文案改写
- [x] `PagExportBridgeTest`：null document / empty path
- [x] `cmake --build build --target bridge_test` + 过滤跑通

### Task 2: App core 封装 + Session / Settings / Progress

- [x] `MotionDocumentCore.exportPAG`
- [x] Session / Settings / Progress UIKit 文件（对齐 Video* 命名与风格）

### Task 3: 编排 + 菜单 + iPad Export 菜单

- [x] `exportPAG()` 流程；与 MP4 session 互斥
- [x] File 菜单 + ⌥⌘P
- [x] iPad `exportButton.menu`：MP4 / PAG
- [x] `xcodebuild` Catalyst Debug **BUILD SUCCEEDED**

### Task 4: 手工验收清单（不自动 commit）

- [ ] 矢量文档导出 Share/Files
- [ ] FollowPath ± fallback 文案
