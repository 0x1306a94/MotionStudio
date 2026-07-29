# 播放预览 CPU 优化设计

> 分支：`feature/0x1306a94_playback_cpu`  
> 相关文档：[libpag-rendering-optimization-notes.md](../../libpag-rendering-optimization-notes.md)  
> 日期：2026-07-29  
> 流程：Phase 1 → 手测 → Phase 2 → 手测；**Phase 3 不做**（见下文）

## 目标

降低画布持续播放时的 CPU，借鉴 libpag：量化帧、跳过未变化工作、热路径去掉编辑 chrome，再做命令级缓存。

本分支不做：

- tgfx DisplayList 的 Partial / Tiled 脏区绘制
- 完整 AE 式全属性 `excludeVaryingRanges`
- 磁盘缓存 / 视频序列管线
- **整帧 / 静态层 GPU Snapshot**（原 Phase 3，已取消）

## 已确认默认取舍

| 决策 | 选择 |
|---|---|
| 播放时时间采样 | 量化到**内容整数帧**（`floor`） |
| 拖拽时间轴 / 暂停预览 | 保留现有亚帧（`double` frameTime）API |
| 播放时编辑 chrome | **不构建、不绘制**选中框 / path-edit / motion-path |
| 交付方式 | Phase 1 + Phase 2；每阶段结束后停手测 |
| 改动落点 | Phase 1：bridge + Swift；Phase 2：bridge `FrameCommandCache` |
| Phase 3 Snapshot | **取消**（见 §Phase 3） |

## 约束：MTKView drawable

每次 `MTKView` 的 `draw(in:)` 都会拿到**新的** drawable。若跳过 `beginFrame`/`endFrame` 又不提交上一帧像素，会黑屏。因此：

- 「跳过」不能简单等于「完全不碰 GPU」，除非 blit 保留的上一帧纹理，或本显示周期根本不进 draw 回调。
- Phase 1 的主杠杆是：**显示回调帧率对齐内容帧率** + 整数帧求值，避免 60× 亚帧全量管线。
- 「同 key → blit 上一帧」作为 Phase 1 可选加强；若仅靠帧率对齐已够用可暂缓。Phase 2 的命令复用仍会走一次轻量 present。

---

## Phase 1 — P0：量化帧、对齐刷新、关掉 chrome、廉价跳过

### 1.1 行为

**播放中**

1. `preferredFramesPerSecond = max(1, Int(frameRate.rounded()))`（不再抬到 60）。
2. 画布绘制使用**整数帧**：`Int64(floor(previewFrame))`，走 `ms_canvas_draw_frame` / 带 profile 的 int API（不用带小数的 `*_at_time`）。
3. 绘制路径省略编辑 chrome（选中描边、path 编辑叠层、motion-path）。自定义 debug overlay：播放时一并关闭（最简）。
4. Bridge 记录上次绘制 key；若 key 相同且已有保留的上一帧图像，则只 blit + present（不做场景 evaluate/build/play）。若尚无保留图，则完整画一次并保留。

**暂停 / 拖拽时间轴**

- 行为不变：亚帧 `drawFrame(…, frameTime:)`、chrome 开启、由 `enableSetNeedsDisplay` 驱动重绘。

### 1.2 API / 接口草案

```c
// motionstudio_bridge.h

typedef CF_CLOSED_ENUM(int, MS_CANVAS_DRAW_MODE) {
    MS_CANVAS_DRAW_MODE_EDIT = 0,      // 开启 chrome；调用方可传小数时间
    MS_CANVAS_DRAW_MODE_PLAYBACK = 1,  // 关闭 chrome；优先走整数帧 API
};

void ms_canvas_set_draw_mode(MSCanvas *canvas, MS_CANVAS_DRAW_MODE mode);

// 内容世代：来自 Swift @Observable 的 revision（bridge 目前没有 revision）。
void ms_canvas_set_content_revision(MSCanvas *canvas, uint64_t revision);
```

`MSCanvas` 新增：

