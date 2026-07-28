# Pen Path Edit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Full pen tool for ShapePath and Mask path editing (create, insert/delete vertices, drag vertices/tangents).

**Architecture:** Core `PathGeometryEdit` + `PathEditHandles` chrome; Bridge BezierPath ABI; Swift tool mode/gestures. Authoring model stays `BezierPath` (not SVG verbs).

**Tech Stack:** C++17 core, Apple C bridge, Swift/UIKit app, GoogleTest.

## Global Constraints

- Branch: `feature/0x1306a94_pen_path` (never commit on master/develop without branch).
- C++/Bridge/tests: auto-commit after each task when tests pass.
- App UI (Swift): implement but **do not commit** until human verifies in the running app.
- Commit messages: English, ≤120 chars, end with period, no other punctuation mid-sentence.
- Spec: `docs/superpowers/specs/2026-07-28-pen-path-edit-design.md`

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
- [ ] Spec + this plan committed

---

### Task 1: PathGeometryEdit

**Files:**
- Create: `include/MotionStudio/common/PathGeometryEdit.h`
- Create: `src/common/PathGeometryEdit.cpp`
- Create: `tests/common/PathGeometryEditTest.cpp`

**Interfaces:**
- Produces: `MoveVertex`, `MoveInTangent`, `MoveOutTangent`, `InsertVertexOnSegment`, `RemoveVertex`, `ClosePath`, `AppendVertex`

- [ ] Write failing tests for move / mirror / insert / remove / close
- [ ] Implement
- [ ] `./build/tests/core_tests --gtest_filter='PathGeometryEditTest.*'`
- [ ] Commit

---

### Task 2: PathEditHandles

**Files:**
- Create: `include/MotionStudio/render/PathEditHandles.h`
- Create: `src/render/PathEditHandles.cpp`
- Create: `tests/render/PathEditHandlesTest.cpp`

- [ ] Tests: empty invalid; hit priority; BuildPathEditCommands non-empty
- [ ] Implement using PathOverlay stroke + handle markers
- [ ] Commit

---

### Task 3: ConvertGeometryToPathCommand

**Files:**
- Create command header/cpp
- Extend `CommandKind`, register in CMake if needed (glob)
- Tests in `tests/undo/`

- [ ] Bake Rect/Ellipse at frame; undo restores; Path no-op
- [ ] Commit

---

### Task 4: Bridge BezierPath ABI

**Files:**
- `motionstudio_bridge.h` / `.cpp`
- `BridgeTest.cpp`

- [ ] MSBezierPath round-trip static + keyframe
- [ ] convert + add_path_layer
- [ ] Commit

---

### Task 5: Canvas path-edit chrome

**Files:**
- `motionstudio_bridge_canvas.mm`
- bridge header for set_path_edit_target / hit / set_path_overlays

- [ ] Merge PathEdit commands after mask overlays; skip selection when editing
- [ ] Commit

---

### Task 6: App UI (no auto-commit)

**Files:**
- `EditorState`, `CanvasViewController`, `MasksInspector`, toolbar

- [ ] Pen tool mode, target sync, gestures, writeback
- [ ] Stop for human App verification
- [ ] Commit only after user confirms
