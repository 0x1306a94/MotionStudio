# PreviewSceneCache：求值与 DrawCommand 两级缓存设计

> 日期：2026-08-12  
> 相关：[2026-07-29-playback-cpu-optimization-design.md](./2026-07-29-playback-cpu-optimization-design.md)（Phase 2 仅在 PLAYBACK 缓存 `DrawCommandList`）  
> 状态：设计已确认，待实现

## 目标

静止 playhead / 同内容世代下：

- **draw / hit-test / bounds / handles** 复用同一次 `EvaluatePreview` + `ResolvePointText`（`SceneState`）。
- **仅 draw** 复用 `BuildCommands` 结果（`DrawCommandList`）；hit-test 不触发 build。

同时：

- 两级缓存都不再限定 `MS_CANVAS_DRAW_MODE_PLAYBACK`（EDIT 同样受益）。
- 播放中 App **直接跳过 hit-test**（不进入 bridge 求值）。

## 问题

| 路径 | 现状 |
|---|---|
| 播放 draw | `FrameCommandCache` 在 canvas 上，仅 PLAYBACK；只存 `DrawCommandList`，丢弃 `SceneState` |
| 编辑 draw | 每次 `EvaluatePreview` + `BuildCommands` + chrome |
| hit-test / bounds / handles | 每次独立 `EvaluatePreview`，与 draw 不共享 |

`FrameCommandCache` 用 `floor(frameTime)` 做 key，同一整数帧内不同小数时间可能错误复用。

## 已确认决策

| 决策 | 选择 |
|---|---|
| 优化范围 | 静止 playhead 复用；不做增量/局部求值 |
| 缓存形态 | **两级多帧 LRU** |
| `SceneState` | 挂 **`MSDocument`**（`PreviewSceneCache`） |
| `DrawCommandList` | 仍挂 **`MSCanvas`**（`FrameCommandCache`），仅渲染路径写入/读取 |
| 条目拆分 | hit-test **不** `BuildCommands`；draw miss 时再 build 并填入 FrameCommandCache |
| 时间键 | 两级均 `bit_cast<uint64_t>(PreviewTime)` 精确匹配 |
| 失效 | 共享 `MSDocument::contentRevision` + `compositionId`；变化 → 各自 `clear` |
| revision 源 | Swift `MotionDocumentCore.revision` 推到 Document；**不**在每次 evaluate 时 atomic++ |
| 播放 hit-test | App 侧 `guard !isPlaying`（tap / double-tap / `hitTestLayer`） |
| Core | 不在 `SceneEvaluator` 内加缓存 |

不采用「求值成功就 atomic++」：同 `(comp, contentRevision, time)` 的逻辑快照应稳定；用**内容世代**即可把 Scene / Command 两级绑在一起。

## 架构

```
MSDocument
  ├── mutex / Document / UndoManager
  ├── contentRevision                 // 内容世代（Swift 推送）
  └── PreviewSceneCache               // LRU：SceneState only
        key: bit pattern of PreviewTime
        scope: (compositionId, contentRevision)

MSCanvas
  ├── drawMode / adapter / chrome 输入
  └── FrameCommandCache               // LRU：scene DrawCommandList only
        key: bit pattern of PreviewTime
        scope: (compositionId, document->contentRevision)
        （移除 MSCanvas::contentRevision；失效读 Document）
```

语义：

- `SceneState` ≈ tgfx lazy bounds：交互与绘制共用。
- `DrawCommandList` 只服务 `PlayCommands`，由 draw 路径按需构建。

## 接口

### PreviewSceneCache（Document）

`SceneState` 体积大（图层路径 / network 等）。缓存用 `unique_ptr<Entry>` 持有，避免 `unordered_map` rehash / LRU 调整时深搬；`put` 吃所有权，返回借出的裸指针（仅当前 `DocumentLock` 内有效）。

```cpp
// bridge/src/common/PreviewSceneCache.h
namespace motionstudio {

class PreviewSceneCache {
 public:
  struct Entry {
    motion::PreviewTime time = 0;
    motion::SceneState state;  // ResolvePointText 之后
  };

  void clear();
  void invalidateIfStale(uint64_t compositionId, uint64_t revision);

  const Entry *find(motion::PreviewTime time) const;
  // 接管 entry；同 key 覆盖旧项。返回缓存内指针，勿 delete / 勿在 unlock 后用。
  Entry *put(motion::PreviewTime time, std::unique_ptr<Entry> entry);

  uint64_t compositionId() const;
  uint64_t revision() const;
  size_t size() const;

 private:
  static constexpr size_t MaxEntries = 256;
  // unordered_map<uint64_t, unique_ptr<Entry>> + LRU
};

}  // namespace motionstudio
```

### FrameCommandCache（Canvas，演进）

```cpp
// 相对现实现的变化：
// - key: PreviewTime bits（不再 floor）
// - scope revision: 使用 MSDocument::contentRevision（draw 时传入）
// - EDIT / PLAYBACK 均可 find/put（去掉 playbackMode 门禁）
// - Entry 仍只含 scene DrawCommandList + viewport 元数据（无 chrome）
// - 与 PreviewSceneCache 相同：unique_ptr<Entry> 存储，
//   put(time, unique_ptr<Entry>) -> Entry*
```

