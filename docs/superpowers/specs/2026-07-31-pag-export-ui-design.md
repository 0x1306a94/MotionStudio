# PAG Export UI — 设计说明

日期：2026-07-31  
状态：已确认，待实现  
依赖：库层 `PagExporter` / `pag_export`（见 `2026-07-31-pag-export-design.md`）  
分支：`feature/export_pag`  
说明：**本里程碑 UI 变更不自动 git commit**（由用户显式提交）。

## 目标

在编辑器 App 中提供最小可用的 PAG 导出 UI，优先验证**矢量**通路：

1. File 菜单 **Export PAG…**（⌥⌘P）；**Export MP4…** 保持 ⌥⌘E
2. **iPad 顶栏导出按钮**：点击后选择导出方式（MP4 / PAG），再进入对应流程（不再直接进 MP4）
3. PAG 设置 sheet：只读合成摘要 + **Allow bitmap fallback**（默认关）
4. 后台导出 + 模态进度
5. 成功后：iPad Share sheet；Catalyst `UIDocumentPicker(forExporting:)`

## 非目标

- 生产用 `BitmapFrameSource` / 离屏光栅化（开关打开且触发降级时给出明确错误）
- iPad 顶栏增加第二颗按钮（仍是一颗 Export，用菜单/弹层选格式）
- Mac Catalyst 工具栏导出按钮（Catalyst 继续只用 File 菜单；顶栏导出按钮本就不在 Catalyst 布局）
- 多合成选择（用 `firstCompositionID`）
- 导出 warnings 列表 UI
- 自定义 `bitmapScale`、分辨率重采样 UI
- SwiftUI 重写编辑器壳

---

## §1 流程

### 1.1 iPad 顶栏入口

```
顶栏 Export 按钮
        │
        ▼
┌─────────────────────────────┐
│ 选择导出方式                  │  UIMenu 或 UIAlertController action sheet
│ · Export MP4…               │  → 现有 exportMP4() 流程
│ · Export PAG…               │  → exportPAG() 流程
└─────────────────────────────┘
```

推荐：`UIButton.menu` + `showsMenuAsPrimaryAction = true`（iPadOS 现代写法）；两项分别调用现有/新增 selector。导出 session 进行中时按钮 disabled（与菜单一致）。

### 1.2 PAG 导出（菜单或顶栏选 PAG 后相同）

```
Export PAG…
        │
        ▼
┌─────────────────────────────┐
│ Settings sheet              │  只读：尺寸 · 帧数 · fps
│ Allow bitmap fallback OFF   │  UISwitch，默认关
│ Export / Cancel             │
└──────────────┬──────────────┘
               │ Export
               ▼
┌─────────────────────────────┐
│ Progress sheet              │  写临时 .pag；不确定进度或简短状态文案
│ (modal, 不可下滑关)         │  Cancel：导出中尽量协作取消；同步 Encode 期间可能无法立即打断
└──────────────┬──────────────┘
               │ success
               ▼
┌─────────────────────────────┐
│ iPad: UIActivityViewController
│ Catalyst: UIDocumentPicker(forExporting:)
└──────────────┬──────────────┘
               │ done / cancel
               ▼
        删除临时目录
```

失败（非取消）：dismiss 进度 → alert（bridge 错误文案）。  
取消：静默关闭进度并清理临时文件（不弹失败 alert）。  
进入 Export 流程时暂停播放；结束后不自动恢复。

---

## §2 设置与 Options 映射

| UI | `MSPagExportOptions` / `PagExportOptions` |
|---|---|
| 输出路径 | session 生成的临时 `.pag` 绝对路径 |
| Allow bitmap fallback | `allowBitmapFallback`（默认 `false`） |
| （固定） | `bitmapScale = 1.0` |
| Composition | `firstCompositionID` |

**无 FrameSource：** Bridge 始终传 `frameSource = nullptr`。

| 场景 | 行为 |
|---|---|
| 纯矢量可映射 | Encode 成功 → 分享 |
| 单层 MappingFailed（含 FollowPath / 缺图等） | **跳过该层** + warning，其余层继续；整份导出仍成功 |
| 需降级 + fallback **开** 但无 FrameSource | 跳过该层；warning `BitmapFallbackUnavailable`（不中断整份导出） |
| 合成 `cornerRadius > 0` | 底层圆角矩形近似；warning `CompositionCornerRadiusApproximated` |

