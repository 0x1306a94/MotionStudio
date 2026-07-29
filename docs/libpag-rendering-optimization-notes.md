# libpag 渲染实现分析与 MotionStudio 可参考优化

> 背景：MotionStudio 持续播放时 CPU 占用偏高。本文基于 `third_party/libpag` 渲染路径（`src/rendering/`）与当前 MotionStudio 预览路径（`ms_canvas_draw_frame*` → `SceneEvaluator` → `BuildCommands` → tgfx adapter）对照，提炼可借鉴的优化手段。  
> 范围：播放器侧（`PAGPlayer` / `RenderCache` / Graphic 树），不含 exporter / PAGX 编辑器。  
> 日期：2026-07-29

## 1. libpag 渲染流水线（简图）

```
DisplayLink / 外部 setProgress
        │
        ▼
PAGPlayer::setProgress  ──► maxFrameRate 量化 progress（可选）
        │
        ▼
PAGPlayer::flushInternal
        │
        ├─ prepareInternal
        │     ├─ beginFrame（计时清零、标记 usedAssets）
        │     ├─ contentVersion 变化？
        │     │     ├─ 是：stage->draw(Recorder) → 重建 lastGraphic
        │     │     └─ 否：复用 lastGraphic
        │     └─
        ▼
PAGSurface::draw(renderCache, lastGraphic)
        ├─ contentVersion 与上次相同且 autoClear？→ 直接 return false（跳过 GPU）
        ├─ prepareLayers（提前解码即将可见的图/序列帧）
        ├─ graphic->prepare / graphic->draw（可走 Snapshot 纹理）
        ├─ context->flush + submit + present
        └─ prepareNextFrame（异步预解下一帧序列）
```

对比 MotionStudio 当前路径（每帧几乎固定全量）：

```
MTKView draw(in:)  @ ≤60fps（ProMotion 被封顶）
        │
        ▼
advancePlayheadForDraw  ──► previewFrame 为 double（亚帧插值）
        │
        ▼
ms_canvas_draw_frame_at_time_profiled
        ├─ DocumentLock
        ├─ SceneEvaluator::EvaluatePreview（全量图层）
        ├─ BuildCommands + 选中框 / path overlay / motion path 等编辑 chrome
        ├─ beginFrame → PlayCommands → endFrame
        └─ 无「内容未变则跳过」短路
```

## 2. libpag 关键优化手段

### 2.1 contentVersion：脏标记 + 整帧跳过（高价值）

**机制**

- 图层 `gotoTime` / 属性修改走 `notifyModified`，沿父链递增 `contentVersion`。
- `PAGPlayer::prepareInternal`：仅当 `contentVersion` 变化时才 `Recorder` 重建 `lastGraphic`。
- `PAGSurface::draw`：若 surface 已有内容且 `contentVersion == cache->getContentVersion()`，直接返回，不 clear、不重画、不 present。

**效果**

- 同一视觉帧被 DisplayLink 多次触发时，CPU/GPU 几乎为零。
- 「时间前进但视觉未变」（静帧区间）时，`gotoTime` 可不触发 `notifyModified`，整帧绘制被跳过。

**MotionStudio 对照**

- 每帧必跑 evaluate + build + play；即使整数帧未变、或关键帧 hold 段内几何不变，仍全量工作。
- 适配器侧已有按 geometry contentHash 的 path 缓存（`TgfxCanvasAdapter`），但亚帧插值使 hash 每帧变化，缓存命中差（Swift 注释已点明）。

### 2.2 静态时间区间（Static Time Ranges）+ FrameCache（高价值）

**机制**

- 对 Transform / Content / Mask / Filter 等分别做 `excludeVaryingRanges`，得到「视觉不变」的时间区间。
- `FrameCache::getCache`：`ConvertFrameByStaticTimeRanges` 把任意帧映射到区间代表帧，再查表；同区间只 `createCache` 一次。
- `LayerCache::checkFrameChanged`：用映射后的帧号判断是否真的变了。
- `CompositionCache` 同样按 composition 的 static ranges 复用 `Graphic`。

