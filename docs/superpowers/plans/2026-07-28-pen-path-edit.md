# Pen Path Edit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Full pen tool for ShapePath and Mask path editing (create, insert/delete vertices, drag vertices/tangents).

**Architecture:** Core `PathGeometryEdit` + `PathEditHandles` chrome; Bridge BezierPath ABI; Swift tool mode/gestures. Authoring model stays `BezierPath` (not SVG verbs).

**Tech Stack:** C++17 core, Apple C bridge, Swift/UIKit app, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-07-28-pen-path-edit-design.md`（含实现中修订的手势 / 锚点 / 工具栏约定）

## Global Constraints

- Branch: `feature/0x1306a94_pen_path` (never commit on master/develop without branch).
- C++/Bridge/tests: auto-commit after each task when tests pass.
- App UI (Swift): implement but **do not commit** until human verifies in the running app.
- Commit messages: English, ≤120 chars, end with period, no other punctuation mid-sentence.

---

## File Map

| File | Role |
|---|---|
| `include/MotionStudio/common/PathGeometryEdit.h` | Pure BezierPath edit ops |
| `src/common/PathGeometryEdit.cpp` | Implementation |
| `tests/common/PathGeometryEditTest.cpp` | Unit tests |
| `include/MotionStudio/render/PathEditHandles.h` | Chrome + hit |
| `src/render/PathEditHandles.cpp` | Build / hit / draw |
| `tests/render/PathEditHandlesTest.cpp` | Unit tests |
| `include/MotionStudio/undo/ConvertGeometryToPathCommand.h` | Rect/Ellipse → Path |
| `src/undo/ConvertGeometryToPathCommand.cpp` | Command |
| `tests/undo/CommandsTest.cpp` (extend) | Convert tests |
| `bridge/include/motionstudio_bridge.h` | BezierPath + canvas APIs |
| `bridge/src/common/motionstudio_bridge.cpp` | ABI impl |
| `bridge/src/apple/motionstudio_bridge_canvas.mm` | Path-edit chrome |
| `bridge/tests/BridgeTest.cpp` | ABI tests |
| App Swift files | Tool UI — manual verify before commit |

---

### Task 0: Docs + branch

- [x] Branch `feature/0x1306a94_pen_path`
- [x] Spec + this plan updated with post-impl UX revisions

---

### Task 1: PathGeometryEdit

- [x] `MoveVertex` / tangents / insert / remove / close / append + tests
- [x] `ToggleVertexSmooth` + `RecenterPath` + tests
- [x] Commit(s)

---

### Task 2: PathEditHandles

- [x] Build / hit / draw + tests
- [x] Exclusive vertex hit zone over segment; tangent stroke color; selected vertex fill
- [x] Commit(s)

---

### Task 3: ConvertGeometryToPathCommand

- [x] Bake Rect/Ellipse; undo; Path no-op
- [x] Commit

---

### Task 4: Bridge BezierPath ABI

- [x] MSBezierPath round-trip; convert; add_path_layer
- [x] `path_edit_*` scene commands; `toggle_smooth`; close/recenter_shape
- [x] Commit(s)

---

### Task 5: Canvas path-edit chrome

- [x] Path-edit chrome after mask overlays; skip selection when editing
- [x] Hit radii (~8pt vertex / ~6pt segment); single-vertex skip stroke
- [x] Commit(s)

---

### Task 6: App UI (no auto-commit until human OK)

- [x] Pen tool mode, target sync, gestures, writeback（工作区未提交）
- [x] creationToolbar: Select → Pen → Rect → Ellipse → Image；无 title；选中同心圆角；钢笔态禁用创建按钮
- [x] 钢笔：零延时 press 选点/拖；双击顶点 toggle smooth；CloseRing 可拖为顶点 0
- [x] 选择态双击单选层 → Shape 钢笔（含 Rect/Ellipse convert）；无选中才双击居中；Mask 仅 Inspector
- [ ] Human App verification
- [ ] Commit App Swift only after user confirms

---

## Follow-ups / known product notes

- Append 顶点默认角点（零切线）；要出手柄需双击变平滑，或点边插点（de Casteljau 可带切线）。
- Mask 编辑入口刻意收窄为 Inspector，避免与选择态双击 Shape 冲突。
- 未实现：加点时按下拖出切线（经典钢笔）；若需要另开任务。