硬失败仍 alert：`InvalidComposition` / `InvalidOptions` / `EncodeFailed` / `WriteFailed`。

---

## §3 Bridge API（Apple）

```c
typedef struct MSPagExportOptions {
    const char *outputPath;   // required
    bool allowBitmapFallback;
    float bitmapScale;        // <=0 → treat as 1.0；UI 固定传 1.0
} MSPagExportOptions;

// Writes .pag via PagExporter. On failure *errorOut is malloc'd (ms_string_free).
bool ms_pag_export(MSDocument *document, uint64_t compositionId,
                   const MSPagExportOptions *options, char **errorOut);
```

- 实现文件：`bridge/src/apple/motionstudio_bridge_pag_export.mm`（或等价）
- CMake：Apple 下 `bridge` **PRIVATE** 链接 `pag_export`（进而 `pag_codec` / tgfx）；Ninja 与 Xcode 均需保证最终链接到 App / bridge 测试
- 单测：`bridge` 侧最小失败路径（null document / empty path）；矢量成功路径可放在已有 `pag_export_tests`，Bridge 冒烟可选

不暴露 progress / cancelFlag 首版（PAG Encode 通常很快）；UI Cancel 在 Task 开始前有效，开始后若无法打断则以完成/失败收尾。

---

## §4 App 结构

| 类型 | 文件（建议） | 职责 |
|---|---|---|
| 菜单 | `App/AppDelegate.swift` | `Export PAG…` → `#selector(EditorViewController.exportPAG)`，⌥⌘P |
| 快捷键 | `EditorViewController.keyCommands` | 同上；`canPerformAction` 在 session 进行中禁用 |
| iPad 顶栏 | `EditorViewController+Layout.swift`（或 Export 扩展） | `exportButton` 改为菜单：MP4 / PAG；不再 `addTarget → exportMP4` |
| 编排 | `Editor/EditorViewController+PagExport.swift` | settings → progress → share/picker；与 MP4 session 互斥 |
| 设置 | `Export/PagExportSettingsViewController.swift` | 摘要 + UISwitch + Export/Cancel |
| 进度 | `Export/PagExportProgressViewController.swift` | 状态文案 / 不确定进度；`isModalInPresentation = true` |
| 会话 | `Export/PagExportSession.swift` | 临时目录、调用 core 导出 |

`MotionDocumentCore` 增加：

```swift
nonisolated func exportPAG(compositionID: UInt64,
                           options: MSPagExportOptions) throws
```

内部调 `ms_pag_export`；错误 → `NSError`。

与 `videoExportSession` 互斥：任一导出进行中时，两个 Export 菜单项均 disabled。

---

## §5 临时文件与分享

- 路径：`temporaryDirectory / "MotionStudioExport-<UUID>" / "<ProjectName>.pag"`
- 项目名：draft → `"Untitled"`；否则 saveURL 去扩展名
- **iPad**：`UIActivityViewController`；完成后删临时目录
- **Catalyst**：`UIDocumentPickerViewController(forExporting:)`；done/cancel 都删

---

## §6 错误与边界

| 情况 | 行为 |
|---|---|
| 无 composition | alert，不进 settings 或 Export 禁用 |
| `duration <= 0` | 同上 |
| 矢量 MappingFailed | alert bridge 文案 |
| fallback 开但仍需 FrameSource | alert `Bitmap fallback is not available yet` |
| Encode / Write 失败 | alert |
| 用户取消（开始前） | 静默清理 |
| 重复 Export | session 非空则忽略 |
| 导出中预览 | 已暂停；进度模态挡住编辑 |

---

## §7 验收（矢量优先）

1. Shape / Text / Image / Group → 导出 `.pag`；背景色正确；有圆角时底层圆角矩形可见  
2. FollowPath + fallback 关/开（无 FrameSource）→ 仍能出文件，问题层被跳过  
3. Catalyst Files / iPad Share 拿到文件  

---

## 决策记录

| 项 | 选择 |
|---|---|
| UI 深度 | 设置 + 进度 + Share/Files（对齐 MP4 方案 B） |
| Bitmap 开关 | 有；默认关；无生产 FrameSource（方案 A） |
| 快捷键 | ⌥⌘P（MP4 仍为 ⌥⌘E） |
| iPad 顶栏 | 一颗 Export → 菜单选 MP4 / PAG |
| 自动 commit | **否** |
