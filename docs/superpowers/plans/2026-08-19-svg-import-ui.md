# SVG Import UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** File 菜单 Import SVG… 把 `.svg` 插入当前合成顶层；Bridge 转调 `ImportSvgInto` 并写嵌入图。

**Architecture:** 对标图片导入。Swift 只调 C ABI。两端用 `UIDocumentPicker`（Catalyst 上为系统 Mac 打开面板）。`ms_document_import_svg` 调已有 `ImportSvgInto`，再把 `embeddedImages` 写到 `{projectRoot}/assets/`。失败则 undo，文档不变。

**Tech Stack:** C bridge、`svg_import`、UIKit `UIDocumentPicker`、`MotionDocumentCore`。

**Spec:** [`docs/superpowers/specs/2026-08-19-svg-import-ui-design.md`](../specs/2026-08-19-svg-import-ui-design.md)

## Global Constraints

- 不改 Core 模型 / schema / `svg_import` 公开 API。
- Swift 不 `#include` `SvgImporter.h`，不直接链 `svg_import`。
- 插入当前合成顶层；不改合成尺寸。
- 禁止 `dynamic_cast`、C++ 异常、lambda；`if`/`switch` 分支必须 `{}`。
- 代码与注释英语；commit 120 字符内英语、句号结尾。
- 仅 Apple：实现放 `bridge/src/apple/`，声明放 header 的 `#if defined(__APPLE__)`。

## File Map

| 文件 | 职责 |
|---|---|
| `bridge/include/motionstudio_bridge.h` | `MSSvgImportOptions` / `MSSvgImportResult` / `ms_document_import_svg` |
| `bridge/src/apple/motionstudio_bridge_svg_import.mm` | 转调 + 写盘 + diagnostic JSON |
| `bridge/CMakeLists.txt` | Apple 链 `svg_import` |
| `bridge/tests/SvgImportBridgeTest.cpp` | null / 坏 XML / kitchen sink / data URI 写盘 / undo |
| `apps/MotionStudioApp/Configurations/Base.xcconfig` | `-lmotionstudio_svg_import` |
| `MotionDocumentCore.swift` | `importSvg` |
| `ProjectPanel/SvgImportCoordinator.swift` | `UIDocumentPicker` |
| `Editor/EditorViewController+SvgImport.swift` | 编排 + Alert |
| `AppDelegate.swift` / `EditorViewController.swift` | 菜单、⌥⌘S、`canPerformAction` |
| `docs/README.md` | 索引链到 UI spec |

---

### Task 1: Bridge `ms_document_import_svg` + 测试

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（`#endif` before `ms_canvas_destroy` 之前）
- Create: `bridge/src/apple/motionstudio_bridge_svg_import.mm`
- Modify: `bridge/CMakeLists.txt`
- Create: `bridge/tests/SvgImportBridgeTest.cpp`
- Modify: `apps/MotionStudioApp/Configurations/Base.xcconfig`

**Interfaces:**
- Consumes: `motion::svg::ImportSvgInto` / `ImportOptions` / `ImportResult` / `EmbeddedImage`
- Produces: `ms_document_import_svg`（签名见 Step 1）

- [x] **Step 1: 写失败测试（API 尚不存在）**

Create `bridge/tests/SvgImportBridgeTest.cpp`:

```cpp
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "motionstudio_bridge.h"

#if defined(__APPLE__)

TEST(SvgImportBridgeTest, NullDocumentFails) {
    char *error = nullptr;
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = nullptr;
    MSSvgImportResult out{};
    const char svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>";
    EXPECT_FALSE(ms_document_import_svg(nullptr, 0, svg, sizeof(svg) - 1, &options, &out, nullptr,
                                        &error));
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("document"), std::string::npos);
    ms_string_free(error);
}

TEST(SvgImportBridgeTest, InvalidXmlFails) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    char *error = nullptr;
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = "SVG";
    MSSvgImportResult out{};
    const char svg[] = "not-svg";
    EXPECT_FALSE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options,
                                        &out, nullptr, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 0);
    ms_string_free(error);
    ms_document_destroy(document);
}

#endif
```

- [x] **Step 2: 确认测试编不过或失败**

Run:

```bash
cmake --build build --target bridge_test
```

Expected: compile error，`ms_document_import_svg` / `MSSvgImportOptions` undeclared.

- [x] **Step 3: 声明 + 实现 + 链接**

In `bridge/include/motionstudio_bridge.h`，`ms_pag_export` 声明之后、`#endif`（Apple 段结束）之前插入：

