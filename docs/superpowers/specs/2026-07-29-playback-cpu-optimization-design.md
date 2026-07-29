# Playback CPU Optimization Design

> Branch: `feature/0x1306a94_playback_cpu`  
> Related: [libpag-rendering-optimization-notes.md](../../libpag-rendering-optimization-notes.md)  
> Date: 2026-07-29  
> Process: implement Phase 1 → user manual verify → Phase 2 → verify → Phase 3 → verify

## Goals

Reduce CPU during continuous canvas playback, borrowing libpag ideas (quantize frames, skip unchanged work, avoid editor chrome on the hot path, then cache / snapshot).

Non-goals for this branch:

- tgfx DisplayList Partial/Tiled dirty regions
- Full AE-style `excludeVaryingRanges` across all properties
- Disk cache / video sequence pipeline

## Confirmed defaults

| Decision | Choice |
|---|---|
| Playback time sampling | Quantize to **content integer frames** (`floor`) |
| Scrub / paused preview | Keep existing sub-frame (`double` frameTime) APIs |
| Editor chrome while playing | **Do not build/draw** selection / path-edit / motion-path chrome |
| Delivery | Three phases; stop after each for manual verification |
| Placement | Phase 1 mostly bridge + Swift; Phase 2/3 add Core/adapter cache |

## Constraint: MTKView drawables

Each `MTKView` `draw(in:)` acquires a **new** drawable. Skipping `beginFrame`/`endFrame` without presenting previous pixels produces a blank frame. Therefore:

- “Skip” cannot mean “return without touching GPU” unless we blit a retained last-frame texture, **or** we simply do not get a draw callback for that display tick.
- Phase 1 primary lever is **align display callback rate with content frame rate** + integer evaluation, so most callbacks do real unique work at content fps instead of 60× sub-frame full pipelines.
- True “same key → blit last frame” is Phase 1 optional stretch if rate alignment alone is insufficient; Phase 2 command reuse still runs a cheap present path.

---

## Phase 1 — P0: Quantize, rate align, chrome off, cheap skip

### 1.1 Behavior

**Playing**

1. `preferredFramesPerSecond = max(1, Int(frameRate.rounded()))` (no longer force up to 60).
2. Canvas draw uses **integer** frame: `Int64(floor(previewFrame))` via `ms_canvas_draw_frame` / profiled int API (not `*_at_time` with fractional time).
3. Editor chrome omitted on the draw path (selection outline, path overlays from edit target, motion-path chrome). Custom debug overlays policy: also off while playing (simplest).
4. Bridge records last draw key; if key matches **and** a retained last-frame image exists, blit + present only (no evaluate/build/play of scene). If no retained image yet, full draw once and retain.

**Paused / scrubbing**

- Unchanged: sub-frame `drawFrame(…, frameTime:)`, chrome on, `enableSetNeedsDisplay` driven redraws.

### 1.2 API / interface sketch

```c
// motionstudio_bridge.h

typedef CF_CLOSED_ENUM(int, MS_CANVAS_DRAW_MODE) {
    MS_CANVAS_DRAW_MODE_EDIT = 0,      // chrome on, caller may pass fractional time
    MS_CANVAS_DRAW_MODE_PLAYBACK = 1,  // chrome off; prefer integer frame APIs
};

void ms_canvas_set_draw_mode(MSCanvas *canvas, MS_CANVAS_DRAW_MODE mode);

// Content generation from Swift @Observable revision (bridge has no revision today).
void ms_canvas_set_content_revision(MSCanvas *canvas, uint64_t revision);
```

`MSCanvas` adds:

```cpp
MS_CANVAS_DRAW_MODE drawMode = MS_CANVAS_DRAW_MODE_EDIT;
uint64_t contentRevision = 0;

struct LastDrawKey {
    uint64_t compositionId;
    int64_t frame;           // quantized
    uint64_t contentRevision;
    float zoom, panX, panY;
    int backdrop;
    int viewportW, viewportH;
    MS_CANVAS_DRAW_MODE mode;
};
LastDrawKey lastDrawKey{};
bool hasLastDrawKey = false;
// Retained by adapter or canvas: last presented snapshot for blit-skip (Phase 1.4).
```

### 1.3 Pseudocode

```
Swift configurePlayback(playing):
  metalView.preferredFramesPerSecond =
      playing ? max(1, Int(frameRate.rounded())) : max(1, Int(frameRate.rounded()))
  ms_canvas_set_draw_mode(playback | edit)
  ms_canvas_set_content_revision(core.revision)

Swift draw(in:):
  ms_canvas_set_content_revision(core.revision)
  if playing:
      ms_canvas_draw_frame_profiled(canvas, doc, compId, floor(previewFrame), &profile)
  else:
      ms_canvas_draw_frame_at_time_profiled(..., previewFrame, &profile)

Bridge ms_canvas_draw_frame_at_time_profiled / int entry:
  key = MakeKey(...)
  if drawMode == PLAYBACK:
      suppress chrome builders
  if hasLastDrawKey && key == lastDrawKey && adapter->blitLastFrameIfPossible():
      profile.drewFrame = false  // or true with "blitted" flag — use drewFrame=false, add skippedDuplicate
      return
  EvaluatePreview(time)  // int path: PreviewTime(frame)
  BuildCommands(scene) only
  if !PLAYBACK: build chrome commands
  begin/play/end
  adapter->retainLastFrameForBlit()
  lastDrawKey = key
```