**效果**

- 长 hold、静态图层、未动画的 mask/content：CPU 求值与矢量构建接近 O(1) 复用。
- 与 contentVersion 配合：整棵树在静帧段可不重建。

**MotionStudio 对照**

- `Animatable<T>` 具备静态/关键帧双态，但预览路径没有「按属性静态区间映射帧 → 复用 SceneState / DrawCommand」层。
- `docs/timeline-evaluation.md` 若已有求值缓存讨论，播放器路径尚未落地为跨帧复用结构。

### 2.3 Graphic 树：录制一次、绘制多次（中高价值）

**机制**

- `Recorder` + `Graphic`（`MatrixGraphic` / `LayerGraphic` / `ModifierGraphic` / `Picture` / `Shape`…）是可复用的显示列表，不是每帧即时画完即弃。
- `Graphic::MakeCompose` 会合并相邻 Matrix / Modifier，减少树深度。
- 绘制阶段 `graphic->draw(Canvas)`；准备阶段 `graphic->prepare(cache)` 可触发异步解码。

**效果**

- 内容未变时跳过「图层遍历 → 几何生成」；只做 GPU 回放（或连回放也跳过，见 2.1）。

**MotionStudio 对照**

- `DrawCommandList` 语义接近 Graphic，但每帧重新 `BuildCommands`，没有跨帧持有的 command/graphic 缓存键（如 compositionId + quantizedFrame + revision）。

### 2.4 Snapshot：复杂矢量栅格化为纹理（中价值，换显存）

**机制**

- `cacheEnabled` → `RenderCache::snapshotEnabled`。
- `SnapshotPicture` / `Picture::draw`：在允许缓存时 `getSnapshot(picture)`，把子树画进离屏 Surface，后续帧 `drawImage`。
- Snapshot 带 `scaleFactor` / `uniqueKey`；缩放变化或 maker 变化则失效。
- LRU + `idleFrames` + 显存上限（约 20MB 水位 / 10 帧未用清理）；`cacheScale` 可整体降低缓存分辨率。
- 缩放过小时启用 mipmap（`MIPMAP_ENABLED_THRESHOLD = 0.4`）。

**效果**

- 复杂 shape / 文字 / 滤镜子树：用纹理采样替代每帧三角化与大量 path op。
- 代价：显存与首次栅格化峰值；不适合「每帧几何都变」的层。

**MotionStudio 对照**

- 持续播放若大量 mask / 复杂 path，每帧走 `drawPath` + mask 离屏，CPU/GPU 都重。
- 可对「本帧相对上次未变的图层」或「整段 static 的图层」做层级 snapshot；动画层保持矢量。

### 2.5 maxFrameRate：进度量化（中价值，尤其编辑器亚帧）

**机制**

- `PAGPlayer::setProgress`：若 `_maxFrameRate < composition.frameRate`，先把 progress 量化到较低帧网格再 `setProgressInternal`。
- 显示刷新 60/120Hz 时，不会逼出超过内容帧率的「伪新帧」。

**效果**

- 减少 contentVersion 变化次数 → 触发 2.1 / 2.2 短路。
- 与「按内容帧率求值、按显示刷新 present」解耦。

**MotionStudio 对照**

- `previewFrame` 为 double，按显示间隔 × `frameRate` 连续累加；`EvaluatePreview(PreviewTime(frameTime))` 亚帧插值。
- 已把 MTKView 封顶 60fps，但仍是「每显示帧一个唯一亚帧」，shape contentHash 基本不命中。
- 可借鉴：播放模式改为「量化到内容帧（或 2× 内容帧）」再 evaluate；scrub 精细预览再开亚帧。

### 2.6 prepare / 异步预解码（中价值，偏图序/视频）

**机制**

- `prepareLayers`：按「即将可见」时间窗（默认提前 500ms）收集 Image / Video PreCompose，触发 `makeDecoded` / `SequenceImageQueue::prepareNextImage`。
- `prepareNextFrame`：本帧 present 后继续预解下一帧。
- `useDiskCache`：序列帧可落盘，降内存峰值。

