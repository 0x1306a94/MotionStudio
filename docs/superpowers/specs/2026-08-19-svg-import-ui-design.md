# SVG Import UI — 设计说明

日期：2026-08-19  
状态：已确认  
依赖：库层 `svg_import` / `ImportSvgInto`（见 `2026-08-19-svg-import-design.md`）  
分支：`feature/svg_importer`

## 目标

把已落地的 SVG → 图层树导入接到编辑器：用户选一个 `.svg`，插入**当前合成**顶层，可编辑、可一次 undo。

对标现有**图片导入**（选文件 → 写入工程 → 记一条 undo），不是导出。

1. File 菜单 **Import SVG…**（⌥⌘S）
2. `UIDocumentPicker` 选一个 `.svg`（与导入图片相同；Catalyst 上由系统画成 Mac 打开面板）
3. Bridge 薄封装 `ImportSvgInto`；嵌入图写到 `{projectRoot}/assets/`
4. 导入后选中根 Group；有 warning 弹一次列表；解析失败才 Error

## 非目标

- PAG / MP4 导出、设置 sheet、进度、Share
- Project 面板按钮、拖放、最近文件、一次多选
- 改合成尺寸、新建 Document / Composition
- 挂到选中图层 / Group 下
- 相册选 SVG
- 升 schema、改 Core 模型

---

## §1 流程

```
File → Import SVG…（⌥⌘S）
        │
        ▼
UIDocumentPicker（.svg，单选，asCopy: true）
        │ 用户取消 → 无操作
        ▼
document.syncProjectRoot()
读文件字节
        │
        ▼
ms_document_import_svg（当前合成，顶层，parent=0）
        │
        ├─ 失败 → Alert「Import Failed」+ 错误串
        │
        └─ 成功
              选中 rootLayerId
              registerEdit("Import SVG")
              若 diagnostics 非空 → Alert「SVG Import Warnings」
```

时间轴停在当前帧。导出进行中时菜单项 disabled（与 Save / Export 一致）。

根图层名：文件名去扩展名；空则 `"SVG"`。

---

## §2 Bridge API（Apple）

`svg_import` 仅 Apple。声明放在 `motionstudio_bridge.h` 的 `#if defined(__APPLE__)` 段，实现 `bridge/src/apple/motionstudio_bridge_svg_import.mm`。

```c
typedef struct MSSvgImportOptions {
    int insertIndex;           // -1 = 追加到顶
    uint64_t parentLayerId;    // 0 = 合成根
    const char *rootName;      // NULL / 空 → "SVG"
} MSSvgImportOptions;

typedef struct MSSvgImportResult {
    uint64_t rootLayerId;
    int sourceWidth;
    int sourceHeight;
} MSSvgImportResult;

// 成功：true，*out 有效。
// 失败：false，*error 为 malloc 串（ms_string_free）；文档不变。
// diagnosticsJson：可选。无 warning 时保持 NULL；有则 JSON 数组，调用方 ms_string_free。
//   [{"code":"...","message":"...","nodeName":"..."}]
bool ms_document_import_svg(MSDocument *document,
                            uint64_t compositionId,
                            const void *bytes,
                            size_t length,
                            const MSSvgImportOptions *options,
                            MSSvgImportResult *out,
                            char **diagnosticsJson,
                            char **error);
```

内部：

1. `DocumentLock`；null document / null bytes / length 0 / 无合成 → 失败。
2. `ImportOptions` 从 C 结构填入，调 `ImportSvgInto(*document, *undoManager, …)`。
3. `Expected` 失败 → `*error`，不写盘。
4. 成功后把 `ImportResult.embeddedImages` 写到 `{projectRoot}/<Asset.path>`（`assets/<hexId>.png`）。为此给库的 `ImportResult` **只加这一字段**（`BuildSvgLayers` 行为不变）。`projectRoot` 空或写盘失败 → `undo` 刚插入的那条 `Import SVG`，返回失败。
5. 部分节点跳过仍算成功；diagnostic 进 JSON。

CMake：Apple 下 `bridge` 链接 `svg_import`（与现有 `pag_export` 一样是「Apple 专用静态库进 bridge」，不是导出功能）。  
App：`Base.xcconfig` 的 `CORE_LINK_FLAGS` 加 `-lmotionstudio_svg_import`（静态库不从 `bridge.a` 再导出符号）。

