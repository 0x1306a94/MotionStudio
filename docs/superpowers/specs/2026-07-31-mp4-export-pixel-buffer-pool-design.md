# MP4 Export Pixel Buffer Pool — 设计说明

日期：2026-07-31  
状态：已确认（修订）  
依赖：`2026-07-31-mp4-video-export-design.md`

## 目标

1. 限制导出时 in-flight `CVPixelBuffer` / IOSurface，避免内存涨到 1G+  
2. 保留零拷贝（Metal → `CVPixelBuffer` → `AVAssetWriter`）  
3. 峰值内存可控（不随帧数线性涨到 GB）

## 非目标

- 渲染与编码多线程 pipeline  
- 可配置池大小 / network optimize 开关（首版写死）  
- `requestMediaDataWhenReadyOnQueue` 拉模型（吞吐优化，另议）

## 方案（修订）

### 不共用 `adaptor.pixelBufferPool`

实测：共享 `AVAssetWriterInputPixelBufferAdaptor.pixelBufferPool` 并对其调用 `CVPixelBufferPoolCreatePixelBuffer`，导出数帧后会在系统库内 **EXC_BREAKPOINT**。  
因此已删除 Core 上的 `VideoEncoder::platformPixelBufferPool` / `VideoFrameSource::setPlatformPixelBufferPool` 死接口；池只由 FrameSource 自建。

### 现行做法

- `TgfxVideoFrameSource`：**自建** `CVPixelBufferPool`（`MinimumBufferCount=3`，`AllocationThreshold=3`）
- **每帧不要 `releaseGpuCaches()`**：清 image/path cache 会让位图每帧重建，IOSurface Total/峰值冲到 GB 级
- `AvfVideoEncoder`：
  - `sourcePixelBufferAttributes = nil`
  - `expectsMediaDataInRealTime = YES`（否则 `readyForMoreMediaData` 常恒为 YES，writer 无界积压）
  - `shouldOptimizeForNetworkUse = NO`（长片 moov 前置会抬高中间内存）
- 背压：每帧 `waitUntilReadyForMoreFrames()` + acquire 触顶时 pump CFRunLoop

## 测试

- 现有 AVF / integration / frame-source / VideoExporter 测试仍通过  
- 手工：导出可跑完；内存不再随帧数线性冲到 1G+
