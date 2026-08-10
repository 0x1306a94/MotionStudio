# PAG Export `_bmp` Bitmap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按修订后的 [PAG export design](../specs/2026-07-31-pag-export-design.md)（2026-08-10）：仅显式 `_bmp` 才导出 `BitmapComposition`；AE 风格序列编码；`MappingFailed` 带图层名与原因。

**Architecture:** 在现有 `pag_export`（`PagExporter` → `PagFileBuilder` → `PagBitmapFallback`）上做破坏性策略切换：去掉 FollowPath 等自动 Fallback；扫描合成名/层名 `_bmp`；Precomp 层名强制引用合成 Bitmap；`PagExportError` 改为结构体；序列帧做 diff/关键帧/maxResolution。

**Tech Stack:** C++17、GoogleTest、`pag_codec`、现有 `FakeBitmapFrameSource` / ASan Ninja 测试。

**Spec:** `docs/superpowers/specs/2026-07-31-pag-export-design.md`（§2 / §3.6 / §4 / §6）

## Global Constraints

- Bitmap **仅** `Composition.name` 或 `Layer.name` 以 `_bmp` 结尾（`ToLowerCase` + 后缀匹配）。
- 不可映射且无 `_bmp` → 整份 `MappingFailed`（结构化错误），不写残缺文件。
- Precomp 层名 `_bmp` → 强制引用合成按 Bitmap 导出（即使合成名无后缀）。
- `pag::*` 不进公共头；`Expected` 断言用 `hasValue()` / `error()`；比较 `error().kind`。
- 本轮可不交付生产 tgfx `BitmapFrameSource`；Fake 必须支持 `prepareComposition`。
- Commit：120 字符内英语、句号结尾（`.claude/rules/git-workflow.md`）；每 Task 提交并同步本 plan checkbox。
- 禁止链接 `third_party/libpag/exporter`（AE SDK / Qt）；diff/编码逻辑在 MS 侧自实现或精简移植算法，不 `#include` exporter。

## File Map

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/export/PagExporter.h` | `PagExportErrorKind` + 结构化 `PagExportError`；`Export` 签名 |
| `include/MotionStudio/export/PagExportOptions.h` | `allowBitmapExport` + 编码参数 |
| `include/MotionStudio/export/BitmapFrameSource.h` | 新增 `prepareComposition` |
| `src/export/pag/PagBmpSuffix.h`（或匿名 ns 工具） | `HasBmpSuffix(name)` |
| `src/export/pag/PagExportErrorUtil.h` | `MakePagExportError(...)` 辅助 |
| `src/export/pag/PagFileBuilder.*` | `_bmp` 收集；硬失败；去掉自动 Fallback soft-skip |
| `src/export/pag/PagBitmapFallback.*` | AE 风格 `BitmapSequence` 编码；合成级/层级入口 |
| `src/export/pag/PagExporter.cpp` | 传播结构化错误 |
| `tests/export/pag/FakeBitmapFrameSource.h` | `prepareComposition`；可选双帧 diff 测试钩子 |
| `tests/export/pag/PagExporterTest.cpp` | 迁移旧 Fallback 用例 + 新 `_bmp` / 错误详情用例 |

---

### Task 1: 结构化 `PagExportError` + Options 字段

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/export/PagExporter.h`
- Modify: `include/MotionStudio/export/PagExportOptions.h`
- Modify: `src/export/pag/PagExporter.cpp`
- Modify: `src/export/pag/PagFileBuilder.h` / `.cpp`（所有 `Unexpected(PagExportError::…)`）
- Modify: `src/export/pag/PagBitmapFallback.h` / `.cpp`
- Modify: `tests/export/pag/PagExporterTest.cpp`（先改编译：`error().kind`；行为用例下一 Task）
- Create: `src/export/pag/PagExportErrorUtil.h`

**Interfaces:**
- Produces:
```cpp
enum class PagExportErrorKind {
    InvalidComposition,
    InvalidOptions,
    MappingFailed,
    EncodeFailed,
    WriteFailed,
};

struct PagExportError {
    PagExportErrorKind kind = PagExportErrorKind::MappingFailed;
    EntityId entityId;
    std::string entityName;
    std::string code;
    std::string message;
};

inline PagExportError MakePagExportError(PagExportErrorKind kind,
                                        EntityId entityId,
                                        std::string entityName,
                                        std::string code,
                                        std::string message);

// PagExportOptions:
bool allowBitmapExport = true;       // 取代 allowBitmapFallback
float bitmapScale = 1.0f;
int bitmapMaxResolution = 720;
int bitmapKeyFrameInterval = 60;
int bitmapImageQuality = 80;
// 可暂留: bool allowBitmapFallback = true; 并在 Export 入口映射到 allowBitmapExport（一个版本后删）
```
- Consumes: 无

