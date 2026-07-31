# MP4 Export Pixel Buffer Pool — 设计说明

日期：2026-07-31  
状态：已确认  
依赖：`2026-07-31-mp4-video-export-design.md`

## 目标

1. 限制导出时 in-flight `CVPixelBuffer` 数量，避免内存涨到 1G+  
2. 保留零拷贝（Metal → `CVPixelBuffer` → `AVAssetWriter`）  
3. `AVAssetWriter.shouldOptimizeForNetworkUse = YES`（默认开启）

## 非目标

- 改 Core `VideoFrame` / `VideoExporter` API  
- 渲染与编码多线程 pipeline  
- 可配置池大小 / network optimize 开关（首版写死）

## 方案

### FrameSource：`CVPixelBufferPool`，in-flight 上限 = 3

- 去掉「整段导出共用一块 buffer」  
- 建池：`MinimumBufferCount = 3`  
- `renderFrame`：`CVPixelBufferPoolCreatePixelBufferWithAuxAttributes` + `AllocationThreshold = 3`；触顶则 sleep 1ms 重试（writer 释放后归还池）  
- `VideoFrame` 持有 pool create 的 +1 retain，`releaseHandle = CFRelease`  
- `finish` 销毁 pool  

峰值约 `3 × W×H×4`（像素）+ GPU/编码器工作集。

### Encoder

`begin()` 中、`startWriting` 前：

```objc
writer.shouldOptimizeForNetworkUse = YES;
```

## 测试

- 现有 AVF / integration / frame-source 测试仍通过  
- 手工：较长合成导出时内存不再随帧数线性冲到 1G+
