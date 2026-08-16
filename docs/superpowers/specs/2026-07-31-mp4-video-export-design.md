# MP4 Video Export (H.264) — 设计说明

日期：2026-07-31  
状态：已确认，待实现  
范围：库层 only（Core 编排 + Apple AVFoundation 编码 + 桥接 API；**无导出 UI**）

## 目标

1. 将合成逐帧烘焙为 **H.264 + MP4**
2. Apple 平台用 **AVFoundation**（`AVAssetWriter`）
3. 渲染→编码路径 **优先零拷贝**（`CVPixelBuffer` / IOSurface，避免 `ReadPixels`）
4. 编码/封装可替换：后续可接 FFmpeg，无需改编排逻辑
5. 预留音轨接口；**首版输出无声 MP4**

## 现状

| 层级 | 能力 |
| --- | --- |
| `SceneEvaluator` → `BuildCommands` → `PlayCommands` | 已有 |
| `TgfxRenderAdapter` | 离屏 `MTLTexture` + `ReadPixels`（有 CPU 拷贝） |
| `export/` 模块 | 架构文档已规划，**目录尚未落地** |
| PAG 导出 | 模型直转，不走 DrawCommand；与视频导出路径不同 |
| 序列帧 PNG | 文档描述走离屏 + 回读；可与视频共享 FrameSource 思路 |
| 视频 / AVAssetWriter | **无** |

## 非目标

- 导出 UI（进度条、文件选择器）
- 音频混流实现（仅接口预留）
- FFmpeg 实现（仅保证抽象可替换）
- 渲染/编码多线程 pipeline 重叠
- HEVC / ProRes / 透明通道 alpha 轨
- 导出保留 composition 圆角（圆角角上透明与 MP4 冲突；导出一律直角）
- 奇数尺寸自动 pad

---

## §1 架构与职责

采用 **Core 编排 + 可注入 Encoder / FrameSource**。

```
Document + VideoExportOptions
        │
        ▼
┌───────────────────┐
│ VideoExporter     │  Core export/：校验、逐帧循环、进度/取消
└────────┬──────────┘
         │
         ▼
┌───────────────────┐     Evaluate → BuildCommands → PlayCommands
│ VideoFrameSource  │◄──── 接口在 Core；Apple 实现在 adapter（零拷贝优先）
└────────┬──────────┘
         │ VideoFrame
         ▼
┌───────────────────┐
│ VideoEncoder      │  接口在 Core；AvfVideoEncoder 在 adapter
│ （可换 FFmpeg）    │
└───────────────────┘
```

| 组件 | 层 | 职责 |
| --- | --- | --- |
| `VideoExporter` | Core `export/` | 编排；不依赖 Metal / AVFoundation / FFmpeg |
| `VideoFrameSource` | 接口 Core；实现 adapter | 产出一帧 `VideoFrame` |
| `VideoEncoder` | 接口 Core；`AvfVideoEncoder` 在 adapter | H.264 编码 + MP4 封装 |
| `VideoFrame` | Core | `CpuRgba` 或 opaque `PlatformShared` |
| Bridge | `ms_video_export_*` | 组装默认 Apple 实现并调用 Exporter |

模块依赖（与 `docs/architecture.md` 一致）：

```
export → model + render
```

`AvfVideoEncoder` / `TgfxVideoFrameSource` **不**链入 `motionstudio_core`；由 `adapter` + `bridge` 链接。

---

## §2 关键接口

### 2.1 Options / Progress

```cpp
enum class H264Profile { Baseline, Main, High };

struct VideoExportOptions {
    std::string outputPath;
    TimeRange range;           // 非法则默认 [0, composition.duration)
    int width = 0;             // 0 → composition.width；须为正偶数
    int height = 0;            // 0 → composition.height；须为正偶数
    FrameRate frameRate;       // den==0 → composition.frameRate
    int bitrateBps = 0;        // 0 → 按分辨率估算默认码率
    int keyframeInterval = 0;  // 0 → 约 2 秒一关键帧（按导出 frameRate）
    H264Profile profile = H264Profile::High;
};

struct VideoExportProgress {
    FrameTime completedFrames;  // 已成功 append 的帧数
    FrameTime totalFrames;
};
```

默认码率（`bitrateBps == 0` 时）建议：

`bitrateBps = clamp(width * height * frameRateHz * 0.1, 1_000_000, 50_000_000)`  
（常数可在实现时微调，写入代码注释即可。）

默认 GOP：`keyframeInterval = max(1, round(frameRateHz * 2))`。