- [x] **Step 1: 写失败测试（错误形态）**

在 `PagExporterTest.cpp` 增加/改写：

```cpp
TEST(PagExporterTest, InvalidCompositionHasStructuredError) {
    Document document;
    PagExportOptions options;
    options.compositionId = EntityId::Generate();
    auto result = PagExporter::Export(document, options);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().kind, PagExportErrorKind::InvalidComposition);
    EXPECT_FALSE(result.error().message.empty());
}
```

- [x] **Step 2: 跑测试确认失败（类型/字段尚不存在）**

Run: `cmake --build build --target pag_export_tests && ./build/tests/pag_export_tests --gtest_filter='PagExporterTest.InvalidComposition*'`  
Expected: 编译失败或断言失败（旧 `PagExportError` enum）

- [x] **Step 3: 实现公共类型与辅助函数**

`PagExporter.h`：用上方 `PagExportErrorKind` / `PagExportError` 替换旧 enum。  
`PagExportErrorUtil.h`：

```cpp
#pragma once
#include "MotionStudio/export/PagExporter.h"
namespace motion {
inline PagExportError MakePagExportError(PagExportErrorKind kind, EntityId entityId,
                                        std::string entityName, std::string code,
                                        std::string message) {
    PagExportError error;
    error.kind = kind;
    error.entityId = entityId;
    error.entityName = std::move(entityName);
    error.code = std::move(code);
    error.message = std::move(message);
    return error;
}
}  // namespace motion
```

`PagExportOptions.h`：加入 `allowBitmapExport` 与三个 int；`allowBitmapFallback` 若保留则文档标注 deprecated，在 `PagExporter::Export` 开头：`if (!options.allowBitmapFallback) optionsEffective.allowBitmapExport = false;`（或要求调用方已迁完则直接删除旧字段——**本 Task 直接删除 `allowBitmapFallback`，测试一并改名**）。

- [x] **Step 4: 全量替换内部 `Unexpected`**

所有 `Unexpected(PagExportError::X)` →  
`Unexpected(MakePagExportError(PagExportErrorKind::X, {}, "", "", "…"))`  
（MappingFailed 的详细 message 在 Task 3 填齐；本 Task 先保证可编译，message 可用简短占位）。

测试中所有 `result.error()` 与 `static_cast<int>(result.error())` 改为 `result.error().kind`。

- [x] **Step 5: 跑测试确认通过**

Run: `./build/tests/pag_export_tests --gtest_filter='PagExporterTest.InvalidComposition*'`  
Expected: PASS（其余 FollowPath 用例可能仍按旧行为暂时绿/红——若红，本 Task 仅保证编译 + InvalidComposition；FollowPath 行为留 Task 3）

- [x] **Step 6: Commit**

```bash
git commit --only include/MotionStudio/export/PagExporter.h \
  include/MotionStudio/export/PagExportOptions.h \
  src/export/pag/PagExportErrorUtil.h \
  src/export/pag/PagExporter.cpp \
  src/export/pag/PagFileBuilder.h \
  src/export/pag/PagFileBuilder.cpp \
  src/export/pag/PagBitmapFallback.h \
  src/export/pag/PagBitmapFallback.cpp \
  tests/export/pag/PagExporterTest.cpp \
  docs/superpowers/plans/2026-08-10-pag-export-bmp.md \
  -m "Add structured PagExportError and bitmap export options fields."
```

---

### Task 2: `HasBmpSuffix` + `BitmapFrameSource::prepareComposition`

**Status:** ✅ Done

**Files:**
- Create: `src/export/pag/PagBmpSuffix.h`
- Modify: `include/MotionStudio/export/BitmapFrameSource.h`
- Modify: `tests/export/pag/FakeBitmapFrameSource.h`
- Test: `tests/export/pag/PagExporterTest.cpp`（可先加纯函数小测，或放在 exporter 测里）

