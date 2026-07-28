# Phase A：路径形变（Path Morph）— 设计说明

日期：2026-07-28  
状态：设计已批准，待 plan / 实现  
分支：`feature/0x1306a94_path_animation`  
总览：[path-animation-roadmap](./2026-07-28-path-animation-roadmap.md)

## 目标

打通 **ShapePath.path** 与 **Mask.path** 的 `BezierPath` 关键帧形变产品闭环：启用动画 → 多关键帧改形 → 播放插值 → 时间轴可见可编。

## 现状（不重复造轮子）

| 层 | 已有 |
|---|---|
| Core | `Animatable<BezierPath>`；`Interpolator` 逐顶点 lerp；不等顶点数走 `ResamplePath`；`AddKeyframeCommand` / `PropertyValue` 支持 `BezierPath` |
| 求值 | `SceneEvaluator` / Mask 求值均 `path.evaluatePreview(time)` |
| Bridge | `ms_command_add_keyframe_bezier_path`、`ms_property_evaluate_bezier_path`、`ms_command_write_bezier_path_at_playhead`；钢笔写回已区分 static / animated |
| 测试 | `InterpolatorTest`（含自动匹配顶点数）、`PathResampleTest`、Bridge 部分 BezierPath 用例 |
| App 缺口 | 时间轴未列出 `path` / `masks[i].path`；无 Path 钻石启用 KF；`MotionDocumentCore` 无 `addKeyframeBezierPath` 封装 |

## 已锁定决策

| 项 | 选择 |
|---|---|
| 目标属性 | `path`（ShapePath）与 `masks[n].path` |
| Core 模型 | **不**新增类型；复用现有 `Animatable<BezierPath>` |
| 启用动画 | 钻石 / 命令：在 playhead 对当前 `evaluate` 值 `AddKeyframe`（静态 → 有关键帧） |
| 后续改形 | 钢笔保持现有：`isAnimated` 则 upsert KF，否则 `SetStaticValue` |
| 时间轴 | 有 KF 时出现 Path / Mask Path 轨道 + 菱形（拖移 / 删 / easing） |
| 顶点数 | 不等时 resample（已有）；**不做**顶点对应 UI |
| Schema | 不升 schema |

## 非目标

- Trim Path（Phase C）
- Motion Path 轨迹 / spatial 手柄 UI（Phase B）
- Follow Path（Phase D）
- 顶点自动对应的可视化编辑
- 多子路径 / compound path
- Lottie 导出完善（可后续跟）

## 架构

```
[钻石启用 / 时间轴]
  Swift → ms_command_add_keyframe_bezier_path(entity, "path"|masks[i].path, frame, evaluated)
  → AddKeyframeCommand → Animatable<BezierPath>

[钢笔改形]
  现有 PathEdit → write_bezier_path_at_playhead
  → animated ? AddKeyframe : SetStaticValue

[播放]
  SceneEvaluator(path.evaluatePreview) → BuildCommands → 画布
  （已通，A 补端到端单测）
```

## 关键接口

### Core（已有，A 只补测）

```cpp
// 求值 / 插值（已实现）
T Animatable<BezierPath>::evaluate(FrameTime time) const;
BezierPath Interpolator<BezierPath>::Lerp(const BezierPath &from, const BezierPath &to, float t);
// from/to 顶点数不等 → ResamplePath 对齐后再 lerp
// closed 标志不一致 → debug assert，release 返回 from

// 命令（已实现）
AddKeyframeCommand(PropertyPath, KeyframeData);  // KeyframeData 含 Keyframe<BezierPath>
RemoveKeyframeCommand(...);
SetStaticValueCommand(..., PropertyValue);       // PropertyValue 含 BezierPath
```

伪代码 — 端到端期望：

```
doc: ShapePath path KF@0 = P0, KF@20 = P1
state_mid = SceneEvaluator.Evaluate(doc, comp, 10)
assert state_mid.shapeItems[0].geometry ≈ Lerp(P0, P1, 0.5)
```