```cpp
MS_CANVAS_DRAW_MODE drawMode = MS_CANVAS_DRAW_MODE_EDIT;
uint64_t contentRevision = 0;

struct LastDrawKey {
    uint64_t compositionId;
    int64_t frame;           // 已量化
    uint64_t contentRevision;
    float zoom, panX, panY;
    int backdrop;
    int viewportW, viewportH;
    MS_CANVAS_DRAW_MODE mode;
};
LastDrawKey lastDrawKey{};
bool hasLastDrawKey = false;
// 由 adapter 或 canvas 持有：上一帧快照，供 blit 跳过（见 1.4）。
```

### 1.3 伪代码

```
Swift configurePlayback(playing):
  metalView.preferredFramesPerSecond = max(1, Int(frameRate.rounded()))
  ms_canvas_set_draw_mode(playing ? PLAYBACK : EDIT)
  ms_canvas_set_content_revision(core.revision)

Swift draw(in:):
  ms_canvas_set_content_revision(core.revision)
  if playing:
      ms_canvas_draw_frame_profiled(canvas, doc, compId, floor(previewFrame), &profile)
  else:
      ms_canvas_draw_frame_at_time_profiled(..., previewFrame, &profile)

Bridge 整数帧 / at_time 入口:
  key = MakeKey(...)
  if drawMode == PLAYBACK:
      抑制 chrome 构建
  if hasLastDrawKey && key == lastDrawKey && adapter->blitLastFrameIfPossible():
      profile.drewFrame = false  // 表示跳过了完整场景绘制
      return
  EvaluatePreview(time)  // 整数路径：PreviewTime(frame)
  BuildCommands(scene)
  if !PLAYBACK: 构建 chrome 命令
  begin / play / end
  adapter->retainLastFrameForBlit()
  lastDrawKey = key
```

### 1.4 Blit 跳过（最小实现）

优先在 `TgfxCanvasAdapter` 用现有 tgfx surface snapshot 做 blit；若 Phase 1 侵入过大，则**不带 blit 先交付**，只靠帧率对齐 + 关 chrome + 整数帧。Phase 1 提交说明里写清落地的是哪一种。

无 blit 也可接受：从约 60× 亚帧降到约内容帧率整数帧，且无 chrome，收益已经很大。

**Phase 1 落地结果（2026-07-29）：未实现 blit。** 交付内容为内容帧率对齐 + 整数帧求值 + `PLAYBACK` 关闭 chrome；`content_revision` API 已预留供 Phase 2 使用。

### 1.5 预期改动文件

- `bridge/include/motionstudio_bridge.h`
- `bridge/src/common/MSCanvas.h`
- `bridge/src/common/motionstudio_bridge_canvas.cpp`
- `bridge/tests/BridgeTest.cpp`（draw mode 抑制 chrome；若有 blit 则测 revision/帧跳过）
- `apps/.../CanvasViewController.swift`
- `apps/.../MotionDocumentCore.swift`（如需薄封装）
- 可选：`adapter/tgfx/...`（blit 保留）

### 1.6 手测清单（用户）

- [ ] 播放简单动画合成：播放头前进，运动呈内容帧率步进（可接受）。
- [ ] Activity Monitor / Instruments：同文件下 CPU 明显低于 `develop`。
- [ ] 暂停后：选中手柄 / motion path chrome 恢复。
- [ ] 拖拽时间轴：亚帧观感与改前一致（若原先就有）。
- [ ] 播放中缩放 / 平移 / 改窗口：画面正确更新（key 失效）。
- [ ] 暂停时 undo 再播放：内容不陈旧。

### 1.7 测试

- Bridge：`PLAYBACK` 模式不产生选中 / motion-path 命令（查 profile 的 `drawCommandCount` 或测试钩子）。
- Bridge：同一 key 连续画两次 → 第二次在有 blit 时 `drewFrame == false`；若无 blit，则在说明里写明仅依赖 Swift 侧帧率对齐。

---