**Interfaces:**
- Produces:
```cpp
// PagBmpSuffix.h
namespace motion { namespace pag_export {
inline bool HasBmpSuffix(std::string_view name); // ToLower + ends with "_bmp"
}}

// BitmapFrameSource
virtual Expected<void, std::string> prepareComposition(
    const Document &document, EntityId compositionId,
    TimeRange visibleRange, float bitmapScale) = 0;
```
- Consumes: Task 1 Options

- [x] **Step 1: 写失败测试**

```cpp
TEST(PagBmpSuffixTest, DetectsSuffixCaseInsensitive) {
    EXPECT_TRUE(pag_export::HasBmpSuffix("Comp_bmp"));
    EXPECT_TRUE(pag_export::HasBmpSuffix("Layer_BMP"));
    EXPECT_FALSE(pag_export::HasBmpSuffix("bmp_Comp"));
    EXPECT_FALSE(pag_export::HasBmpSuffix("Comp"));
}
```

（若测试目标未暴露内部头，把 `HasBmpSuffix` 放进 `include/MotionStudio/export/` 过重——保持 `src/export/pag/PagBmpSuffix.h`，在 `PagExporterTest.cpp` 旁加 `tests/export/pag/PagBmpSuffixTest.cpp` 并在 `tests/CMakeLists.txt` 加入源；或把函数测合并进 `PagExporterTest` 并 `#include "PagBmpSuffix.h"` 且保证测试 target include `src/export/pag`。）

- [x] **Step 2: 跑测确认失败**

Run: `./build/tests/pag_export_tests --gtest_filter='PagBmpSuffixTest.*'`  
Expected: 编译失败

- [x] **Step 3: 实现 `HasBmpSuffix` + `prepareComposition`**

```cpp
inline bool HasBmpSuffix(std::string_view name) {
    static constexpr std::string_view kSuffix = "_bmp";
    if (name.size() < kSuffix.size()) return false;
    // ASCII tolower compare last 4 chars
    …
}
```

`FakeBitmapFrameSource::prepareComposition`：按 `compositionId` 找合成，尺寸 = `comp.w/h * bitmapScale`，逻辑同现有 `prepare`（可抽私有 `prepareSize`）。

- [x] **Step 4: 跑测确认通过**

Run: `./build/tests/pag_export_tests --gtest_filter='PagBmpSuffixTest.*'`  
Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagBmpSuffix.h \
  include/MotionStudio/export/BitmapFrameSource.h \
  tests/export/pag/FakeBitmapFrameSource.h \
  tests/export/pag/PagBmpSuffixTest.cpp \
  tests/CMakeLists.txt \
  docs/superpowers/plans/2026-08-10-pag-export-bmp.md \
  -m "Add _bmp suffix helper and BitmapFrameSource prepareComposition."
```

---

### Task 3: 去掉自动 Fallback；硬失败 + `_bmp` 触发

**Status:** ✅ Done

**Files:**
- Modify: `src/export/pag/PagFileBuilder.cpp` / `.h`
- Modify: `tests/export/pag/PagExporterTest.cpp`

**Interfaces:**
- Consumes: `HasBmpSuffix`、`MakePagExportError`、`allowBitmapExport`
- Produces: `build()` 行为符合 spec §3.6.1 / §3.6.4

行为要点：

1. `collectBitmapCompositionIds`：合成名 `_bmp` **或** 被 Precomp 层名 `_bmp` 引用 → 记入集合。
2. 构建该合成时走 Bitmap 路径（调用 Task 4 的编码器；本 Task 可先接现有 `PagBitmapFallback::Build` 的层入口 / 临时合成入口，Task 4 再换编码）。
3. `needsBitmapFallback(layer)` 为 true 且层未被 Bitmap 覆盖 →  
   `return Unexpected(MakePagExportError(MappingFailed, layer.id, layer.name, "UnsupportedFollowPath", "Layer \"…\": FollowPath is not supported in vector PAG export; add \"_bmp\" …"))`  
   **禁止** soft-skip 继续导出。
4. 非 Precomp 层名 `_bmp` → `buildFallbackLayer`（光栅该层）。
5. `allowBitmapExport == false` 且需要 Bitmap → MappingFailed，message 说明禁止。
6. 需要 Bitmap 且 `frameSource == nullptr` → MappingFailed，message 说明缺 FrameSource。

- [x] **Step 1: 改写失败测试（破坏性）**

删除/替换：

- `FollowPathSkippedWhenFallbackDisabled`
- `FollowPathSkippedWithoutFrameSource`
- `FollowPathBitmapFallback`（改名并要求层名 `_bmp`）
- `GroupFollowPathRasterizesSubtree`（Group 名加 `_bmp`）

新增：

```cpp
TEST(PagExporterTest, FollowPathWithoutBmpFailsWithDetails) {
    Document document = MakeEmptyDoc(400, 300, 30);
    Layer *layer = AddShapeRect(document, Primary(document), Vec2{0, 0}, Vec2{20, 20});
    layer->name = "Follow";
    layer->followPath.enabled = true;
    auto result = PagExporter::Export(document, {});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().kind, PagExportErrorKind::MappingFailed);
    EXPECT_EQ(result.error().code, "UnsupportedFollowPath");
    EXPECT_EQ(result.error().entityName, "Follow");
    EXPECT_NE(result.error().message.find("Follow"), std::string::npos);
    EXPECT_NE(result.error().message.find("_bmp"), std::string::npos);
}

