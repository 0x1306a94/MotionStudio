# 关键帧时间缓动（自定义曲线）— 设计说明

日期：2026-07-28  
状态：已确认，实现中  
分支：`feature/0x1306a94_keyframe_easing`

## 目标

在时间轴上用**点击**编辑关键帧段的时间缓动（含自定义三次贝塞尔），Catalyst / iPad 均可用；不再依赖菱形右键（与 `DragGesture` 冲突，Catalyst 上无效）。

## 已有能力

- Core：`Easing` / `ApplyEasing` / `SetEasingCommand`
- Bridge：`ms_command_set_easing`（含 `MS_EASING_CUBIC_BEZIER` + in/out 控制点）
- App：`setEasing`；`CubicBezierPad`（绝对坐标两端点 + 两控制点）
- 语义：缓动存在**起点关键帧**，作用于该帧 → 下一帧；末帧缓动不参与求值

## 已锁定交互

| 入口 | 行为 |
|---|---|
| 点**菱形** | 打开 popover，编辑**该关键帧**的 outgoing easing；可 Delete |
| 点**段上曲线徽章** | 打开同一套 popover，编辑**段起点**（前面的）关键帧 easing |
| 预设（Linear / Ease / … / Hold） | 只设类型，**不**显示曲线板 |
| Custom | 显示单位正方形 `CubicBezierPad`；写入 `CUBIC_BEZIER` |
| 末帧菱形 | 可 Delete；缓动区灰显或提示无后续段（改了也不影响播放） |
| 菱形右键 | **去掉**（含缓动与 Delete）；删除改走 popover |

拖菱形仍移动关键帧：`DragGesture(minimumDistance: …)` + 轻点打开 popover。

## 坐标映射（Custom）

CSS 约定：时间 x、进度 y，控制点 `(inX,inY)` / `(outX,outY)`，端点 (0,0)→(1,1)。

显示时 Y 翻转以符合屏幕坐标（y 向下）：

- `p0 = (0, 1)`，`p3 = (1, 0)`
- `c1 = (inX, 1 - inY)`，`c2 = (outX, 1 - outY)`
- 写回：`inX/outX` clamp 到 `[0, 1]`；`inY/outY` 允许越界（overshoot）

## 非目标

- Inspector 第二入口
- 时间轴上绘制真实缓动波形
- 改 Core 缓动语义

## 测试 / 验收

- Core/Bridge：已有 `SetEasing` / Bridge CUBIC_BEZIER 测即可，本阶段可不加新 C++ 测
- 人机：Catalyst +（可选）iPad — 点徽章/菱形改预设与 Custom；拖控制点后播放可见变化；Delete 可用
