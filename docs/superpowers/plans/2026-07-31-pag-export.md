# PAG Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 库层 `PagExporter`（Document → `.pag`）；先矢量可编辑子集，Bitmap 降级后置。

**Architecture（以 [spec §0](../specs/2026-07-31-pag-export-design.md) 为准）：**  
Phase 0（**已完成**）：唯一 tgfx 源码 = `third_party/libpag/third_party/tgfx`；MS 继续 `BuildTgfx.cmake` 预编译 **Metal** `tgfx.a`；**不** `add_subdirectory(libpag)`；`adapter/pag_codec` 只编 base+codec + Platform stub，链同一份预编译 tgfx。  
Phase 1a（**本轮**）：`pag_export` + Composition / Shape / Group / Transform+KF；遇 FollowPath / 未支持层类型 → `MappingFailed`（无 Bitmap）。  
Phase 1b（后置）：Text / Image / Mask / TrackMatte / Precomp / BitmapFallback。

**Tech Stack:** C++17、GoogleTest、`third_party/libpag`（内嵌 tgfx）、`adapter/pag_codec`、现有 tgfx adapter / Xcode Catalyst。

**Spec:** `docs/superpowers/specs/2026-07-31-pag-export-design.md`（**§0 已落地**；架构摘要见 `docs/architecture.md`）

## Global Constraints

- Phase 0 已完成；可开始 Phase 1。
- 禁止并行维护独立 `third_party/tgfx`。
- tgfx 头文件路径固定为 `third_party/libpag/third_party/tgfx/include`（无 symlink）。
- **保留** MS `BuildTgfx.cmake` 预编译 Metal tgfx；adapter / App / `pag_codec` / `pag_export` 链该产物（或经 `pag_codec` 传递）。
- **禁止** `add_subdirectory(third_party/libpag)` 整库；编解码只经 `pag_codec`。
- `pag_export` **PRIVATE 链 `pag_codec`**（不要链上游 CMake 目标 `pag`）。
- `pag::*` 不进 `include/MotionStudio` 公共头；`motionstudio_core` 不链 `pag_codec`。
- 库层 PagExporter：无 UI / 无 Bridge；Expected 用 `hasValue()`/`error()`。
- 本轮不做 BitmapFallback / `BitmapFrameSource`；`allowBitmapFallback` 可不暴露或固定为无效。
- Commit：120 字符内英语、句号结尾（`.claude/rules/git-workflow.md`）。

## File Map

| 区域 | 文件 | 状态 |
|---|---|---|
| DEPS / sync | `DEPS`、`sync_deps.sh` | Phase 0 完成 |
| Catalyst patches | `patches/libpag-tgfx-maccatalyst-arm64.patch`、`patches/libpag-tgfx-vendor_tools-maccatalyst-arm64.patch` | Phase 0 完成 |
| tgfx 预编译 | `cmake/BuildTgfx.cmake`、`adapter/tgfx/CMakeLists.txt` | Phase 0 完成（**保留**，非拆除） |
| 编解码裁剪 | `adapter/pag_codec/` | Phase 0 完成 |
| 根变量 | 根 `CMakeLists.txt`（`LIBPAG_DIR` / `TGFX_*`） | Phase 0 完成 |
| 文档 | `docs/architecture.md`、spec §0 | Phase 0 完成 |
| PagExporter | `include/MotionStudio/export/Pag*.h`、`src/export/pag/*`、`tests/export/Pag*.cpp` | Phase 1 待做 |
| CMake 接入 | 根或 `src/export/pag/CMakeLists.txt`、`tests/CMakeLists.txt` | Phase 1 待做 |

---

# Phase 0 — 依赖收敛 + Catalyst（已完成）

> 早期曾写「拆除 `BuildTgfx` + `add_subdirectory(libpag)`」——**已否决**，勿再执行。落地结果见 spec §0。

### 完成清单

- [x] DEPS：独立 `third_party/tgfx` → pin `third_party/libpag`；shaderc sync + libpag 内 tgfx Catalyst patch
- [x] `sync_deps.sh` skip-paths 指向 libpag 内 shaderc/tint/libyuv
- [x] 保留 `BuildTgfx.cmake`；根 CMake 设 `TGFX_*` / `LIBPAG_DIR`；adapter 链预编译 Metal `tgfx.a`
- [x] `adapter/pag_codec`（base+codec + PlatformStub）+ `pag_codec_test` Encode↔Decode；测试目标自挂 tgfx PRE_BUILD / search paths
- [x] `docs/architecture.md` + spec §0 记录拓扑与取舍

**门禁：** Phase 0 已满足；进入 Phase 1。

---

# Phase 1a — 矢量 PagExporter（本轮）

> 链 `pag_codec`（非 `pag`）。冒烟 Encode 逻辑已有 `pag_codec_test`；本阶段在其上做 Document 映射。

### Task 5: `pag_export` 库 + CMake 接入

**Files:**
- Create: `src/export/pag/CMakeLists.txt`（或等价挂到根/`src`）
- Create: `tests/export/PagEncodeSmokeTest.cpp`（可选：若直接做 Task 6 空合成，可合并跳过独立 smoke）
- Modify: `tests/CMakeLists.txt`、根 `CMakeLists.txt`（APPLE：`pag_export` 在 `pag_codec` 之后）

