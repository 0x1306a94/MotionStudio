# Tgfx BitmapFrameSource Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 生产级 `TgfxBitmapFrameSource`（Metal Surface + `readPixels`）并在 `ms_pag_export` 自动注入，使带 `_bmp` 的 PAG 导出走真实渲染。

**Architecture:** 复用 `TgfxRenderAdapter`（已有 `Surface::MakeFrom` + `ReadPixels`）作为离屏画布；`Evaluate` → 可选过滤 `SceneState.layers` → `BuildCommands` → `PlayCommands`；Bridge 检测 `_bmp` 后栈上创建 source 注入 `PagExporter::Export`。

**Tech Stack:** C++17、ObjC++、tgfx Metal、GoogleTest、现有 `pag_export` / bridge。

**Spec:** `docs/superpowers/specs/2026-08-10-tgfx-bitmap-frame-source-design.md`

## Global Constraints

- 层 `_bmp`：只渲目标层 + Document 子树；宿主尺寸；透明底；`cornerRadius=0`。
- 合成 `_bmp`：整合成；合成背景色；不过滤层；`cornerRadius=0`。
- 读回：预乘 RGBA8；不走 CVPixelBuffer。
- Bridge 自动注入；`allowBitmapExport==false` 不创建 source。
- `HasBmpSuffix` 上移到 `include/MotionStudio/export/PagBmpSuffix.h` 供 Core/Bridge 共用。
- Commit：120 字符内英语、句号结尾；每 Task 同步本 plan checkbox。
- 禁止改 `SceneEvaluator` 增加子集 API；过滤在 `SceneState.layers` 上完成。

## File Map

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/export/PagBmpSuffix.h` | 公共 `HasBmpSuffix`（namespace `motion`） |
| `src/export/pag/PagBmpSuffix.h` | 改为 include 公共头或删除并改引用 |
| `adapter/tgfx/include/TgfxBitmapFrameSource.h` | 类声明 |
| `adapter/tgfx/src/TgfxBitmapFrameSource.mm` | 实现 |
| `adapter/tgfx/tests/TgfxBitmapFrameSourceTest.mm` | Metal 烟测 / 层隔离 |
| `bridge/src/apple/motionstudio_bridge_pag_export.mm` | 自动注入 |
| `bridge/tests/PagExportBridgeTest.cpp` | `_bmp` 集成（可选扩） |

---

### Task 1: 上移 `HasBmpSuffix`

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/export/PagBmpSuffix.h`
- Modify: `src/export/pag/PagBmpSuffix.h`（转发 include）或删除并改 `#include`
- Modify: `src/export/pag/PagFileBuilder.cpp`、`tests/export/pag/PagBmpSuffixTest.cpp`

**Interfaces:**
- Produces:
```cpp
namespace motion {
inline bool HasBmpSuffix(std::string_view name);
}
```
- 旧 `motion::pag_export::HasBmpSuffix`：若保留，在 `src/export/pag/PagBmpSuffix.h` 内 `using` 或 inline 转调 `motion::HasBmpSuffix`，避免大范围改名；**推荐**直接改调用点为 `motion::HasBmpSuffix`。

- [x] **Step 1: 写/改测试 include 公共头**

`PagBmpSuffixTest.cpp`：
```cpp
#include "MotionStudio/export/PagBmpSuffix.h"
using motion::HasBmpSuffix;
// 原断言不变
```

- [x] **Step 2: 跑测确认仍绿（或 include 路径失败）**

Run: `./build/src/export/pag/pag_export_tests --gtest_filter='PagBmpSuffixTest.*'`  
Expected: PASS（改完后）或编译失败（若尚未创建公共头）

- [x] **Step 3: 创建公共头并更新引用**

`include/MotionStudio/export/PagBmpSuffix.h`：复制现有实现，放在 `namespace motion`（去掉 `pag_export`）。  
`PagFileBuilder.cpp`：`#include "MotionStudio/export/PagBmpSuffix.h"`，调用 `HasBmpSuffix`。  
删除或改写 `src/export/pag/PagBmpSuffix.h` 为：
```cpp
#pragma once
#include "MotionStudio/export/PagBmpSuffix.h"
```

- [x] **Step 4: 跑测确认通过**

