# PAG Export VideoComposition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `_bmp` 可按 Auto/Video/Bitmap 导出为 `VideoComposition`（VT + Alpha 侧边条）或现有 Bitmap，默认 Auto，导出 UI 可选。

**Architecture:** 共享 `BitmapFrameSource` 出 RGBA；`resolveSequenceKind` 分流；Video 路径 `PackRgbAlphaSideBySide` → VideoToolbox Annex-B → `pag::VideoComposition`；层 PreCompose 放大逻辑与 Bitmap 对称。

**Tech Stack:** C++17、ObjC++/VideoToolbox（Apple）、pag_codec、GoogleTest、SwiftUI/UIKit 导出设置页。

**Spec:** `docs/superpowers/specs/2026-08-10-pag-export-video-composition-design.md`

## Global Constraints

- 选项：`PagBmpSequenceType { Auto, Video, Bitmap }`，默认 **Auto**。
- Video **一律** Alpha 侧边条：`alphaStartX=W`，`alphaStartY=0`。
- Auto：几乎静态 → Bitmap；否则 Video；编码失败 **不**静默回落。
- 尺寸：`ComputeBitmapSize`；FrameSource 收 `pixelWidth/Height`。
- 层 `_bmp` PreCompose：maxResolution 缩小后按 host/bitmap 放大。
- 非 Apple：Video/Auto 非静态 → `EncodeFailed`。
- Commit：120 字符内英语、句号结尾；每 Task 同步本 plan checkbox。
- `pag_export` 已是 Apple-only；VT 可在 `.cpp` 链 `VideoToolbox`/`CoreMedia`/`CoreVideo`，或单独 `.mm`（需改 CMake `add_files_by_extension` 含 `.mm`）。