```c
/* ============================ svg import (Apple platforms) ============================ */

typedef struct MSSvgImportOptions {
    int insertIndex;
    uint64_t parentLayerId;
    const char *rootName;
} MSSvgImportOptions;

typedef struct MSSvgImportResult {
    uint64_t rootLayerId;
    int sourceWidth;
    int sourceHeight;
} MSSvgImportResult;

bool ms_document_import_svg(MSDocument *document, uint64_t compositionId, const void *bytes,
                            size_t length, const MSSvgImportOptions *options,
                            MSSvgImportResult *out, char **diagnosticsJson, char **error);
```

Create `bridge/src/apple/motionstudio_bridge_svg_import.mm`。要点（完整实现按此结构写，禁止 lambda / `dynamic_cast` / 异常）：

```cpp
#include "motionstudio_bridge.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "MotionStudio/import/svg/SvgImporter.h"
#include "common/DocumentLock.h"
#include "common/MSDocument.h"

namespace {

void SetError(char **error, const std::string &message) {
    if (error == nullptr) {
        return;
    }
    *error = strdup(message.c_str());
}

std::string JsonEscape(const std::string &input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
            out.push_back(ch);
        } else if (ch == '\n') {
            out += "\\n";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

char *DiagnosticsToJson(const std::vector<motion::svg::Diagnostic> &diagnostics) {
    if (diagnostics.empty()) {
        return nullptr;
    }
    std::string json = "[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"code\":\"";
        json += JsonEscape(diagnostics[i].code);
        json += "\",\"message\":\"";
        json += JsonEscape(diagnostics[i].message);
        json += "\",\"nodeName\":\"";
        json += JsonEscape(diagnostics[i].nodeName);
        json += "\"}";
    }
    json += "]";
    return strdup(json.c_str());
}

bool WriteEmbeddedImages(const std::string &projectRoot,
                         const std::vector<motion::svg::EmbeddedImage> &images,
                         std::string &error) {
    if (images.empty()) {
        return true;
    }
    if (projectRoot.empty()) {
        error = "project root is empty";
        return false;
    }
    const std::filesystem::path root(projectRoot);
    std::error_code fsError;
    std::filesystem::create_directories(root / "assets", fsError);
    if (fsError) {
        error = "failed to create assets directory";
        return false;
    }
    for (const motion::svg::EmbeddedImage &image : images) {
        const std::filesystem::path destination = root / image.suggestedFileName;
        std::filesystem::create_directories(destination.parent_path(), fsError);
        std::ofstream output(destination, std::ios::binary);
        if (!output) {
            error = "failed to write embedded image";
            return false;
        }
        output.write(reinterpret_cast<const char *>(image.bytes.data()),
                     static_cast<std::streamsize>(image.bytes.size()));
        if (!output) {
            error = "failed to write embedded image";
            return false;
        }
    }
    return true;
}

}  // namespace

bool ms_document_import_svg(MSDocument *document, uint64_t compositionId, const void *bytes,
                            size_t length, const MSSvgImportOptions *options,
                            MSSvgImportResult *out, char **diagnosticsJson, char **error) {
    DocumentLock lock(document);
    if (document == nullptr) {
        SetError(error, "document is null");
        return false;
    }
    if (bytes == nullptr || length == 0) {
        SetError(error, "svg bytes are empty");
        return false;
    }
    if (out == nullptr) {
        SetError(error, "result is null");
        return false;
    }
    motion::svg::ImportOptions importOptions;
    if (options != nullptr) {
        importOptions.insertIndex = options->insertIndex;
        if (options->parentLayerId != 0) {
            importOptions.parentLayerId = motion::EntityId{options->parentLayerId};
        }
        if (options->rootName != nullptr && options->rootName[0] != '\0') {
            importOptions.rootName = options->rootName;
        }
    }
    auto imported = motion::svg::ImportSvgInto(*document->document, *document->undoManager,
                                               motion::EntityId{compositionId}, bytes, length,
                                               importOptions);
    if (!imported.hasValue()) {
        SetError(error, imported.error());
        return false;
    }
    std::string writeError;
    if (!WriteEmbeddedImages(document->document->projectRoot, imported.value().embeddedImages,
                             writeError)) {
        document->undoManager->undo(*document->document);
        SetError(error, writeError);
        return false;
    }
    out->rootLayerId = imported.value().rootLayerId.value;
    out->sourceWidth = imported.value().sourceWidth;
    out->sourceHeight = imported.value().sourceHeight;
    if (diagnosticsJson != nullptr) {
        *diagnosticsJson = DiagnosticsToJson(imported.value().diagnostics);
    }
    document->previewSceneCache.clear();
    return true;
}
```