**效果**

- 解码毛刺从关键路径挪到后台；主线程更稳。
- MotionStudio 首阶段若以矢量为主，收益小于静帧/命令缓存，但图片层/未来视频层需要同样模式。

### 2.7 tgfx DisplayList：脏区 Partial / Tiled（中长期）

**机制**

- `DisplayList::render` 支持 `Direct` / `Partial` / `Tiled`。
- Partial：维护全尺寸 cache surface，只重画 dirty rect，再 blit 到目标。

**说明**

- 这是 tgfx **图层树**路径的优化；经典 `PAGPlayer`→`Graphic`→`Canvas` 路径不直接走 DisplayList。
- MotionStudio 若未来把编辑器 UI 或场景迁到 tgfx Layer，可直接用；当前扁平 `DrawCommand` 适配器要做脏区，需自建「层 bounds + dirty rect」或引入 Layer API。

### 2.8 性能埋点（工程化）

**机制**

- `PAGPlayer::flushInternal` 用 `tgfx::Clock` 拆 `rendering` / `presenting`，并扣除 texture upload / program compile。
- `RenderCache` 继承 `Performance`，累计 imageDecoding / textureUploading 等。
- Viewer 有运行时图表（`PAGRunTimeDataModel`）。

**MotionStudio 对照**

- 已有 `ms_canvas_draw_frame_profiled` 与 Swift 侧播放 timing 日志，方向正确。
- 建议补齐：evaluate / build / play / mask-offscreen / path-cache-hit-rate；用数据决定先做 2.1 还是 2.4。

## 3. 与 MotionStudio 的差距摘要

| 能力 | libpag | MotionStudio 现状 | 播放 CPU 影响 |
|---|---|---|---|
| 内容版本短路整帧 | 有 | 无 | 高 |
| 静帧区间映射 | 有 | 无 | 高 |
| 跨帧 Graphic/Command 复用 | 有 | 每帧重建 | 高 |
| 进度/帧率量化 | maxFrameRate | 亚帧 double + 封顶 60Hz | 高 |
| 层/子树 Snapshot | cacheEnabled | 无 | 中（复杂场景） |
| Path 几何缓存 | tgfx + 自有 | adapter contentHash（亚帧易失效） | 中 |
| 异步预解码 | prepareLayers | 基本无 | 低→中（有图序时） |
| 脏区绘制 | DisplayList Partial | 全清全画 | 中（编辑器 UI） |
| 编辑 chrome 分离 | 播放器较纯 | 每帧 build selection/overlay | 中 |

## 4. 建议落地优先级（仅建议，非实施计划）

### P0 — 先止血（改动面相对小）

1. **播放模式关闭亚帧（或量化到内容帧）**  
   - 播放时 `EvaluatePreview` 使用 `floor(previewFrame)` 或 `round` 到内容帧；亚帧仅用于 scrub / 导出预览开关。  
   - 直接恢复 path contentHash 命中，并降低 evaluate 独特性。  
   - 对齐 libpag `maxFrameRate` 思想。

2. **整数帧 / revision 未变则跳过 draw**  
   - 桥接或 adapter 缓存「上次已呈现的 (compositionId, quantizedFrame, documentRevision, viewSize, chromeSignature)」。  
   - 命中则跳过 evaluate/build/play（或至少跳过 play）。  
   - 对齐 `contentVersion` + `PAGSurface::draw` 短路。

3. **播放时降级/跳过编辑 chrome**  
   - 播放中不每帧 `BuildSelectionOutline` / path edit / motion path（或降到整数帧变更时再画）。  
   - libpag 播放路径本身不含编辑器叠层。

### P1 — 结构级缓存（对齐 FrameCache / Graphic）