## File Map

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/export/PagExportOptions.h` | `PagBmpSequenceType` + 字段 |
| `src/export/pag/PagRgbAlphaPack.h` (+ `.cpp`) | RGBA → 侧边条缓冲 |
| `src/export/pag/PagVideoSequenceEncode.h` (+ `.cpp`/`.mm`) | VT 编码填 `VideoSequence` |
| `src/export/pag/PagAlmostStatic.h` (+ `.cpp`) | Auto 静态抽样 |
| `src/export/pag/PagVideoFallback.h` (+ `.cpp`) | 建 `VideoComposition` + 层包装 |
| `src/export/pag/PagFileBuilder.cpp` | resolve kind 后调 Bitmap/Video |
| `src/export/pag/CMakeLists.txt` | 链 VideoToolbox 等；必要时收录 `.mm` |
| `bridge/include/motionstudio_bridge.h` | `MS_PAG_BMP_SEQUENCE_TYPE` |
| `bridge/src/apple/motionstudio_bridge_pag_export.mm` | 映射选项 |
| `apps/.../PagExportSettingsViewController.swift` 等 | UI 分段 |
| `tests/export/pag/*` | 单测 / 集成 |

---

### Task 1: `PagBmpSequenceType` + Bridge 透传（行为暂等同 Bitmap）

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/export/PagExportOptions.h`
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/apple/motionstudio_bridge_pag_export.mm`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Export/PagExportSettingsViewController.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Export/` 相关 Session/Core 传参
- Modify: `bridge/tests/PagExportBridgeTest.cpp`（默认值不破现有）

**Interfaces:**
- Produces:
```cpp
enum class PagBmpSequenceType { Auto = 0, Video = 1, Bitmap = 2 };
// PagExportOptions::bmpSequenceType = Auto
```
```c
typedef CF_CLOSED_ENUM(int, MS_PAG_BMP_SEQUENCE_TYPE) { ... };
// MSPagExportOptions.bmpSequenceType
```
- **本 Task 强制**：`PagFileBuilder` 仍只走 Bitmap（忽略 type 或暂时把 Auto/Video 也当 Bitmap），避免半成品 Video 弄红 CI。Video 分流在 Task 5。

- [x] **Step 1: 加枚举与字段；Bridge/App 透传**

`PagExportOptions.h` 增加枚举与 `bmpSequenceType`。  
`motionstudio_bridge.h` 增加 `MS_PAG_BMP_SEQUENCE_TYPE` 与 struct 字段。  
`ms_pag_export`：`exportOptions.bmpSequenceType = static_cast<PagBmpSequenceType>(options->bmpSequenceType)`（非法 raw → Auto）。  
App：`PagExportSettings` + 分段控件默认 Auto；仅 `allowBitmapExport` 开启时可交互。

- [x] **Step 2: 跑现有测**

Run: `./build/src/export/pag/pag_export_tests --gtest_filter='PagExporterTest.*'`  
Expected: PASS（仍走 Bitmap）

- [x] **Step 3: Commit**

```bash
git commit --only include/MotionStudio/export/PagExportOptions.h \
  bridge/include/motionstudio_bridge.h \
  bridge/src/apple/motionstudio_bridge_pag_export.mm \
  apps/MotionStudioApp/MotionStudioApp/Export/PagExportSettingsViewController.swift \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+PagExport.swift \
  docs/superpowers/plans/2026-08-10-pag-export-video-composition.md \
  -m "Add PagBmpSequenceType option and wire Bridge and export UI."
```

（实际改动的 App 文件以仓库为准，一并 `--only`。）

---

### Task 2: `PackRgbAlphaSideBySide`

**Status:** ✅ Done

**Files:**
- Create: `src/export/pag/PagRgbAlphaPack.h`
- Create: `src/export/pag/PagRgbAlphaPack.cpp`
- Create: `tests/export/pag/PagRgbAlphaPackTest.cpp`

**Interfaces:**
```cpp
namespace motion::pag_export {
// Packs premultiplied or straight RGBA W×H into side-by-side buffer.
// outWidth = evenAlign(W*2), outHeight = evenAlign(H).
// Left W×H: RGB from src (A ignored for color channels).
// Right W×H: R=G=B=src.A, A=255.
// outRowBytes = outWidth * 4. Caller owns outPixels.
bool PackRgbAlphaSideBySide(const uint8_t *rgba, int width, int height, size_t rowBytes,
                            std::vector<uint8_t> *outPixels, int *outWidth, int *outHeight);
}
```

- [x] **Step 1: 写失败测试**

```cpp
TEST(PagRgbAlphaPackTest, SideBySideAlphaAtRight) {
    // 2x1 RGBA: pixel0=(10,20,30,128), pixel1=(1,2,3,255)
    // expect outW>=4 even, outH>=1 even
    // left (0,0) RGB≈10,20,30; right (width,0) RGB≈128,128,128
}
```

- [x] **Step 2: 跑测确认失败**

Run: `./build/src/export/pag/pag_export_tests --gtest_filter='PagRgbAlphaPackTest.*'`  
Expected: 编译失败

- [x] **Step 3: 实现 pack**

- [x] **Step 4: 跑测确认通过**

Expected: PASS

- [x] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagRgbAlphaPack.h src/export/pag/PagRgbAlphaPack.cpp \
  tests/export/pag/PagRgbAlphaPackTest.cpp \
  docs/superpowers/plans/2026-08-10-pag-export-video-composition.md \
  -m "Add RGB plus alpha side-by-side packer for PAG video sequences."
```

---

### Task 3: `PagAlmostStatic` 抽样

**Status:** 待开始

**Files:**
- Create: `src/export/pag/PagAlmostStatic.h`
- Create: `src/export/pag/PagAlmostStatic.cpp`
- Create: `tests/export/pag/PagAlmostStaticTest.cpp`

**Interfaces:**
```cpp
// Samples first/mid/last (+ up to 2 uniform) frames in [start,end).
// Returns true if changed-pixel ratio across consecutive samples < 0.001f.
bool IsAlmostStaticSequence(BitmapFrameSource *source, FrameTime start, FrameTime end);
```
- Consumes: 已 `prepare*` 的 FrameSource；函数内 `renderFrame` 抽样，**不** `finish`（调用方继续用）。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(PagAlmostStaticTest, SolidFakeIsStatic) { /* Fake solid, prepareComposition, EXPECT_TRUE */ }
TEST(PagAlmostStaticTest, TintChangeIsNotStatic) {
  // Fake setFrameColor(0,red) setFrameColor(1,blue); range [0,2); EXPECT_FALSE
}
```

- [ ] **Step 2–4: 实现并跑绿**

- [ ] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagAlmostStatic.h src/export/pag/PagAlmostStatic.cpp \
  tests/export/pag/PagAlmostStaticTest.cpp \
  docs/superpowers/plans/2026-08-10-pag-export-video-composition.md \
  -m "Detect almost-static bitmap ranges for PAG Auto sequence type."
```