注意：`ImportSvgInto` 已经把图层写进 undo。写盘失败必须 `undo` 那一条。`embeddedImages` 在 `ImportSvgInto` 返回后仍在 `ImportResult` 里——**当前 `ImportResult` 没有 `embeddedImages` 字段**。

这里必须停一下：公开 `ImportResult` 只有 `rootLayerId` / `layerIds` / `diagnostics` / 尺寸。字节在 `SvgLayerTree.embeddedImages`，`ImportSvgInto` 用完 tree 后丢掉了。

**本 Task 允许的唯一库改动（不扩公开导入语义）：** 给 `ImportResult` 增加 `std::vector<EmbeddedImage> embeddedImages;`，在 `ImportSvgInto` 里 `out.embeddedImages = std::move(tree.embeddedImages);`。不改 `BuildSvgLayers` 行为。现有库测无需改断言。

`bridge/CMakeLists.txt` Apple 段：

```cmake
list(APPEND BRIDGE_LINK_LIBRARIES motionstudio_tgfx_adapter motionstudio_avf_adapter pag_export svg_import)
```

`Base.xcconfig` 的 `CORE_LINK_FLAGS` 在 `-lmotionstudio_bridge` 后加 `-lmotionstudio_svg_import`。

- [x] **Step 4: 补成功路径测试**

同一测试文件追加（kitchen sink 相对 `PROJECT_SOURCE_DIR` 不可用时，用内联小 SVG）：

```cpp
TEST(SvgImportBridgeTest, ImportsRectAndUndo) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = "SVG";
    MSSvgImportResult out{};
    char *diagnostics = nullptr;
    char *error = nullptr;
    const char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"10\" fill=\"#f00\"/>"
        "</svg>";
    ASSERT_TRUE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options, &out,
                                       &diagnostics, &error))
        << (error != nullptr ? error : "unknown");
    EXPECT_EQ(error, nullptr);
    EXPECT_NE(out.rootLayerId, 0u);
    EXPECT_EQ(out.sourceWidth, 20);
    EXPECT_EQ(out.sourceHeight, 10);
    EXPECT_EQ(ms_layer_type(document, out.rootLayerId), MS_LAYER_GROUP);
    EXPECT_GE(ms_composition_layer_count(document, compositionId), 1);
    if (diagnostics != nullptr) {
        ms_string_free(diagnostics);
    }
    ASSERT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 0);
    ms_document_destroy(document);
}

TEST(SvgImportBridgeTest, WritesDataUriImageUnderProjectRoot) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const auto root = std::filesystem::temp_directory_path() /
                      ("ms_svg_bridge_" + std::to_string(reinterpret_cast<uintptr_t>(document)));
    std::filesystem::create_directories(root / "assets");
    ms_document_set_project_root(document, root.string().c_str());

    const char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"data:image/png;base64,"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\""
        " width=\"16\" height=\"16\"/>"
        "</svg>";
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = "SVG";
    MSSvgImportResult out{};
    char *error = nullptr;
    ASSERT_TRUE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options, &out,
                                       nullptr, &error))
        << (error != nullptr ? error : "unknown");
    EXPECT_EQ(ms_document_asset_count(document), 1);
    const uint64_t assetId = ms_document_asset_id_at(document, 0);
    char *path = ms_asset_path(document, assetId);
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(std::filesystem::exists(root / path));
    ms_string_free(path);
    std::error_code removeError;
    std::filesystem::remove_all(root, removeError);
    ms_document_destroy(document);
}

TEST(SvgImportBridgeTest, ExternalImageReturnsDiagnostics) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"photo.png\" width=\"16\" height=\"16\"/>"
        "</svg>";
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = nullptr;
    MSSvgImportResult out{};
    char *diagnostics = nullptr;
    char *error = nullptr;
    ASSERT_TRUE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options, &out,
                                       &diagnostics, &error));
    ASSERT_NE(diagnostics, nullptr);
    EXPECT_NE(std::string(diagnostics).find("image.external"), std::string::npos);
    ms_string_free(diagnostics);
    ms_document_destroy(document);
}
```

若 `ms_document_asset_id_at` 不存在，用已有 `ms_document_asset_count` + 遍历 `ms_asset_path` 的现有 API（先搜 header；不要新造 asset 枚举 API）。