Run: `./build/src/export/pag/pag_export_tests --gtest_filter='PagBmpSuffixTest.*'`  
Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/export/PagBmpSuffix.h \
  src/export/pag/PagBmpSuffix.h \
  src/export/pag/PagFileBuilder.cpp \
  tests/export/pag/PagBmpSuffixTest.cpp \
  docs/superpowers/plans/2026-08-10-tgfx-bitmap-frame-source.md \
  -m "Move HasBmpSuffix into public MotionStudio export headers."
```

---

### Task 2: `TgfxBitmapFrameSource` composition 模式

**Status:** ✅ Done

**Files:**
- Create: `adapter/tgfx/include/TgfxBitmapFrameSource.h`
- Create: `adapter/tgfx/src/TgfxBitmapFrameSource.mm`
- Create: `adapter/tgfx/tests/TgfxBitmapFrameSourceTest.mm`
- CMake：`add_files_by_extension` 已扫目录，通常无需改；确认 `.mm` 被收录

**Interfaces:**
- Consumes: `TgfxRenderAdapter::Make` / `ReadPixels`；`SceneEvaluator::Evaluate`；`BuildCommands`；`PlayCommands`
- Produces: `TgfxBitmapFrameSource` 完整虚函数实现

实现要点（composition）：

```cpp
// prepareComposition:
finish();
impl_->mode = Composition;
impl_->document = &document;
impl_->compositionId = compositionId;
impl_->visibleRange = visibleRange;
impl_->bitmapScale = bitmapScale;
// resolve composition size → pixelW/H = lround(w*scale), lround(h*scale)
impl_->adapter = TgfxRenderAdapter::Make(pixelW, pixelH);
if (!impl_->adapter) return Unexpected("Metal unavailable for bitmap frame source");

// renderFrame(time):
state = Evaluate(*doc, compositionId, time);
state->cornerRadius = 0;
adapter->beginFrame(w, h, state->backgroundColor, 0);
adapter->setColorSourceFrameContext(...);
PlayCommands(BuildCommands(*state), *adapter);
adapter->endFrame();
adapter->ReadPixels(impl_->pixels);
return BitmapFrame{ w, h, pixels.data(), rowBytes, true };
```

- [x] **Step 1: 写失败测试**

```cpp
TEST(TgfxBitmapFrameSourceTest, CompositionRendersNonEmptyPixels) {
    Document document;
    // 100x80 composition, red rect full size, duration 1
    // prepareComposition(..., scale=1)
    // renderFrame(0) → width==100, height==80, some pixel RGB near red
}
```

- [x] **Step 2: 跑测确认失败**

Run: `./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxBitmapFrameSourceTest.*'`  
Expected: 编译失败或链接失败

- [x] **Step 3: 实现 composition 路径**

`prepare` / 层过滤可先 stub：`prepare` 返回 `"not implemented"` 或同样走 composition 但下一 Task 补过滤——**本 Task 必须实现 `prepareComposition` + `finish`；`prepare` 可返回错误字符串 `layer mode not implemented` 仅当测试未覆盖，推荐本 Task 一并搭好 `prepare` 骨架存 ids。**

- [x] **Step 4: 跑测确认通过**

Run: `./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxBitmapFrameSourceTest.Composition*'`  
Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only adapter/tgfx/include/TgfxBitmapFrameSource.h \
  adapter/tgfx/src/TgfxBitmapFrameSource.mm \
  adapter/tgfx/tests/TgfxBitmapFrameSourceTest.mm \
  docs/superpowers/plans/2026-08-10-tgfx-bitmap-frame-source.md \
  -m "Add TgfxBitmapFrameSource composition offscreen readback for PAG export."
```

---

### Task 3: 层模式过滤 + 隔离测试

**Status:** ✅ Done

**Files:**
- Modify: `adapter/tgfx/src/TgfxBitmapFrameSource.mm`
- Modify: `adapter/tgfx/tests/TgfxBitmapFrameSourceTest.mm`

**Interfaces:**
- Produces: `prepare(host, rootLayerId, …)` 收集子树 id；`renderFrame` 过滤 `state.layers`

```cpp
void CollectSubtreeIds(const Composition &host, EntityId root, std::unordered_set<uint64_t> *ids) {
    ids->insert(root.value);
    bool added = true;
    while (added) {
        added = false;
        for (const auto &layer : host.layers) {
            if (!layer || ids->count(layer->id.value)) continue;
            if (layer->parentId.isValid() && ids->count(layer->parentId.value)) {
                ids->insert(layer->id.value);
                added = true;
            }
        }
    }
}
// renderFrame layer mode:
auto filtered = *state;
filtered.layers.erase(
  remove_if(..., id not in ids), filtered.layers.end());