### 2.2 VideoFrame

```cpp
enum class VideoFrameStorage { CpuRgba, PlatformShared };

struct VideoFrame {
    int width = 0;
    int height = 0;
    VideoFrameStorage storage = VideoFrameStorage::CpuRgba;

    // CpuRgba：top-left origin
    const uint8_t* rgba = nullptr;
    size_t rowBytes = 0;
    bool premultiplied = true;

    // PlatformShared：opaque。Apple 实现约定为 CVPixelBufferRef
    void* platformHandle = nullptr;
    void (*retainHandle)(void*) = nullptr;
    void (*releaseHandle)(void*) = nullptr;
};
```

约定：

- Apple 零拷贝：`PlatformShared` + BGRA `CVPixelBuffer`（IOSurface-backed）
- CPU 回退：`CpuRgba`，RGBA8 premultiplied
- 持有 `platformHandle` 时，调用方在 `appendFrame` 返回后按需 `releaseHandle`；Encoder 若需跨调用持有则自行 `retainHandle`

### 2.3 VideoEncoder

```cpp
class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    virtual Expected<void, std::string> begin(const VideoExportOptions& options) = 0;
    virtual Expected<void, std::string> appendFrame(const VideoFrame& frame,
                                                    FrameTime presentationIndex) = 0;

    // 预留音轨：首版不调用。后续传入具体 AudioExportSource 时再实现。
    // 默认实现恒失败，避免静默忽略。
    virtual Expected<void, std::string> attachAudio() {
        return Unexpected<std::string>("audio not implemented");
    }

    virtual Expected<void, std::string> end() = 0;
    virtual void abort() = 0;  // 取消/失败：尽量删除不完整输出文件
};
```

`presentationIndex`：相对 `range.start` 的帧序号（0-based），Encoder 用 `frameRate` 转 CMTime / 时间戳。

### 2.4 VideoFrameSource

```cpp
class VideoFrameSource {
public:
    virtual ~VideoFrameSource() = default;

    virtual Expected<void, std::string> prepare(const Document& document,
                                                EntityId compositionId,
                                                const VideoExportOptions& options) = 0;

    // 优先 PlatformShared；无法零拷贝时可降级 CpuRgba
    virtual Expected<VideoFrame, std::string> renderFrame(FrameTime time) = 0;

    virtual void finish() = 0;
};
```

Apple 实现 `TgfxVideoFrameSource`：

1. 池化 IOSurface-backed `CVPixelBuffer`
2. 从 buffer 取得 `MTLTexture`，`tgfx::Surface::MakeFrom`
3. `SceneEvaluator::Evaluate` → `BuildCommands` → `PlayCommands`（`beginFrame` 时 **强制 `cornerRadius = 0`**，忽略合成圆角）
4. 返回带同一 `CVPixelBuffer` 的 `VideoFrame`（不 `ReadPixels`）

可基于扩展后的 `TgfxRenderAdapter`（支持外部 CVPixelBuffer/MTLTexture 目标），或独立类复用 `TgfxCanvasAdapter`；实现阶段选侵入更小者。

### 2.5 VideoExporter

```cpp
class VideoExporter {
public:
    // onProgress 返回 false → 取消：encoder.abort()，frames.finish()，
    // 返回 Unexpected("cancelled")
    // 调用期间 Document 必须不可变
    static Expected<void, std::string> Export(
        const Document& document, EntityId compositionId,
        const VideoExportOptions& options, VideoFrameSource& frames,
        VideoEncoder& encoder,
        const std::function<bool(VideoExportProgress)>& onProgress = {});
};
```

伪代码：

```
resolved = resolveAndValidate(document, compositionId, options)
frames.prepare(document, compositionId, resolved)
encoder.begin(resolved)
for t in [resolved.range.start, resolved.range.end):
    progress = { completedFrames: t - start, totalFrames }
    if onProgress && !onProgress(progress):
        encoder.abort(); frames.finish(); return Unexpected("cancelled")
    frame = frames.renderFrame(t)
    encoder.appendFrame(frame, t - start)
    release frame handle if needed
encoder.end()
frames.finish()
```

### 2.6 Bridge（示意）

