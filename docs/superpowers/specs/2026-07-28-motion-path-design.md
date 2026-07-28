# Phase B：Motion Path — 设计说明

日期：2026-07-28  
状态：B1 实现中  
分支：`feature/0x1306a94_path_animation`  
总览：[path-animation-roadmap](./2026-07-28-path-animation-roadmap.md)

## 目标

为 Position 关键帧提供空间贝塞尔运动路径：**先 API+单测（B1）→ 再画布可视化与手柄编辑（B2）**。

## 现状

| 层 | 已有 |
|---|---|
| Core | `Keyframe::spatialIn/OutTangent`、`EvaluateSpatial`、序列化、`AnimatableTest` 弧线用例 |
| Bridge / App | **无** spatial 读写；无轨迹叠加；无手柄 UI |

## 子阶段

### B1 — API（本轮实现）

- `BuildMotionPath(Animatable<Vec2>)` → 开放 `BezierPath`（精确段：有双边 spatial 用切线，否则直线）
- `SetSpatialTangentsCommand`（仅 Vec2 关键帧；可清手柄）
- Bridge：get/set spatial；`ms_property_build_motion_path`
- 单测：BuildMotionPath、命令 undo、Bridge round-trip

### B2 — 可视 + 手柄（B1 之后）

- 选中层 + position ≥2 KF → `PathOverlay` 画轨迹
- 手柄 chrome + 拖拽 → `SetSpatialTangentsCommand`
- 可选：「启用弧线」写入默认切线（段长 1/3）
- App 人机验证

## 已锁定决策

| 项 | 选择 |
|---|---|
| 属性 | 仅 `transform.position`（API 可挂任意 Vec2，产品先 position） |
| 无手柄 | 直线（现有求值） |
| B1 默认手柄 | **不**自动生成 |
| 轨迹空间 | 与 keyframe value 同空间（层局部 position） |
| 命令 | 独立 `SetSpatialTangents`，不混入 `AddKeyframe` |

## 非目标

- Follow Path（Phase D）
- 非 position 的 Vec2 运动轨迹产品化
- 时间轴曲线编辑器面板

## 核心接口（B1）

```cpp
// animation/MotionPath.h
BezierPath BuildMotionPath(const Animatable<Vec2> &position);

// undo
class SetSpatialTangentsCommand : public Command {
  SetSpatialTangentsCommand(PropertyPath property, FrameTime time,
                            std::optional<Vec2> spatialIn,
                            std::optional<Vec2> spatialOut);
};
```

```c
// Bridge
bool ms_property_keyframe_spatial_at(MSDocument *, uint64_t entityId, const char *path,
                                     int index, bool *hasIn, float *inX, float *inY,
                                     bool *hasOut, float *outX, float *outY);
void ms_command_set_spatial_tangents(MSDocument *, uint64_t entityId, const char *path,
                                     int64_t frame, bool hasIn, float inX, float inY,
                                     bool hasOut, float outX, float outY);
MSBezierPath *ms_property_build_motion_path(MSDocument *, uint64_t entityId, const char *path);
```

## 测试（B1）

- BuildMotionPath：直线两 KF；带 spatial 中点偏离直线；单 KF → 空路径
- SetSpatialTangents undo
- Bridge：set → get；build_motion_path 顶点数

## 进度回写

B1 测绿 → Core/Bridge `core-done`；B2 交验 → `ui-pending-verify` → 人机 OK → `done`。
