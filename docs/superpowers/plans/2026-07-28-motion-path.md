# Phase B：Motion Path 实现计划

> **For agentic workers:** 按任务逐步实现；步骤用 `- [ ]` 跟踪。

**目标：** Position 空间运动路径 — B1 API+单测，再 B2 画布可视与手柄。

**Spec：** `docs/superpowers/specs/2026-07-28-motion-path-design.md`

## 全局约束

- 分支：`feature/0x1306a94_path_animation`
- Core/Bridge 测通自动 commit；App 等人机验证
- Commit：英语、≤120 字符、句号结尾

---

### Task 1：BuildMotionPath + 单测

**Files:**
- Create: `include/MotionStudio/animation/MotionPath.h`
- Create: `src/animation/MotionPath.cpp`
- Create: `tests/animation/MotionPathTest.cpp`

- [x] 实现 `BuildMotionPath`：≥2 KF；双边 spatial 写切线，否则零切线直线段
- [x] 测试直线 / 弧线 / 单 KF 空路径
- [x] Commit: `Add BuildMotionPath for position spatial arcs.`

---

### Task 2：SetSpatialTangentsCommand

**Files:**
- Modify: `include/MotionStudio/undo/CommandKind.h`
- Create: `include/MotionStudio/undo/SetSpatialTangentsCommand.h`
- Create: `src/undo/SetSpatialTangentsCommand.cpp`
- Modify: `src/undo/CommandHelpers.h/.cpp`（`ApplySpatialTangentsVec2`）
- Modify: `tests/undo/CommandsTest.cpp`

- [x] 仅 Vec2；无关键帧 no-op；undo 恢复；merge 同 property+time
- [x] Commit: `Add SetSpatialTangentsCommand for Vec2 keyframes.`

---

### Task 3：Bridge API + 单测

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_property.cpp`（get）
- Modify: `bridge/src/common/motionstudio_bridge_commands.cpp`（set）
- Modify: `bridge/tests/BridgeTest.cpp`

- [x] `ms_property_keyframe_spatial_at` / `ms_command_set_spatial_tangents` / `ms_property_build_motion_path`
- [x] 测 round-trip；roadmap Phase B Core → `core-done`
- [x] Commit: `Expose motion path spatial tangents over the bridge.`

---

### Task 4（B2）：MotionPathChrome Core

**Files:**
- Create: `include/MotionStudio/render/MotionPathChrome.h`
- Create: `src/render/MotionPathChrome.cpp`
- Create: `tests/render/MotionPathChromeTest.cpp`

- [ ] `BuildMotionPathChrome`：Document 取 position；父 world；≥2 KF；预览手柄 = 邻段 (Δ)/3
- [ ] `HitTestMotionPath`：选中 KF 时 In/Out > Keyframe
- [ ] `BuildMotionPathCommands`：复用 PathOverlay 描边 + KF 方块 + 切线圆（零长也画预览）
- [ ] 单测 + Commit: `Add MotionPathChrome for position path editing.`

### Task 5（B2）：Bridge 画布

**Files:**
- Modify: `bridge/.../MSCanvas.h`、`motionstudio_bridge.h`、`motionstudio_bridge_canvas.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`（可选 smoke）

- [ ] `ms_canvas_set_motion_path_selection`；draw 时合并 MotionPath 命令（非 path-edit 时）
- [ ] `ms_canvas_hit_motion_path`
- [ ] App 用已有 `ms_command_set_spatial_tangents`；拖拽时若对端缺手柄由 Core helper 补默认
- [ ] Commit: `Wire motion path chrome into the canvas bridge.`

### Task 6（B2）：App Select 工具

**Files:**
- Modify: `CanvasViewController.swift`、必要时 `MotionDocumentCore.swift`

- [ ] Select 工具：hit KF 选中；拖 In/Out 写 spatial（merge group）
- [ ] Pen 工具不显示/不 hit 运动路径
- [ ] roadmap → `ui-pending-verify`；Commit: `Add motion path handle editing in select tool.`