- [x] **Step 5: 跑测试**

```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='SvgImportBridgeTest.*'
```

Expected: 全部 PASS。

- [x] **Step 6: Commit**

```bash
git add bridge/include/motionstudio_bridge.h \
        bridge/src/apple/motionstudio_bridge_svg_import.mm \
        bridge/CMakeLists.txt \
        bridge/tests/SvgImportBridgeTest.cpp \
        include/MotionStudio/import/svg/SvgImporter.h \
        src/import/svg/SvgImporter.cpp \
        apps/MotionStudioApp/Configurations/Base.xcconfig \
        docs/superpowers/plans/2026-08-19-svg-import-ui.md
git commit --only bridge/include/motionstudio_bridge.h \
  bridge/src/apple/motionstudio_bridge_svg_import.mm \
  bridge/CMakeLists.txt \
  bridge/tests/SvgImportBridgeTest.cpp \
  include/MotionStudio/import/svg/SvgImporter.h \
  src/import/svg/SvgImporter.cpp \
  apps/MotionStudioApp/Configurations/Base.xcconfig \
  docs/superpowers/plans/2026-08-19-svg-import-ui.md \
  -m "Add SVG import C ABI that writes embedded images."
```

勾选本 Task 所有 checkbox，Task 标 **Status:** ✅ Done，随 commit 一起提交。

---

### Task 2: App 菜单 + 选文件

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Create: `apps/MotionStudioApp/MotionStudioApp/ProjectPanel/SvgImportCoordinator.swift`
- Create: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+SvgImport.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/App/AppDelegate.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift`（若 `imageImportCoordinator` 旁需要持有 coordinator）

**Interfaces:**
- Consumes: `ms_document_import_svg`
- Produces: `MotionDocumentCore.importSvg`、`EditorViewController.importSVG`

- [x] **Step 1: `MotionDocumentCore.importSvg`**

放在 `importImageAsset` 附近：

```swift
struct SvgImportDiagnostic: Sendable {
    var code: String
    var message: String
    var nodeName: String
}

struct SvgImportOutcome: Sendable {
    var rootLayerId: UInt64
    var sourceWidth: Int
    var sourceHeight: Int
    var diagnostics: [SvgImportDiagnostic]
}

enum SvgImportError: Error {
    case failed(String)
}

func importSvg(compositionID: UInt64, data: Data, rootName: String?) throws -> SvgImportOutcome {
    var options = MSSvgImportOptions()
    options.insertIndex = -1
    options.parentLayerId = 0
    let outcome: SvgImportOutcome = try data.withUnsafeBytes { rawBuffer in
        guard let base = rawBuffer.baseAddress, !rawBuffer.isEmpty else {
            throw SvgImportError.failed("svg bytes are empty")
        }
        return try withRootName(rootName) { namePointer in
            options.rootName = namePointer
            var result = MSSvgImportResult()
            var diagnosticsPtr: UnsafeMutablePointer<CChar>?
            var errorPtr: UnsafeMutablePointer<CChar>?
            let ok = ms_document_import_svg(handle, compositionID, base, rawBuffer.count, &options,
                                            &result, &diagnosticsPtr, &errorPtr)
            if !ok {
                throw SvgImportError.failed(Self.takeString(errorPtr) ?? "import failed")
            }
            let diagnosticsJson = Self.takeString(diagnosticsPtr)
            changed()
            return SvgImportOutcome(rootLayerId: result.rootLayerId,
                                    sourceWidth: Int(result.sourceWidth),
                                    sourceHeight: Int(result.sourceHeight),
                                    diagnostics: Self.parseSvgDiagnostics(diagnosticsJson))
        }
    }
    return outcome
}
```

`withRootName` / `parseSvgDiagnostics` 用文件内 `private` 方法实现：JSON 只解析 `code` / `message` / `nodeName` 字符串字段。不要引入新 Swift 依赖；手写扫描或 `JSONSerialization`。

- [x] **Step 2: Coordinator + 编排**

`SvgImportCoordinator.swift` 对齐 `ImageImportCoordinator` 的文件选择（两端同一套 `UIDocumentPicker`，不用 `NSOpenPanel`）：

```swift
import UIKit
import UniformTypeIdentifiers

@MainActor
final class SvgImportCoordinator: NSObject {
    private weak var presenter: UIViewController?
    private let document: MotionProjectDocument
    private let perform: (String, () -> Void) -> Void
    var onImported: ((SvgImportOutcome) -> Void)?
    var onFailed: ((String) -> Void)?

