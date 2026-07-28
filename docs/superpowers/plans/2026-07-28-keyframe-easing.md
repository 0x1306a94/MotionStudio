# 关键帧时间缓动 UI — 实现计划

> App-only；Core/Bridge 已具备 `SetEasing` / `CUBIC_BEZIER`。

**Spec：** `docs/superpowers/specs/2026-07-28-keyframe-easing-design.md`

## 任务

- [x] `KeyframeEasingPopover`：预设 + Custom/`CubicBezierPad`（单位正方形 Y 翻转）
- [x] 点菱形 → popover（含 Delete）；末帧缓动灰显
- [x] 点段徽章 → popover（改 `segment.start`）
- [x] 去掉菱形 `contextMenu`
- [x] Catalyst 编译通过

## 验收

人机：点徽章/菱形改预设与 Custom；拖板子播放有变化；Delete 可用。
