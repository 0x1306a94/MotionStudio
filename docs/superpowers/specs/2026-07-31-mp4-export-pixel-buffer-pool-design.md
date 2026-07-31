# MP4 Export Pixel Buffer Pool — 设计说明

日期：2026-07-31  
状态：已确认  
依赖：`2026-07-31-mp4-video-export-design.md`

## 目标

1. 限制导出时 in-flight `CVPixelBuffer` / IOSurface，避免内存涨到 1G+  
2. 保留零拷贝（Metal → `CVPixelBuffer` → `AVAssetWriter`）  
3. `AVAssetWriter.shouldOptimizeForNetworkUse = YES`（默认开启）

## 非目标

- 渲染与编码多线程 pipeline  
- 可配置池大小 / network optimize 开关（首版写死）

## 方案

### 共用 `AVAssetWriterInputPixelBufferAdaptor.pixelBufferPool`

Instruments：`VM: IOSurface` Total Bytes 暴涨。FrameSource 自建池 + adaptor `sourcePixelBufferAttributes` 会让 adaptor 再拷/再占 IOSurface。

- `VideoExporter`：先 `encoder.begin()`，再 `setPlatformPixelBufferPool(encoder.platformPixelBufferPool())`，再 `prepare`
- `AvfVideoEncoder`：暴露 `adaptor.pixelBufferPool`；`shouldOptimizeForNetworkUse = YES`
- `TgfxVideoFrameSource`：优先用共享池；无共享池时（单测）仍自建池
- 背压：`VideoExporter` 每帧先 `encoder.waitUntilReadyForMoreFrames()`，再 `renderFrame`（避免在 append 前用 AllocationThreshold 死锁）
- `finish`：仅释放自建池

峰值目标：约 `3 × W×H×4`（像素）+ GPU/编码工作集。

## 测试

- 现有 AVF / integration / frame-source / VideoExporter 测试仍通过  
- 手工：较长合成导出时内存不再随帧数线性冲到 1G+
