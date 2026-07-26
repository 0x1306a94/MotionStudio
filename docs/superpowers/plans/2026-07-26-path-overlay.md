# Path Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generic path overlay stroke chrome; first consumer shows selected layers' mask paths.

**Architecture:** Core `PathOverlayItem` + `BuildPathOverlayCommands` + `CollectMaskPathOverlays`; canvas merges mask overlays with optional extra list and plays after scene content (same as selection chrome).

**Tech Stack:** C++17 core, Apple canvas bridge (ObjC++), GoogleTest.

## Global Constraints

- Preview-only; not serialized.
- Mask display uses evaluated path (ignore expansion/feather for stroke).
- Auto-commit core/tests/docs; no Swift required for v1 mask display.

---

## File Map

| File | Role |
|---|---|
| `include/MotionStudio/render/PathOverlay.h` | Item + APIs |
| `src/render/PathOverlay.cpp` | Build + Collect |
| `tests/render/PathOverlayTest.cpp` | Unit tests |
| `bridge/src/apple/motionstudio_bridge_canvas.mm` | Draw merge |
| `bridge/include/motionstudio_bridge.h` + cpp/mm | Optional `ms_canvas_set_path_overlays` |
| `docs/superpowers/specs/2026-07-26-path-overlay-design.md` | Spec |

---

### Task 1: Core PathOverlay + tests

- [x] Add `PathOverlay.h` / `.cpp` with `BuildPathOverlayCommands` and `CollectMaskPathOverlays`
- [x] Add `PathOverlayTest.cpp`
- [x] `cmake --build build && ./build/tests/core_tests --gtest_filter='PathOverlayTest.*'`
- [x] Commit

### Task 2: Wire canvas

- [x] Canvas: CollectMaskPathOverlays → BuildPathOverlayCommands; play after content, before selection chrome
- [x] `ms_canvas_set_path_overlays` deferred to pen tool (needs BezierPath C ABI)
- [x] Rebuild / smoke Xcode build
- [x] Commit + update layer-masks design note