    func presentImport() {
        let types: [UTType] = {
            if let svg = UTType(filenameExtension: "svg") {
                return [svg]
            }
            return [.xml]
        }()
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: true)
        picker.delegate = self
        picker.allowsMultipleSelection = false
        presenter?.present(picker, animated: true)
    }
}
```

`didPickDocumentsAt`：`startAccessingSecurityScopedResource` → `Data(contentsOf:)` → `document.syncProjectRoot()` → `perform("Import SVG") { outcome = try document.core.importSvg(...) }` → `onImported`。读文件或 import 失败走 `onFailed`。

`EditorViewController+SvgImport.swift`：

```swift
@objc func importSVG() {
    guard !isExportInProgress else { return }
    if document.core.firstCompositionID == 0 {
        presentImportAlert(title: "Import Failed", message: "No composition to import into.")
        return
    }
    // present coordinator
}
```

成功：`editorState.selectedLayerID = outcome.rootLayerId`。  
`diagnostics` 非空：`presentImportAlert(title: "SVG Import Warnings", message:)`，每行 `code: message`，有 `nodeName` 则 `code: message (name)`。

- [x] **Step 3: 菜单与快捷键**

`AppDelegate.buildMenu` 的 `saveMenu` 在 Save As 与 Export 之间插入：

```swift
let importSVGCommand = UIKeyCommand(title: "Import SVG...",
                                    image: nil,
                                    action: #selector(EditorViewController.importSVG),
                                    input: "s",
                                    modifierFlags: [.command, .alternate])
```

`keyCommands` 同样加一条。`canPerformAction` 把 `#selector(importSVG)` 与 export 一样：`!isExportInProgress`。

`EditorViewController` 增加 `svgImportCoordinator` 属性（与 `imageImportCoordinator` 并列）。

- [x] **Step 4: 编译 App**

优先 Xcode MCP `BuildProject`（已打开 `MotionStudio.xcworkspace`）。不可用则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug \
  -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64"
```

Expected: **BUILD SUCCEEDED**。链接错误缺 `motionstudio_svg_import` 时先跑 `apps/gen_mac` 再编。

- [x] **Step 5: Commit**

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  apps/MotionStudioApp/MotionStudioApp/ProjectPanel/SvgImportCoordinator.swift \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+SvgImport.swift \
  apps/MotionStudioApp/MotionStudioApp/App/AppDelegate.swift \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift \
  docs/superpowers/plans/2026-08-19-svg-import-ui.md \
  -m "Add File menu Import SVG using the document picker."
```

---

### Task 3: 文档索引 + 手工验收

**Status:** Pending

**Files:**
- Modify: `docs/README.md`
- Modify: `docs/architecture.md`（bridge 行补一句 SVG import C ABI，若目录树仍写旧 canvas 路径则顺手改那一行）

**Interfaces:**
- Consumes: Task 1–2 已实现行为
- Produces: 文档链接

- [ ] **Step 1: 索引**

`docs/README.md` 表格在 SVG 导入库 spec 下加一行：

`[superpowers/specs/2026-08-19-svg-import-ui-design.md](superpowers/specs/2026-08-19-svg-import-ui-design.md)` — File 菜单导入 SVG。

- [ ] **Step 2: 手工点一遍（实现者做，结果写进 PR / 回复）**

1. 导入 `tests/import/svg/fixtures/kitchen_sink.svg`：顶层 Group，尺寸不变。
2. Undo / Redo 整棵。
3. 导入带 data URI `<image>` 的 SVG，确认 `assets/*.png` 存在。
4. 导入带外链 `<image href="photo.png">`：成功 + Warning。
5. 选坏文件：Alert，图层数不变。

- [ ] **Step 3: Commit**

```bash
git commit --only docs/README.md docs/architecture.md docs/superpowers/plans/2026-08-19-svg-import-ui.md \
  -m "Document the SVG import File menu."
```

---

## Self-review

1. **Spec coverage:** 菜单、picker、Bridge 写盘、warning Alert、不改尺寸、单文件、选中根 Group — Task 1–2。验收 — Task 3。
2. **Placeholders:** 无 TBD。`ImportResult.embeddedImages` 是实现写盘的必要字段，已写明。
3. **Types:** `ms_document_import_svg` / `MSSvgImportOptions` / `SvgImportOutcome` 在 Task 1–2 一致。