filtered.cornerRadius = 0;
beginFrame(..., Color{0,0,0,0}, 0);
```

- [x] **Step 1: 写失败测试**

```cpp
TEST(TgfxBitmapFrameSourceTest, LayerPrepareExcludesSiblingColor) {
    // host 100x100: LayerA red rect left, LayerB blue rect right
    // prepare(host, LayerA.id, [0,1), 1.0)
    // sample right-side pixel → not blue-dominant
    // sample left-side → red-dominant
}
```

- [x] **Step 2: 跑测确认失败**

Run: `./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxBitmapFrameSourceTest.Layer*'`  
Expected: FAIL（若 prepare 未实现过滤）

- [x] **Step 3: 实现过滤**

- [x] **Step 4: 跑测确认通过**

Run: `./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxBitmapFrameSourceTest.*'`  
Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only adapter/tgfx/src/TgfxBitmapFrameSource.mm \
  adapter/tgfx/tests/TgfxBitmapFrameSourceTest.mm \
  docs/superpowers/plans/2026-08-10-tgfx-bitmap-frame-source.md \
  -m "Filter SceneState layers for TgfxBitmapFrameSource layer _bmp mode."
```

---

### Task 4: Bridge 自动注入

**Status:** 待开始

**Files:**
- Modify: `bridge/src/apple/motionstudio_bridge_pag_export.mm`
- Modify: `bridge/tests/PagExportBridgeTest.cpp`（扩 `_bmp` 用例；需临时文件）
- 可能 Modify: bridge 链接已含 `tgfx_adapter`（确认）

**Interfaces:**
- Consumes: `HasBmpSuffix`、`TgfxBitmapFrameSource`、`PagExporter::Export`

```cpp
bool ExportTreeHasBmpSuffix(const Document &document, EntityId rootCompositionId) {
    // DFS / 与 compositionOrder 相同：合成名或任一层名 HasBmpSuffix
}

// ms_pag_export:
std::unique_ptr<TgfxBitmapFrameSource> bitmapSource;
if (exportOptions.allowBitmapExport &&
    ExportTreeHasBmpSuffix(*document->document, exportOptions.compositionId)) {
    bitmapSource = std::make_unique<TgfxBitmapFrameSource>();
}
auto result = PagExporter::Export(..., bitmapSource.get());
```

- [ ] **Step 1: 写 Bridge 测试**

```cpp
TEST(PagExportBridgeTest, CompositionBmpExportsWithRealFrameSource) {
    // create doc via bridge API or C++ Document through test helper
    // set composition name "*_bmp", add shape, allowBitmapExport=true, temp path
    // EXPECT_TRUE(ms_pag_export(...))
    // Load bytes / file size > 0
}
```

若 bridge 测试难建完整 Document，可改为：仅断言「有 `_bmp` 时不再因缺 FrameSource 失败」（需能从 bridge 建层——查现有 bridge_test 模式）。

- [ ] **Step 2: 跑测确认失败**

Run: `./build/bridge/bridge_test --gtest_filter='PagExportBridgeTest.*'`  
Expected: FAIL 或导出 MappingFailed（缺 FrameSource）

- [ ] **Step 3: 实现自动注入**

- [ ] **Step 4: 跑测确认通过**

Run: `ctest --test-dir build -R 'TgfxBitmapFrameSource|PagExportBridge|PagBmpSuffix|PagExporter' --output-on-failure`  
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git commit --only bridge/src/apple/motionstudio_bridge_pag_export.mm \
  bridge/tests/PagExportBridgeTest.cpp \
  docs/superpowers/plans/2026-08-10-tgfx-bitmap-frame-source.md \
  -m "Auto-inject TgfxBitmapFrameSource when PAG export tree uses _bmp."
```

---

## Spec coverage（自检）

| Spec | Task |
|---|---|
| HasBmpSuffix 公共头 | Task 1 |
| Composition Surface + readPixels | Task 2 |
| Layer 过滤 + 透明底 | Task 3 |
| Bridge 自动注入 | Task 4 |
| 烟测 / 隔离测 | Task 2–3 |
| 不改 SceneEvaluator API | 全计划 |

## Placeholder scan

无 TBD；复用 `TgfxRenderAdapter::Make` / `ReadPixels` 路径已写明。
