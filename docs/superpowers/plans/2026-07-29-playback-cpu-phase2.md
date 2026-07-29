# 播放 CPU 优化 Phase 2 实现计划

**Goal:** 播放整数帧跨帧复用 `DrawCommandList`，循环第二圈跳过 evaluate/build。

**做法:** `FrameCommandCache`（LRU，最多 256 帧）挂在 `MSCanvas`；仅 `PLAYBACK` 模式写入/命中；`content_revision` 或 composition 变化时清空。不做 hold 段映射。

**手测:** 见 design §2.3。

**后续:** Phase 3（GPU Snapshot）已取消，见 design §Phase 3。本分支以 Phase 1 + Phase 2 收尾。