TEST(PagExporterTest, FollowPathLayerBmpExportsBitmap) {
    Document document = MakeEmptyDoc(40, 30, 3);
    Layer *layer = AddShapeRect(document, Primary(document), Vec2{0, 0}, Vec2{20, 20});
    layer->name = "Follow_bmp";
    layer->followPath.enabled = true;
    FakeBitmapFrameSource frameSource;
    PagExportOptions options;
    options.allowBitmapExport = true;
    auto result = PagExporter::Export(document, options, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    // assert BitmapComposition exists + BitmapForcedByLayerName warning
}

TEST(PagExporterTest, CompositionNameBmpExportsBitmap) {
    Document document = MakeEmptyDoc(40, 30, 2);
    Primary(document)->name = "Main_bmp";
    FakeBitmapFrameSource frameSource;
    auto result = PagExporter::Export(document, {}, &frameSource);
    ASSERT_TRUE(result.hasValue()) << result.error().message;
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->compositions.back()->type(), pag::CompositionType::Bitmap);
}
```

另加：Precomp 层名 `_bmp` 强制子合成；`allowBitmapExport=false` + `_bmp` → fail。

- [x] **Step 2: 跑测确认失败（旧行为仍绿/新断言红）**

Run: `./build/tests/pag_export_tests --gtest_filter='PagExporterTest.FollowPath*'`  
Expected: 新用例 FAIL

- [x] **Step 3: 实现 Builder 策略**

- 删除 soft-fail 分支（`PagFileBuilder.cpp` 中 `MappingFailed` 时 skip 层的逻辑）。
- 在 `build()` / `buildComposition` 接入 `_bmp` 集合。
- Precomp：`buildPrecompLayer` 前确保子合成已按 Bitmap 或 Vector 建好。
- Warning：`BitmapForcedByLayerName` / `BitmapForcedByCompositionName`。

合成级 Bitmap：新增 `PagBitmapFallback::BuildComposition(...)`（若尚无，本 Task stub 调 `prepareComposition` + 现有整帧关键帧编码；Task 4 替换为 diff）。

- [x] **Step 4: 跑测确认通过**

Run: `./build/tests/pag_export_tests --gtest_filter='PagExporterTest.*'`  
Expected: PASS（ASan build）

- [x] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagFileBuilder.cpp \
  src/export/pag/PagFileBuilder.h \
  src/export/pag/PagBitmapFallback.cpp \
  src/export/pag/PagBitmapFallback.h \
  tests/export/pag/PagExporterTest.cpp \
  docs/superpowers/plans/2026-08-10-pag-export-bmp.md \
  -m "Require explicit _bmp suffix for PAG bitmap export and hard-fail unsupported layers."
```

---

### Task 4: AE 风格 BitmapSequence 编码

**Status:** 待开始

**Files:**
- Modify: `src/export/pag/PagBitmapFallback.cpp` / `.h`
- Optional Create: `src/export/pag/PagBitmapSequenceEncode.h` / `.cpp`（diff / IsKeyFrame / WebP 矩形）
- Modify: `tests/export/pag/FakeBitmapFrameSource.h`（支持按帧变色以便测 diff）
- Modify: `tests/export/pag/PagExporterTest.cpp`

**Interfaces:**
- Consumes: `PagExportOptions::{bitmapScale,bitmapMaxResolution,bitmapKeyFrameInterval,bitmapImageQuality}`
- Produces: `BitmapSequence` 帧带 `isKeyframe` 与增量 `BitmapRect`

算法对照（只读，不链接）：  
`third_party/libpag/exporter/src/export/sequence/BitmapSequence.cpp`  
`third_party/libpag/exporter/src/utils/ImageData.*`