```c
typedef struct {
    const char *outputPath;
    int64_t startFrame;   // <0 → 0
    int64_t endFrame;     // <0 → composition.duration
    int width;            // 0 → composition
    int height;
    int frameRateNum;     // 0 → composition
    int frameRateDen;
    int bitrateBps;
    int keyframeInterval;
    int profile;          // 0 Baseline / 1 Main / 2 High
} MSVideoExportOptions;

// progress: completed/total；返回 false 取消。cancelFlag 非空且 *cancelFlag!=0 亦取消。
// 成功返回 true；失败 false 且 *errorOut 需 ms_string_free。
bool ms_video_export(MSDocument *document, uint64_t compositionId,
                     const MSVideoExportOptions *options,
                     bool (*progress)(void *ctx, int64_t completed, int64_t total),
                     void *progressCtx, const volatile int *cancelFlag,
                     char **errorOut);
```

Bridge 内部构造 `TgfxVideoFrameSource` + `AvfVideoEncoder`，调用 `VideoExporter::Export`。仅 Apple 平台编译该 API（与 canvas 类似）。

---

## §3 错误处理 / 线程 / 像素

### 错误与取消

- Core：`Expected<..., std::string>`
- Bridge：`bool` + `errorOut`（`ms_string_free`）
- 校验失败（comp 不存在、奇数尺寸、空 path、空 range、非法 frameRate）在写文件前返回
- 中途失败或取消：`encoder.abort()` + `frames.finish()`
- 取消来源：`onProgress == false` 或 `cancelFlag`

### 线程

- `Export` 同步；可在调用方任意线程执行
- 约定：导出期间 Document 只读
- Apple 首版：渲染与 `AVAssetWriter` **同线程串行**
- UI 线程策略由未来 App 层决定（本任务不做 UI）

### 像素 / 色彩

- 使用 composition `backgroundColor` 清屏（导出时背景按不透明处理，写满整帧）
- **忽略 composition `cornerRadius`**：预览里的圆角裁剪会在角上露出透明，MP4 无透明轨；导出时 `beginFrame(..., cornerRadius=0)`，输出直角满幅矩形
- **不导出透明通道 / alpha 轨**
- 零拷贝：BGRA `CVPixelBuffer`
- CPU 回退：RGBA8 premultiplied
- 奇数宽高：校验失败，不自动 pad

---

## §4 文件布局（预期）

```
include/MotionStudio/export/
  VideoExportOptions.h
  VideoFrame.h
  VideoEncoder.h
  VideoFrameSource.h
  VideoExporter.h
src/export/
  VideoExporter.cpp          # resolve + loop
tests/export/
  VideoExporterTest.cpp      # Fake source/encoder
adapter/tgfx/ 或 adapter/avf/
  TgfxVideoFrameSource.*     # 零拷贝帧源
  AvfVideoEncoder.*          # AVAssetWriter
bridge/
  ms_video_export...         # Apple-only
```

具体落在 `adapter/tgfx` 还是新建 `adapter/avf`：实现时以「AVF 与 tgfx 耦合度」为准——若 Encoder 完全不依赖 tgfx，优先独立 `adapter/avf`，避免 tgfx 目标链接 AVFoundation 责任过重。

---

## §5 测试策略

| 层 | 内容 |
| --- | --- |
| Core | Fake source + Fake encoder：默认 range、自定义 range、进度回调次数、cancel、校验错误、PTS/`presentationIndex` 单调 |
| `AvfVideoEncoder` | 固定色帧写出 MP4，文件存在且可用系统 API 打开；非法 path / abort 删残文件 |
| `TgfxVideoFrameSource` | 简单场景一帧 `PlatformShared` 非空；可选与 `ReadPixels` 近似对比 |
| Bridge | 冒烟：非法参数返回错误字符串 |

首版不做与参考 MP4 的像素级 golden diff。

---

## §6 后续扩展点

- `attachAudio` + 音轨源（模型音频或外部文件）
- `FfmpegVideoEncoder` 实现同一 `VideoEncoder` 接口（可先只接 `CpuRgba`）
- 质量档 UI → 映射 `bitrateBps` / `profile`
- 渲染与编码双缓冲流水线（仍共用同一抽象）

---

## 决策记录

| 项 | 选择 |
| --- | --- |
| 范围 | 库层 only，无 UI |
| 音频 | 接口预留，首版无声 |
| 编排位置 | Core `VideoExporter`，实现注入 |
| 帧抽象 | `VideoFrame`：SharedHandle 优先，CPU 回退 |
| 参数粒度 | path / range / size / fps / bitrate / keyframeInterval / H.264 profile |
| 进度取消 | 同步 API + progress 回调（false=cancel） |
| 时间范围 | 可选 `startFrame`/`endFrame`，默认整段 |
| 圆角 | 导出忽略 `cornerRadius`（MP4 无透明） |
