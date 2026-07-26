# Layer Masks + Track Matte Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完整支持 AE 常用 Path Masks（含 feather/expansion/可动画 path）与 Track Matte（Alpha/Luma ± 反相），经统一离屏 coverage 管线渲染，并提供编辑 UI。

**Architecture:** 扩展 `Mask` / `Layer` 模型 → `SceneEvaluator` 产出 `EvaluatedMask` + matte 引用 → `CommandBuilder` 发 `BeginLayer/BeginMask/...` → `TgfxCanvasAdapter` 离屏合成。硬边可降级 `ClipPath`。

**Tech Stack:** C++17 core、GoogleTest、tgfx Metal 适配器、SwiftUI bridge/app。

**Spec:** `docs/superpowers/specs/2026-07-26-layer-masks-design.md`

## Global Constraints

- 不升 `schemaVersion`，不做旧文档迁移；直接改当前 JSON。
- Core 不依赖 tgfx；遮罩语义经 `DrawCommand` / `RenderAdapter` 表达。
- 首版 MaskMode 仅 Add / Subtract / Intersect。
- Core / 适配器 / 测试 / 文档：任务完成后可自动提交（不推送）。
- Bridge + SwiftUI：实现后先留本地，**UI 人工验证通过后再提交**。

**进度（2026-07-26）：** Task 1–8、Task 10 文档部分已完成；Task 9（SwiftUI）进行中，待人工验证后再提交。

## File Map

| 区域 | 文件 |
|---|---|
| 模型 | `include/MotionStudio/model/Layer.h`, `MaskMode.h`, 新增 `TrackMatteType.h` |
| PropertyPath | `src/model/PropertyPath.cpp` |
| 序列化 | `src/serialization/Serializer.cpp`, `Dto.cpp` / `Dto.h` |
| 求值 | `include/MotionStudio/render/EvaluatedLayer.h`, `src/render/SceneEvaluator.cpp` |
| 指令 | `include/MotionStudio/render/DrawCommand.h`, `CommandBuilder.cpp`, `RenderAdapter.h/.cpp` |
| 适配器 | `adapter/tgfx/src/TgfxCanvasAdapter.mm` (+ 头文件) |
| 桥接/UI | `bridge/`, `apps/MotionStudioApp/` |
| 测试 | `tests/model/`, `tests/serialization/`, `tests/render/`, `adapter/tgfx/tests/` |

---

### Task 1: 扩展 Mask / TrackMatte 模型

**Files:**
- Modify: `include/MotionStudio/model/Layer.h`
- Create: `include/MotionStudio/model/TrackMatteType.h`
- Modify: `include/MotionStudio/model/MaskMode.h`（仅注释对齐即可）
- Test: `tests/model/LayerTest.cpp`（或新建 `MaskTest.cpp`）

**Interfaces:**
- Produces: `Mask::{path: Animatable<BezierPath>, feather, expansion}`；`Layer::{trackMatteLayerId, trackMatteType}`；`enum class TrackMatteType`

- [x] **Step 1:** 写失败测试：默认 Mask path 静态空路径、feather/expansion=0；Layer track matte 默认 None/invalid id
- [x] **Step 2:** 改 `Mask` / `Layer` 字段；新增 `TrackMatteType.h`
- [x] **Step 3:** 跑 `./build/tests/core_tests --gtest_filter='*Mask*:*Layer*'` 相关用例通过
- [x] **Step 4:** 更新 `docs/data-model.md` 中 Mask / Layer 片段（与实现一致）

---

### Task 2: 序列化 + DTO

**Files:**
- Modify: `src/serialization/Serializer.cpp`（`MaskToJson` / `MaskFromJson` / `LayerToJson`）
- Modify: `include/MotionStudio/serialization/Dto.h`, `src/serialization/Dto.cpp`
- Test: `tests/serialization/SerializerTest.cpp`

**Interfaces:**
- Consumes: Task 1 字段
- Produces: JSON `path` 为 Animatable 形态；`feather`/`expansion`；`trackMatteType` 字符串；`trackMatteLayerId`

- [x] **Step 1:** 失败测试：round-trip 含 animatable mask path、feather、track matte
- [x] **Step 2:** 实现 DTO + Serializer（无迁移分支）
- [x] **Step 3:** `ctest -R SerializerTest --output-on-failure` 通过

---

### Task 3: PropertyPath 解析 masks\[i\].\*

**Files:**
- Modify: `src/model/PropertyPath.cpp`
- Test: 现有 PropertyPath 测试或扩展