Swift 不直接 `#include` `SvgImporter.h`，不链 `svg_import`。

---

## §3 App 结构

| 类型 | 文件 | 职责 |
|---|---|---|
| 菜单 / 快捷键 | `AppDelegate.swift`、`EditorViewController.swift` | `Import SVG…` → `importSVG`，⌥⌘S |
| 编排 | `EditorViewController+SvgImport.swift` | 无 composition → 失败 Alert；否则交给 coordinator |
| 选择器 | `ProjectPanel/SvgImportCoordinator.swift` | `UIDocumentPicker` + 读字节 + 调 core |
| Core | `MotionDocumentCore.swift` | `importSvg` → `ms_document_import_svg` |

```swift
struct SvgImportDiagnostic {
    var code: String
    var message: String
    var nodeName: String
}

struct SvgImportOutcome {
    var rootLayerId: UInt64
    var sourceWidth: Int
    var sourceHeight: Int
    var diagnostics: [SvgImportDiagnostic]
}

func importSvg(compositionID: UInt64, data: Data, rootName: String?) throws -> SvgImportOutcome
```

`perform("Import SVG")` 包住 `importSvg` + `selectedLayerID = rootLayerId`，与加矩形层同一套 UIKit undo 注册。

`SvgImportCoordinator` 对齐 `ImageImportCoordinator` 的文件选择：`UIDocumentPickerViewController(forOpeningContentTypes:asCopy: true)`，单选。类型：`UTType.svg`（必要时 `UTType(filenameExtension: "svg")`）。不走相册，也不用 `NSOpenPanel` / AppKit。

---

## §4 嵌入图写盘

库只登记 `Asset`，字节在 `embeddedImages`。Bridge 负责落盘：

- 路径：`{projectRoot}/assets/<hexId>.png`（与 `Asset.path` 一致）
- 先 `create_directories(assets)`
- App 在调用前 `syncProjectRoot()`（含临时 draft）
- 写盘失败则 undo 整次导入，避免「图层在、文件不在」

无 `<image>` 的 SVG 不要求 `projectRoot` 非空（测试可直接导入）。

---

## §5 错误与边界

| 情况 | 行为 |
|---|---|
| 用户取消 picker | 无操作 |
| 无 composition | Alert「Import Failed」 |
| 读文件失败 | 同上 |
| 解析失败 / 空树 / 合成不存在 | Bridge 失败；Alert 显示错误串 |
| 嵌入图写盘失败 | undo + Alert |
| diagnostic（mask / filter / dash / 外链图等） | 导入成功 + 一次 Warning Alert，文案 `code: message`（有 nodeName 则附上） |
| 导出进行中 | 菜单 disabled |
| 重复点菜单 | 可再开 picker；不排队 |

---

## §6 验收

1. File → Import SVG… 能导入 `kitchen_sink.svg`：根 Group 在合成顶层，形状 / 渐变 / 文本可见，合成尺寸不变。
2. Undo 一次整棵消失；Redo 恢复。
3. 含 data URI `<image>` 的 SVG：`assets/<hexId>.png` 落盘，重开工程图还在。
4. 含 `mask` / 外链图等：图层进来，弹 Warning，不是 Error。
5. 坏 XML：Alert，文档不变。
6. Catalyst 与 iPad 都能用 `UIDocumentPicker` 选 `.svg`（Catalyst 为系统 Mac 打开面板）。

---

## 决策记录

| 项 | 选择 |
|---|---|
| 产品对标 | 图片导入（插入 + undo + `UIDocumentPicker`），不是 PAG/MP4 导出 |
| 选文件 | 两端 `UIDocumentPicker`（与导入图片相同；不用 `NSOpenPanel`） |
| 入口 | 仅 File 菜单 + ⌥⌘S |
| 插入 | 当前合成顶层，不挂选中层下 |
| 导入后 | 选中根 Group；时间轴不动 |
| diagnostic | 成功仍弹一次 Warning |
| 嵌入图 | Bridge 写 `projectRoot/assets/` |
| 多文件 / 拖放 | 不做 |
| 合成尺寸 | 不改 |