## Phase 2 — P1：跨帧复用 SceneState / DrawCommand

### 2.1 行为

Phase 1 之后，同一量化帧再次绘制时（循环、回拖、重复回调）：

- 按 key 复用上次的 `DrawCommandList`（可选连同 `SceneState`），跳过 evaluate+build。
- 仍执行 `PlayCommands` + present（若 Phase 1 blit 已处理同 key，则可继续走 blit）。

可选加强：**hold 段映射**——若区间内采样到的 animatable 皆静态，多帧映射同一 cache key（仅简单 hold 检测，不做完整 libpag 静帧区间）。

**Phase 2 落地结果（2026-07-29）：** `FrameCommandCache`（LRU≤256）仅在 `PLAYBACK` 下按整数帧缓存场景命令；revision/composition 变化清空。未做 hold 段映射。

### 2.2 接口草案

```cpp
// Core 或 bridge 持有的辅助类
class FrameCommandCache {
  struct Key { uint64_t compositionId; int64_t frame; uint64_t revision; /* 场景 key 不含视图变换 */ };
  struct Entry { SceneState state; DrawCommandList commands; };
  std::optional<Entry> find(const Key&);
  void put(Key, Entry);
  void clear(); // revision 变化时清空
};
```

视图变换仍在 adapter `beginFrame` 应用，不进场景缓存。Chrome 继续由 Phase 1 的 draw mode 控制。

### 2.3 手测

- [ ] 循环播放：第二圈 CPU 更稳 / 更低。
- [ ] 暂停中改图层再播放：无残帧。
- [ ] Precomp / 嵌套时间仍正确。

---

## Phase 3 — 已取消：GPU Snapshot

原计划：静态层（或整帧）栅格化为 GPU 纹理，跳过 `PlayCommands` / path 三角化；图片异步 `prepare`。

**本分支不做。** 试做整帧 drawable 空间 snapshot 后确认不划算：

1. **显存装不下循环。** Retina 全窗口单帧常 12–20MB+；约 24MB LRU 往往只能留 1 帧，正常长度动画循环几乎无法命中（手测 `snap hit ≈ 0%`，仍走 `PlayCommands`）。
2. **Metal drawable 限制。** CAMetalLayer 为 `framebufferOnly`，不能直接 `makeImageSnapshot`；离屏 capture + blit 使 miss 路径比 Phase 2 更贵。
3. **Phase 1/2 已覆盖主瓶颈。** Instruments 下 evaluate/build 可忽略；剩余热点含 SwiftUI（另议）与 play/三角化。整帧缓存救不了「每帧内容都在变」的长动画。

若将来再做，应另开设计，优先考虑**合成分辨率**或 **per-layer 静态层** snapshot，并单独定显存预算；不要再做全 drawable 整帧 LRU。SwiftUI 时间轴刷新成本单独处理。

---

## 落地与 Git

| 步骤 | 动作 |
|---|---|
| 0 | 从 `develop` 拉出 `feature/0x1306a94_playback_cpu` |
| 1 | 合入本设计与分析笔记 |
| 2 | 实现 Phase 1 → commit → **用户手测** |
| 3 | 实现 Phase 2 → commit → **用户手测** |
| 4 | （已取消）Phase 3 |
| 5 | Phase 1/2 通过后向 `develop` 开 PR |

除非用户要求，不自动 push。

## 风险

- 整数帧播放在 60Hz 屏上慢动作会有步进感——Phase 1 接受；若需更顺可后续加 2× 帧率开关。
- 播放时隐藏 chrome，若用户期望实时手柄会不习惯——接受；暂停即恢复。
- Blit 路径若过重，Phase 1 可砍掉（落地时未做 blit）。

## 成功指标

- Phase 1：ProMotion 上播放 CPU 明显低于现状（evaluate+build 大致随 内容帧率/60 下降）。
- Phase 2：第二圈 / 重复帧在 `MSCanvasFrameProfile` 中 evaluate+build 接近 0。