---

### Task 4: `EncodeVideoSequence`（VideoToolbox）

**Status:** 待开始

**Files:**
- Create: `src/export/pag/PagVideoSequenceEncode.h`
- Create: `src/export/pag/PagVideoSequenceEncode.cpp`（或 `.mm`）
- Modify: `src/export/pag/CMakeLists.txt`（`target_link_libraries(... "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo")`；若用 `.mm` 扩展 glob）
- Create: `tests/export/pag/PagVideoSequenceEncodeTest.cpp`

**Interfaces:**
```cpp
Expected<void, PagExportError> EncodeVideoSequence(
    BitmapFrameSource *frameSource, pag::VideoSequence *sequence,
    FrameTime start, FrameTime end, int width, int height,
    int keyFrameInterval, int imageQuality);
// Fills sequence->frames, headers; sets alphaStartX=width, alphaStartY=0;
// sequence->width/height = width/height; frameRate already set by caller.
// Calls frameSource->finish() at end (mirror EncodeBitmapSequence).
```

实现要点：
- 每帧 `renderFrame` → `PackRgbAlphaSideBySide` → VT 压成 Annex-B
- `VTCompressionSession`：`kCMVideoCodecType_H264`，`kVTCompressionPropertyKey_MaxKeyFrameInterval` = keyFrameInterval
- bitrate：`imageQuality` 映射（如 `W*H*fps*bytesPerPixel*quality/100` 量级，实现选合理常量）
- 回调收集 SPS/PPS 一次进 `headers`；每帧 NAL → `VideoFrame{frame, isKeyframe, fileBytes}`
- 失败 → `EncodeFailed`

- [ ] **Step 1: 写失败测试（Apple）**

```cpp
#if defined(__APPLE__)
TEST(PagVideoSequenceEncodeTest, EncodesSideBySideAlphaHeaders) {
  Document doc; // 64x64, 2 frames, red rect
  FakeBitmapFrameSource src;
  // prepareComposition(..., 64, 64)
  auto *comp = new pag::VideoComposition();
  auto *seq = new pag::VideoSequence();
  seq->width=64; seq->height=64; seq->frameRate=30; seq->composition=comp;
  ASSERT_TRUE(EncodeVideoSequence(&src, seq, 0, 2, 64, 64, 60, 80).hasValue());
  EXPECT_EQ(seq->alphaStartX, 64);
  EXPECT_EQ(seq->alphaStartY, 0);
  EXPECT_FALSE(seq->headers.empty());
  EXPECT_EQ(seq->frames.size(), 2u);
  delete comp; // owns seq if pushed — 按 pag 所有权：先 seq 挂到 comp.sequences 再删 comp
}
#endif
```

- [ ] **Step 2–4: 实现 VT 编码并跑绿**

Run: `./build/src/export/pag/pag_export_tests --gtest_filter='PagVideoSequenceEncodeTest.*'`  
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagVideoSequenceEncode.h \
  src/export/pag/PagVideoSequenceEncode.cpp \
  src/export/pag/CMakeLists.txt \
  tests/export/pag/PagVideoSequenceEncodeTest.cpp \
  docs/superpowers/plans/2026-08-10-pag-export-video-composition.md \
  -m "Encode PAG VideoSequence with VideoToolbox and alpha side strip."
```

---

### Task 5: `PagVideoFallback` + FileBuilder 分流

**Status:** 待开始

**Files:**
- Create: `src/export/pag/PagVideoFallback.h`
- Create: `src/export/pag/PagVideoFallback.cpp`
- Modify: `src/export/pag/PagFileBuilder.cpp`（`buildFallbackLayer` / `buildBitmapComposition`）
- Modify: `tests/export/pag/PagExporterTest.cpp`

**Interfaces:**
```cpp
struct VideoFallbackResult {
  pag::PreComposeLayer *layer = nullptr;
  pag::VideoComposition *composition = nullptr;
};
class PagVideoFallback {
  static Expected<VideoFallbackResult, PagExportError> Build(...); // 同 Bitmap 签名
  static Expected<pag::VideoComposition *, PagExportError> BuildComposition(...);
};
```

分流伪代码（层路径示例）：

```cpp
// after ComputeBitmapSize + prepare(...)
const bool almostStatic =
  options.bmpSequenceType == PagBmpSequenceType::Auto &&
  IsAlmostStaticSequence(frameSource, visibleStart, visibleEnd);
