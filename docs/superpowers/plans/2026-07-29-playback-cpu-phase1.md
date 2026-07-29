# 播放 CPU 优化 Phase 1 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development 或 superpowers:executing-plans 按任务推进。步骤用 checkbox（`- [ ]`）跟踪。

**Goal:** 播放时按内容帧率刷新、整数帧求值、关闭编辑 chrome，降低持续播放 CPU。

**Architecture:** Bridge 增加 `MS_CANVAS_DRAW_MODE`；`PLAYBACK` 下跳过 selection/path/motion-path 命令构建。Swift 播放时对齐 `preferredFramesPerSecond` 到内容帧率，并走整数帧 draw API。本阶段**不实现** blit 跳过（设计允许）。

**Tech Stack:** C bridge、SwiftUI/MTKView、现有 `ms_canvas_draw_frame*`。

**Spec:** [2026-07-29-playback-cpu-optimization-design.md](../specs/2026-07-29-playback-cpu-optimization-design.md)

## Global Constraints

- 播放量化：`floor` 整数帧；暂停/scrub 仍用亚帧
- 播放隐藏 chrome；暂停恢复
- 不自动 push；完成后 commit，交用户手测

---

### Task 1: Bridge draw mode + revision API

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/MSCanvas.h`
- Modify: `bridge/src/common/motionstudio_bridge_canvas.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`

- [x] 添加 `MS_CANVAS_DRAW_MODE`、`ms_canvas_set_draw_mode`、`ms_canvas_get_draw_mode`、`ms_canvas_set_content_revision`、`ms_canvas_get_content_revision`
- [x] `PLAYBACK` 时不构建 chrome 命令
- [x] 测试：null 安全 API
- [x] Commit（随 Phase 1 一并提交）

### Task 2: Swift 播放路径

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Canvas/CanvasViewController.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（如需）

- [x] `configurePlayback`：`preferredFramesPerSecond = max(1, Int(frameRate.rounded()))`；`ms_canvas_set_draw_mode`
- [x] `draw(in:)`：播放用整数帧 + `set_content_revision`；暂停用亚帧
- [x] Commit（随 Phase 1 一并提交）

### Task 3: 交付手测

- [x] 跑 `bridge_test` 相关用例
- [x] 更新 design §1.4：注明 Phase 1 未含 blit
- [ ] 停交用户按 spec §1.6 手测