- [x] **Step 1:** 失败测试：解析并 resolve `masks[0].opacity|path|feather|expansion`
- [x] **Step 2:** 实现 resolve 分支
- [x] **Step 3:** 测试通过；确认 SetStaticValue / keyframe 命令可打到这些属性

---

### Task 4: EvaluatedLayer + SceneEvaluator

**Files:**
- Modify: `include/MotionStudio/render/EvaluatedLayer.h`
- Modify: `src/render/SceneEvaluator.cpp`（及内部辅助）
- Test: `tests/render/SceneEvaluatorTest.cpp`（或新建）

**Interfaces:**
- Produces: `EvaluatedMask`, `EvaluatedMatteKind`, `matteSourceId`, `usedAsMatteOnly`

- [x] **Step 1:** 失败测试：求值 masks 标量；matte 引用解析；自引用变 None；源层标记 usedAsMatteOnly
- [x] **Step 2:** 实现求值
- [x] **Step 3:** 测试通过

---

### Task 5: DrawCommand + CommandBuilder + PlayCommands

**Files:**
- Modify: `include/MotionStudio/render/DrawCommand.h`
- Create: `include/MotionStudio/render/MaskApplyMode.h`（或放进 DrawCommand.h）
- Modify: `include/MotionStudio/render/RenderAdapter.h`
- Modify: `src/render/RenderAdapter.cpp`
- Modify: `src/render/CommandBuilder.cpp`
- Test: `tests/render/CommandBuilderTest.cpp`

**Interfaces:**
- Produces: `BeginLayer/EndLayer/BeginMask/EndMask`；`RenderAdapter` 对应虚函数；`PlayCommands` 分发

- [x] **Step 1:** 失败测试：有 mask 的层发出 BeginLayer…BeginMask(PathCoverage)…EndMask…EndLayer；matte-only 源层不出现常规绘制
- [x] **Step 2:** 扩展命令与 builder；adapter 基类默认空实现或纯虚（tgfx 在 Task 6 实现）
- [x] **Step 3:** 测试通过；无 mask 层指令序列与现网兼容

---

### Task 6: tgfx 离屏 PathCoverage

**Files:**
- Modify: `adapter/tgfx/` 头文件 + `TgfxCanvasAdapter.mm`
- Test: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`（快照）

- [x] **Step 1:** 实现 BeginLayer/EndLayer 离屏栈
- [x] **Step 2:** PathCoverage：Add/Subtract/Intersect + inverted + opacity + expansion + feather
- [x] **Step 3:** 快照测试至少覆盖 Add、Subtract、feather>0
- [x] **Step 4:** 跑 tgfx_adapter_test 相关用例

---

### Task 7: Track Matte 端到端

**Files:**
- Modify: `CommandBuilder.cpp`（重放 matte 源 + 相对变换）
- Modify: `TgfxCanvasAdapter.mm`（Alpha/Luma/反相）
- Test: CommandBuilder + 快照

- [x] **Step 1:** 失败测试：Alpha matte 指令形态与相对变换
- [x] **Step 2:** 实现四种 matte 模式
- [x] **Step 3:** 快照通过；path masks ∩ matte 顺序正确

---

### Task 8: Bridge + Undo 命令

**Files:**
- Modify: `bridge/` 头/实现
- 新增或复用 undo 命令设 track matte、增删 mask
- Test: `tests` / bridge_test

- [x] **Step 1:** C API：add/remove/reorder mask；set track matte
- [x] **Step 2:** undo 覆盖
- [x] **Step 3:** bridge_test 通过

---

### Task 9: SwiftUI 编辑 UI

**Files:**
- Modify: `apps/MotionStudioApp/` Inspector、可选画布 mask 轮廓、时间轴属性行
- Modify: `MotionDocumentCore` facade

> **提交门禁：** 本 Task 改动等人工验证通过后再 git commit。

- [x] **Step 1:** Inspector Masks 段 + Track Matte 控件（本地未提交）
- [x] **Step 2:** 时间轴暴露 mask 可动画 path（本地未提交）
- [ ] **Step 3:** 手动验证：加 mask → 画布裁切 → undo（验证通过后再 commit）

---

### Task 10: 文档与可选快路径

**Files:**
- Modify: `docs/rendering.md`, `docs/data-model.md`
- Optional: CommandBuilder 硬边降级 ClipPath

- [x] **Step 1:** 文档与实现对齐
- [ ] **Step 2:**（可选）硬边快路径 + 测试证明与离屏视觉一致

---

## 执行顺序

严格按 Task 1 → 10。Core/适配器/测试/文档完成后可自动提交；UI（Task 9）人工验证后再提交。