### 1.4 Blit-skip (minimal)

Prefer implementing blit via existing tgfx surface snapshot if cheap in `TgfxCanvasAdapter`; if that proves invasive in Phase 1, **ship without blit** and rely on rate alignment + chrome off + integer frames only. Document in Phase 1 PR which of the two landed.

Acceptance without blit is still a large win: 60× sub-frame → ~content-fps integer frames, no chrome.

### 1.5 Files (expected)

- `bridge/include/motionstudio_bridge.h`
- `bridge/src/common/MSCanvas.h`
- `bridge/src/common/motionstudio_bridge_canvas.cpp`
- `bridge/tests/BridgeTest.cpp` (draw mode chrome suppressed; revision/frame skip if blit present)
- `apps/.../CanvasViewController.swift`
- `apps/.../MotionDocumentCore.swift` (thin wrappers if needed)
- Optionally `adapter/tgfx/...` for blit retain

### 1.6 Manual verify checklist (user)

- [ ] Play a simple animated comp: playhead advances, motion looks stepped at content fps (acceptable).
- [ ] Activity Monitor / Instruments: CPU clearly lower vs `develop` on same file.
- [ ] Pause: selection handles / motion path chrome return.
- [ ] Scrub timeline: sub-frame smoothness unchanged if previously present.
- [ ] Resize / zoom / pan during play: canvas updates correctly (key invalidates).
- [ ] Undo during pause then play: content not stale.

### 1.7 Tests

- Bridge: `PLAYBACK` mode does not emit selection/motion-path commands (inspect profile `drawCommandCount` or test hook).
- Bridge: two draws same key → second reports `drewFrame == false` when blit path exists; otherwise document skip as Swift-rate-only.

---

## Phase 2 — P1: Cross-frame SceneState / DrawCommand reuse

### 2.1 Behavior

After Phase 1, when a quantized frame is drawn again (loop, scrub back, duplicate callback):

- Reuse last `DrawCommandList` (and optionally `SceneState`) for that key instead of evaluate+build.
- Still `PlayCommands` + present (unless Phase 1 blit handles identical key).

Optional stretch: **hold-segment mapping** — if all sampled animatables are static between floor frames in a range, map to one cache key (simple hold detection only; not full libpag static ranges).

### 2.2 Interface sketch

```cpp
// Core or bridge-owned helper
class FrameCommandCache {
  struct Key { uint64_t compositionId; int64_t frame; uint64_t revision; /* view not in key for scene */ };
  struct Entry { SceneState state; DrawCommandList commands; };
  std::optional<Entry> find(const Key&);
  void put(Key, Entry);
  void clear(); // on revision change
};
```

View transform stays outside the scene cache (applied in adapter beginFrame). Chrome remains mode-gated from Phase 1.

### 2.3 Manual verify

- [ ] Looping playback: CPU stable/lower on second loop.
- [ ] Edit layer mid-pause, play: no stale frames.
- [ ] Precomp / nested time still correct.

---

## Phase 3 — P2: Static layer snapshots + image prepare

### 3.1 Behavior

- Layers (or whole frames) unchanged for N playback frames → rasterize to GPU texture at `cacheScale`, draw with `drawImage`.
- LRU + memory cap (start ~20MB echo of libpag; tune later).
- Invalidate on revision, zoom scale change beyond threshold, or layer bounds change.
- Image layers (when present): async `prepare` decode before first use (only if image layers exist in tree).

### 3.2 Manual verify

- [ ] Complex static shapes / masks: CPU drop, visual parity at 100% zoom.
- [ ] Zoom in after snapshot: no prolonged blur (invalidate / rescale).
- [ ] Memory does not climb without bound over long play.

---

## Rollout & git

| Step | Action |
|---|---|
| 0 | Branch `feature/0x1306a94_playback_cpu` from `develop` |
| 1 | Land this design + analysis notes |
| 2 | Implement Phase 1 → commit → **user verifies** |
| 3 | Implement Phase 2 → commit → **user verifies** |
| 4 | Implement Phase 3 → commit → **user verifies** |
| 5 | PR to `develop` when all phases accepted |

No push unless user asks.

## Risk notes

- Integer-frame play looks less smooth on 60Hz displays for slow moves — accepted for Phase 1; optional 2× rate can be a later flag.
- Hiding chrome while playing may surprise if user expects live handles — accepted; pause restores.
- Blit path complexity: droppable in Phase 1 if adapter work balloons.

## Success metrics

- Phase 1: playback CPU ≪ current on ProMotion (target: roughly proportional to content fps / 60 for the evaluate+build portion).
- Phase 2: second loop / repeated frames show near-zero evaluate+build time in `MSCanvasFrameProfile`.
- Phase 3: static-heavy comps show play path dominated by present, not path tessellation.