### MSDocument

```cpp
struct MSDocument {
  std::mutex mutex;
  std::unique_ptr<motion::Document> document;
  std::unique_ptr<motion::UndoManager> undoManager;
  uint64_t contentRevision = 0;
  motionstudio::PreviewSceneCache previewSceneCache;
};
```

### 入口（已持 DocumentLock）

```cpp
// 仅求值；供 hit-test / bounds / handles / draw 共用
Expected<const PreviewSceneCache::Entry *, std::string>
EnsurePreviewScene(MSDocument *handle, uint64_t compositionId, PreviewTime time) {
  handle->previewSceneCache.invalidateIfStale(compositionId, handle->contentRevision);
  if (const auto *hit = handle->previewSceneCache.find(time)) {
    return hit;
  }
  auto evaluated = SceneEvaluator::EvaluatePreview(...);
  // ...
  auto entry = std::make_unique<PreviewSceneCache::Entry>();
  entry->time = time;
  entry->state = std::move(evaluated.value());
  ResolvePointTextContainerSizes(entry->state);
  return handle->previewSceneCache.put(time, std::move(entry));
}

// 仅 draw 路径：在 SceneState 之上懒构建 / 复用 commands
const DrawCommandList *
EnsureSceneCommands(MSCanvas *canvas, MSDocument *handle,
                    uint64_t compositionId, PreviewTime time,
                    const SceneState &state) {
  canvas->frameCommandCache.invalidateIfStale(compositionId, handle->contentRevision);
  if (const auto *hit = canvas->frameCommandCache.find(time)) {
    return &hit->commands;
  }
  auto entry = std::make_unique<FrameCommandCache::Entry>();
  // fill viewport meta from state...
  entry->commands = BuildCommands(state);
  FrameCommandCache::Entry *stored =
      canvas->frameCommandCache.put(time, std::move(entry));
  return &stored->commands;
}
```

指针仅在当前 `DocumentLock` 内有效。

### Bridge C API

```c
void ms_document_set_content_revision(MSDocument *document, uint64_t revision);
uint64_t ms_document_get_content_revision(const MSDocument *document);
```

`ms_canvas_set_content_revision` / `MSCanvas::contentRevision` **删除**（测试改用 Document API）。Canvas 缓存失效在 draw 时读 `document->contentRevision`。

## 调用方

### draw（`ms_canvas_draw_*`）

1. `EnsurePreviewScene` → `state`。
2. `EnsureSceneCommands` → scene `commands`（EDIT/PLAYBACK 都缓存）。
3. `PlayCommands(commands)`。
4. 仅 `!playbackMode` 时用 `state` 现算 chrome。

### composition / hit 路径

只调用 `EnsurePreviewScene`（**不** build commands）：

- `ms_composition_hit_test_layer`
- `ms_composition_layer_bounds`
- `ms_layer_local_bounds`
- `ms_composition_selection_handles`
- canvas 侧 path / gradient / motion-path hit 中的 evaluate 一并接入

调试用的 evaluate `printf` 在验证后删除。

### App

- `MotionDocumentCore.changed()`：`revision += 1` 后 `ms_document_set_content_revision`。
- `CanvasViewController`：去掉 `ms_canvas_set_content_revision`。
- `hitTestLayer`、tap、double-tap 在 `isPlaying` 时直接返回（`handleLayerDrag` 已有 guard）。

## 行为预期

| 场景 | 预期 |
|---|---|
| 编辑静止 playhead：draw 后 hover hit-test | SceneState hit；不跑 `BuildCommands` |
| 仅 hit-test（尚未 draw） | 只 evaluate+resolve；不填 FrameCommandCache |
| 同帧第二次 draw | SceneState + DrawCommand 均 hit |
| revision 上涨 | 两级缓存均清空 |
| `12.0` vs `12.1` | 不同 key，不串用 |
| 播放循环整数帧 | FrameCommandCache 命中 |
| 播放中点击画布 | 不 hit-test |
| 拖拽改 transform | revision 连涨 → miss，可接受 |

## 测试

1. draw 后再 hit-test：`EvaluatePreview` 一次；第二次 hit-test 不 evaluate。
2. 仅 hit-test：不增加 FrameCommandCache 条目（或不调用 `BuildCommands`）。
3. 同帧两次 draw：第二次 `BuildCommands` 跳过。
4. revision 变化后两级均失效。
5. 不同 `PreviewTime` 不串用。
6. 两级 LRU 各自超限逐出。
7. 适配 `BridgeTest` revision API 到 Document。
8. （可选）播放中 tap 不改变 selection——手测。

## 非目标

- Core `SceneEvaluator` 内缓存
- 每次 evaluate 的 atomic generation
- 增量 / 脏层求值
- GPU snapshot / 脏区绘制
- 拖拽编辑路径的特殊加速

## 实现顺序建议

1. `PreviewSceneCache` + `MSDocument::contentRevision` + C API  
2. `EnsurePreviewScene`；composition / hit API 切换  
3. `FrameCommandCache` 改精确 time key + 用 Document revision；去掉 playback 门禁；`EnsureSceneCommands`  
4. App：revision 推送 + 播放跳过 hit-test；删除 canvas revision API  
5. 测试 + 去掉临时 profile `printf`