关键帧规则（移植 `IsKeyFrame`）：首帧；`diffSize==0` → 非关键帧；全变；大面积变 + 距上次关键帧；超过 `bitmapKeyFrameInterval`。

尺寸：spec §3.6.2（maxResolution、factor 钳制）。首版 max scale 可先用 `bitmapScale` + maxResolution；Precomp max-scale 扫描可同 Task 落地简化为 `min(bitmapScale, 1)` 再套 maxResolution。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(PagExporterTest, BitmapSequenceSkipsUnchangedFramePayload) {
    // Fake：frame0 红，frame1 同红 → 第二帧 isKeyframe=false 且 bitmaps 空或无新 rect
    // Fake：frame2 大变 → isKeyframe=true
}
TEST(PagExporterTest, BitmapMaxResolutionCapsShortSide) {
    // composition 2000x1000, bitmapScale=1, maxResolution=720 → sequence short side <= 720
}
```

扩展 Fake：`std::function<void(FrameTime, std::vector<uint8_t>&)>` 或 `setFrameTint(FrameTime, rgba)`。

- [ ] **Step 2: 跑测确认失败**

Run: `./build/tests/pag_export_tests --gtest_filter='PagExporterTest.BitmapSequence*'`  
Expected: FAIL（当前每帧整幅关键帧）

- [ ] **Step 3: 实现序列编码器**

在 `PagBitmapFallback`（或新文件）中：

1. 保留上一帧 RGBA buffer
2. 算 diff rect（逐像素 RGBA 比较）
3. `IsKeyFrame` → 决定 encode 全图裁边或增量区
4. WebP 质量用 `options.bitmapImageQuality`（替换写死 80）
5. 填充 `pag::BitmapFrame` / `BitmapRect`

- [ ] **Step 4: 跑测确认通过**

Run: `ctest --test-dir build -R 'PagExporter' --output-on-failure`  
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagBitmapFallback.cpp \
  src/export/pag/PagBitmapFallback.h \
  src/export/pag/PagBitmapSequenceEncode.cpp \
  src/export/pag/PagBitmapSequenceEncode.h \
  src/export/pag/CMakeLists.txt \
  tests/export/pag/FakeBitmapFrameSource.h \
  tests/export/pag/PagExporterTest.cpp \
  docs/superpowers/plans/2026-08-10-pag-export-bmp.md \
  -m "Encode PAG bitmap sequences with AE-style diff keyframes and resolution caps."
```

---

### Task 5: 回归扫尾 + 旧 plan 指针

**Status:** 待开始

**Files:**
- Modify: `docs/superpowers/plans/2026-07-31-pag-export.md`（文首注明 Bitmap 策略以 2026-08-10 plan/spec 为准）
- Modify: 任何仍引用 `allowBitmapFallback` / 旧 enum 的文件（`rg` 清零）

- [ ] **Step 1: 全量搜索残留**

Run: `rg 'allowBitmapFallback|BitmapFallbackUnavailable|PagExportError::' --glob '!third_party/**' --glob '!docs/superpowers/specs/**'`  
Expected: 无实现/测试命中（docs 历史除外）

- [ ] **Step 2: ASan 全量相关测试**

Run: `ctest --test-dir build -R 'pag_export|PagExporter|PagCodec' --output-on-failure`  
Expected: PASS

- [ ] **Step 3: 更新旧 plan 指针 + Commit**

```bash
git commit --only docs/superpowers/plans/2026-07-31-pag-export.md \
  docs/superpowers/plans/2026-08-10-pag-export-bmp.md \
  -m "Point legacy PAG export plan at explicit _bmp bitmap policy."
```

---

## Spec coverage（自检）

| Spec 要求 | Task |
|---|---|
| 结构化 `PagExportError` + MappingFailed 详情 | Task 1、3 |
| Options：`allowBitmapExport` / maxRes / KF / quality | Task 1、4 |
| `prepareComposition` | Task 2 |
| 仅 `_bmp` 触发；Precomp 层名强制子合成 | Task 3 |
| 无 `_bmp` 硬失败 | Task 3 |
| AE diff / 关键帧 / maxResolution | Task 4 |
| 测试表 §6 Bitmap 行 | Task 3–4 |
| 不交付生产 FrameSource | 全计划（仅 Fake） |

## Placeholder scan

无 TBD；算法对照路径已给出；旧 soft-fail 明确删除。