**Interfaces:**
- Produces: `pag_export`（PUBLIC `core`，PRIVATE `pag_codec`）、`pag_export_tests`
- Xcode：`pag_export_tests` 需 `motionstudio_add_tgfx_prebuild` + `motionstudio_link_prebuilt_tgfx`（与 `pag_codec_test` 同，否则 `ld: library 'tgfx' not found`）

- [x] **Step 1–2: CMake + Encode 冒烟**（`pag_export` PRIVATE `pag_codec`；测试自挂 tgfx）
- [x] **Step 3:** 合入后续 Task 一并提交

---

### Task 6: 公共 API + 空合成导出

**Files:**
- Create: `include/MotionStudio/export/PagExportOptions.h`、`PagExporter.h`
- Create: `src/export/pag/PagExporter.cpp`、`PagFileBuilder.h/.cpp`
- Create: `tests/export/pag/PagExporterTest.cpp`

**Interfaces（本轮精简，无 BitmapFrameSource）：**

```cpp
PagExporter::Export(const Document&, const PagExportOptions&)
  → Expected<PagExportResult, PagExportError>
```

- `PagExportOptions`：`outputPath`、`compositionId`（无效 → 主/第一个合成）
- 无 `frameSource` 参数；遇不可映射 → `MappingFailed`

- [x] InvalidComposition / 空合成 Encode+Decode
- [x] resolve + 空 `VectorComposition` + Encode + 可选写文件

---

### Task 7: Shape + 静态 Transform

- [x] ShapeRect / Ellipse / Path → ShapeLayer
- [x] Fill / Stroke（含 Trim→TrimPath）+ Transform2D 静态值

---

### Task 8: Keyframe / 缓动

- [x] position 两关键帧 + 时间贝塞尔；空间切线映射到 PAG SpatialPointKeyframe

---

### Task 9: Group → NullLayer + parent；未支持类型失败

- [x] Group → `NullLayer` + `parent`
- [x] FollowPath / Text / Image / Precomp / Mask → **整次** `MappingFailed`

---

### Task 10: Phase 1a 验收

- [x] `pag_export_tests`（ASan）通过；含失败路径单测
- [ ] 可选回归：`pag_codec_test` + `core_tests`

**Phase 1a 完成门禁：** Shape/Group/KF 可 Encode+Decode；FollowPath 等失败路径有单测。

---

# Phase 1b — 扩展映射 + Bitmap（后置，本轮不实施）

> 仍链 `pag_codec`。Bitmap 需注入 `BitmapFrameSource`（生产实现可后挂 tgfx 离屏）。

### Task 11: Text + Image

- [ ] TextLayer / TextDocument；无法表达的细节 → warning，尽量保矢量
- [ ] ImageBytes + scaleMode；缺 asset → `MappingFailed`（或占位，实现时锁定一种）
- [ ] Commit — `Export text and image layers into editable PAG content.`

---

### Task 12: Mask / TrackMatte / BlendMode

- [ ] MaskData、trackMatte 顺序、BlendMode 子集
- [ ] 无 PAG 对应且不能近似 → 本阶段可走 Bitmap（Task 14）或 `MappingFailed`
- [ ] Commit — `Export masks track mattes and blend modes into PAG.`

---

### Task 13: Precomp

- [ ] `PreComposeLayer`；时间：`innerTime = (outer - inPoint) × timeStretch + startTime`
- [ ] 多 Composition 进同一 `pag::File`
- [ ] Commit — `Export precomps into PAG layer graphs.`

---

### Task 14: BitmapFallback + FollowPath

**Files:** `PagBitmapFallback.*`、`tests/export/FakeBitmapFrameSource.h`

- [ ] FollowPath + Fake → warning + BitmapComposition + Load
- [ ] `allowBitmapFallback=false` → `MappingFailed`
- [ ] Group 自身触发 → 子树光栅化
- [ ] Commit — `Rasterize unsupported layers into PAG bitmap compositions.`

---

## Spec 覆盖自检

| Spec 项 | Task | 状态 |
|---|---|---|
| §0 唯一 tgfx 源 + Metal 预编译 + `pag_codec` | Phase 0 | 完成 |
| §0 否决整库 libpag / 拆除 BuildTgfx | Phase 0 | 完成（与早期草案相反） |
| Encode / PagExporter API | 5–6 | 1a |
| Shape / KF / Group | 7–9 | 1a |
| 未支持 → MappingFailed（无 Bitmap） | 9 | 1a |
| Text / Image | 11 | 1b |
| Mask / Matte / Blend | 12 | 1b |
| Precomp | 13 | 1b |
| BitmapFallback / FollowPath | 14 | 1b |
| 无 UI/Bridge | 全局 | — |

## 执行说明

- Phase 0 已合入依赖侧；Phase 1a / 1b 可分 PR。
- 实现时勿再打开「删 BuildTgfx / add_subdirectory(libpag)」路径。
- 用户若收窄 1a（例如只要 Shape、不要 Group），以对话确认覆盖本 plan Task 9。