### Bridge（已有 API；补单测）

```c
void ms_command_add_keyframe_bezier_path(MSDocument *, uint64_t entityId,
                                         const char *path, int64_t frame,
                                         const MSBezierPath *value);
MSBezierPath *ms_property_evaluate_bezier_path(MSDocument *, uint64_t entityId,
                                               const char *path, int64_t frame);
int ms_property_keyframe_count(...);      // 对 BezierPath 通用
int64_t ms_property_keyframe_time_at(...);
void ms_command_write_bezier_path_at_playhead(...);  // 钢笔已用
```

### App（人机验证）

```swift
// MotionDocumentCore
func addKeyframeBezierPath(entityID:path:frame:value:)  // 或从 evaluate 取当前值
func evaluateBezierPath(entityID:path:frame:) -> ...

// TimelineSupport
// timelineAnimatedPropertyPaths / buildTimelineRows：
//   若 keyframes("path") 非空 → 轨道 "Path"
//   若 keyframes("masks[i].path") 非空 → 轨道 "Mask N Path"

// Inspector / 时间轴侧栏 Path 行
// 钻石语义与 Transform 一致：
//   当前帧无 KF → addKeyframe(evaluate(playhead))
//   当前帧有 KF → removeKeyframe
```

`keyframes()` 现用 `ms_property_keyframe_float_at` 填 value；时间轴菱形只依赖 **frame + easing**，BezierPath 轨道可继续忽略 float value（不阻塞 A）。若后续要显示摘要再扩展 Bridge。

## 数据流

1. 用户完成 ShapePath / Mask path（静态）  
2. 点 Path 钻石 → playhead 写入第 1 个 KF → `isAnimated == true`  
3. 移动 playhead → 钢笔改顶点 → upsert 后续 KF  
4. 播放 → 中间帧逐顶点插值（必要时 resample）  
5. 时间轴显示轨道菱形；可拖移 / 删除 / 改 easing；⌘Z 走现有 undo 镜像  

## 错误与边界

| 情况 | 行为 |
|---|---|
| closed 不一致的两 KF | debug assert；release 返回 from（现有） |
| 顶点数为 0 | lerp / resample 保持空路径 |
| 属性不存在 | Bridge no-op / 返回 NULL（现有） |
| 删光所有 KF | 回到 static（现有 `removeKeyframe`；若需显式 clear 用现有命令） |

## 测试计划（非 App，自动提交）

| 层 | 用例 |
|---|---|
| Core | 两 KF 中间帧顶点插值；不等顶点数；closed mismatch death；Add/RemoveKeyframe undo |
| Core | Serializer round-trip 保留 path 关键帧 |
| Core | `SceneEvaluator`：t0 / mid / t1 几何不同，mid 介于两端 |
| Bridge | 两 KF → evaluate mid；`keyframe_count` / `time_at`；animated vs static 的 write_at_playhead |

已有 `InterpolatorTest` / `PathResampleTest` 保留；A 补 **求值链路** 与 **Bridge 端到端**，避免重复测纯 lerp。

## App 验收清单（`ui-pending-verify`）

- [ ] Path / Mask Path 钻石可在 playhead 启用 / 取消当前帧关键帧  
- [ ] 时间轴出现对应轨道与菱形  
- [ ] 两帧改形后播放形态连续变化  
- [ ] ⌘Z / 重做正确  

## 实现顺序建议

1. Core：`SceneEvaluator` + undo + 序列化 补测 → commit  
2. Bridge：BezierPath 关键帧端到端补测 → commit  
3. App：Core 封装 + 时间轴 track + 钻石 → 标 `ui-pending-verify`，等人机验证  

## 进度回写

完成后更新 roadmap 进度表：Core/Bridge → `core-done`；App 交验 → `ui-pending-verify`；人机 OK → `done`，再开 Phase C spec。