PagBmpSequenceType kind = options.bmpSequenceType;
if (kind == PagBmpSequenceType::Auto) {
  kind = almostStatic ? PagBmpSequenceType::Bitmap : PagBmpSequenceType::Video;
}
if (kind == PagBmpSequenceType::Bitmap) {
  // existing EncodeBitmapSequence path (may need prepare again if static probe consumed frames
  // — Fake/Tgfx: probe renders ok; then Encode* renders again. OK.
  // If probe left source prepared, EncodeBitmapSequence loops renderFrame — OK.
} else {
  return PagVideoFallback::Build(...); // EncodeVideoSequence inside
}
```

**注意：** `IsAlmostStaticSequence` 与后续 `Encode*` 都要完整 `renderFrame`；prepare 一次即可。`Encode*` 末尾 `finish()`。

层 PreCompose scale：从 `PagBitmapFallback` **抽出**共享 helper 或在 Video 路径复制同一 scale 逻辑（1920/1280 → 1.5）。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(PagExporterTest, BmpSequenceTypeVideoExportsVideoComposition) {
  // Main_bmp, Fake, options.bmpSequenceType=Video, allowBitmapExport=true
  // Export → File::Load → compositions 含 Video; alphaStartX==sequence.width
}
TEST(PagExporterTest, BmpSequenceTypeAutoStaticUsesBitmap) {
  // solid Fake, Auto → BitmapComposition
}
TEST(PagExporterTest, BmpSequenceTypeAutoMotionUsesVideo) {
  // Fake tint differs frame 0 vs 1, Auto → VideoComposition
}
TEST(PagExporterTest, LayerBmpVideoMaxResolutionScalesPrecompose) {
  // 1920x1080 layer _bmp, maxRes 720, Video → PreCompose scale 1.5
}
```

- [ ] **Step 2: 跑测确认失败**

- [ ] **Step 3: 实现 Fallback + 分流**

- [ ] **Step 4: 全量相关测**

Run: `ctest --test-dir build -R 'PagExporter|PagVideo|PagAlmost|PagRgbAlpha|PagBmpSuffix|PagExportBridge' --output-on-failure`  
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagVideoFallback.h src/export/pag/PagVideoFallback.cpp \
  src/export/pag/PagFileBuilder.cpp src/export/pag/PagBitmapFallback.cpp \
  tests/export/pag/PagExporterTest.cpp \
  docs/superpowers/plans/2026-08-10-pag-export-video-composition.md \
  -m "Route _bmp PAG export to VideoComposition when sequence type is Video."
```

---

### Task 6: Bridge 集成测 + 文档勾选

**Status:** 待开始

**Files:**
- Modify: `bridge/tests/PagExportBridgeTest.cpp`（`allowBitmapExport=true` + `bmpSequenceType=Video` 导出 `_bmp`，断言文件存在；可选解码查 Video）
- Modify: `docs/superpowers/specs/2026-08-10-pag-export-video-composition-design.md` 状态 → 已实现

- [ ] **Step 1: Bridge 测试传 `MS_PAG_BMP_SEQUENCE_VIDEO`**

- [ ] **Step 2: 跑 bridge + pag 测**

- [ ] **Step 3: Commit**

```bash
git commit --only bridge/tests/PagExportBridgeTest.cpp \
  docs/superpowers/specs/2026-08-10-pag-export-video-composition-design.md \
  docs/superpowers/plans/2026-08-10-pag-export-video-composition.md \
  -m "Cover bridge PAG video sequence export and mark video design done."
```

---

## Spec coverage

| Spec | Task |
|---|---|
| 选项枚举 + UI/Bridge | Task 1 |
| Alpha 侧边条 pack | Task 2 |
| Auto 静态判定 | Task 3 |
| VT EncodeVideoSequence | Task 4 |
| VideoFallback + 分流 + scale | Task 5 |
| 集成 / 文档 | Task 6 |

## Placeholder scan

无 TBD；VT 质量映射用实现常量；静态阈值 `0.001f` 写死在 `PagAlmostStatic.cpp`。