4. **按层的求值/命令缓存**  
   - Key：`layerId + quantizedContentFrame + layerContentRevision`。  
   - 静帧区间：对无动画属性可把多帧映射同一 key（可先做 hold 段检测，完整 `excludeVaryingRanges` 后置）。  
   - 输出可复用的 `EvaluatedLayer` 或局部 `DrawCommandList`。

5. **持有上一帧 DrawCommandList / SceneState**  
   - document revision 不变且帧 key 命中时直接 `PlayCommands`。  
   - 类似 `lastGraphic` 复用。

### P2 — 栅格化与异步（对齐 Snapshot / prepare）

6. **静态层 Snapshot**  
   - 对 N 帧未变的层，栅格化为纹理，播放时 `drawImage`。  
   - 提供 `cacheScale` 与显存上限，避免编辑大画布爆显存。

7. **图片/序列异步解码**  
   - 与播放时钟解耦的 prepare 队列（有图片层后再做）。

### P3 — 更长线

8. **脏区 / 瓦片**（tgfx DisplayList 或自建）。  
9. **播放专用精简管线**（无 DocumentLock 争用、无 UI overlay、可选离屏固定分辨率）。

## 5. 关键源码索引（libpag）

| 主题 | 路径 |
|---|---|
| 播放入口 / maxFrameRate / prepareInternal | `third_party/libpag/src/rendering/PAGPlayer.cpp` |
| 跳过同版本绘制 | `third_party/libpag/src/rendering/PAGSurface.cpp`（`draw`） |
| Snapshot / prepareLayers / LRU | `third_party/libpag/src/rendering/caches/RenderCache.{h,cpp}` |
| 帧缓存模板 | `third_party/libpag/src/rendering/caches/FrameCache.h` |
| 层缓存 / 静帧检测 | `third_party/libpag/src/rendering/caches/LayerCache.cpp` |
| 合成内容缓存 | `third_party/libpag/src/rendering/caches/CompositionCache.cpp` |
| Graphic 组合与绘制 | `third_party/libpag/src/rendering/graphics/Graphic.cpp` |
| SnapshotPicture | `third_party/libpag/src/rendering/graphics/Picture.cpp` |
| contentVersion | `third_party/libpag/src/rendering/layers/PAGLayer.cpp`（`notifyModified`） |
| tgfx 脏区 | `third_party/tgfx/src/layers/DisplayList.cpp` |
| MotionStudio 每帧全量路径 | `bridge/src/common/motionstudio_bridge_canvas.cpp` |
| 播放调度 / 亚帧 / 60fps 封顶 | `apps/MotionStudioApp/.../Canvas/CanvasViewController.swift` |

## 6. 结论

libpag 的低 CPU 播放并不主要靠「更快的单次 path 绘制」，而靠：

1. **尽量少产生新帧**（maxFrameRate / 静帧区间 / gotoTime 无变化不 dirty）；  
2. **产生了也不重建**（contentVersion + lastGraphic）；  
3. **重建了也不重算图层**（FrameCache / CompositionCache）；  
4. **重算了也不重复矢量**（Snapshot 纹理）；  
5. **解码不挡帧**（prepare 异步）。

MotionStudio 当时瓶颈更符合「显示刷新驱动的亚帧全量管线」：每帧唯一内容 → 缓存失效 → evaluate/build/play 全开。优先做 **帧量化 + 播放期去掉编辑 chrome + 命令级复用**（见 [playback-cpu 设计](superpowers/specs/2026-07-29-playback-cpu-optimization-design.md) Phase 1/2）。

**GPU Snapshot（层/整帧）本阶段不做：** 全 drawable 整帧缓存在 Retina 下显存装不下循环、命中率近零，且 Metal `framebufferOnly` 需离屏拷贝，miss 更贵。若再做需另开设计（合成分辨率或 per-layer），见该设计文档 §Phase 3。

---

相关文档：[rendering.md](rendering.md)、[timeline-evaluation.md](timeline-evaluation.md)、[architecture.md](architecture.md)、[playback-cpu 设计](superpowers/specs/2026-07-29-playback-cpu-optimization-design.md)。
