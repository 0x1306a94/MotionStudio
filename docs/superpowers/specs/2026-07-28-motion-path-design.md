# Phase B：Motion Path — 设计说明

日期：2026-07-28  
状态：`done`（人机验证通过）  
分支：`feature/0x1306a94_path_animation`  
总览：[path-animation-roadmap](./2026-07-28-path-animation-roadmap.md)

## 目标

为 Position 关键帧提供空间贝塞尔运动路径：**B1 API+单测 → B2 画布可视化与手柄编辑**。

## 现状

| 层 | 已有 |
|---|---|
| Core | `spatialIn/OutTangent`、`EvaluateSpatial`、`BuildMotionPath`、`SetSpatialTangentsCommand` |
| Bridge | spatial get/set、`ms_property_build_motion_path` |
| App | **无**轨迹叠加；无手柄 UI |

## 子阶段

### B1 — API（已完成）

- `BuildMotionPath` / `SetSpatialTangentsCommand` / Bridge spatial API + 单测

### B2 — 可视 + 调节（本轮）

**交互（锁定）：**

- Select 工具下：选中层且 `transform.position` ≥2 KF → 画布画运动路径 + KF 方块（只读切线预览）
- **调节**：Inspector「Motion Path」`CubicBezierPad` 拖控制点 → `SetSpatialTangents`（避免与图层 Free Transform 抢命中）
- 画布点选 KF / Prev·Next 切换当前段
- **不做**：画布拖切线改 spatial、画布拖 KF 改 position

**复用：** `CubicBezierPad` 为绝对坐标两端点+两控制点控件；后续时间缓动 `CUBIC_BEZIER` 映射到单位正方形即可复用。

**坐标：** path 顶点 = position KF 值（父空间）；`worldTransform` = 父层 world（无父则为 Identity / precomp context）。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 属性 | 仅 `transform.position` |
| 显示条件 | Select 工具 + 选中层 + ≥2 position KF |
| 无 stored spatial | 直线求值；选中 KF 显示可拉出预览手柄 |
| 轨迹空间 | position 值 × 父 worldTransform |
| 命令 | `SetSpatialTangents`（拖拽 merge） |

## 非目标

- Follow Path（Phase D）
- 画布拖 KF 改 position 值
- 非 position 的 Vec2 轨迹产品化
- 时间轴曲线面板

## 核心接口

### B1（已有）

```cpp
BezierPath BuildMotionPath(const Animatable<Vec2> &position);
class SetSpatialTangentsCommand;  // optional in/out
```

### B2（新增）

```cpp
// render/MotionPathChrome.h
struct MotionPathChrome {
  bool valid = false;
  EntityId layerId;
  BezierPath path;                 // BuildMotionPath，父空间
  Mat3 parentWorldTransform;
  int selectedKeyframe = -1;       // index into ascending KF list
  std::vector<FrameTime> keyframeTimes;
  std::vector<Vec2> worldVertices;
  std::vector<Vec2> worldInHandles;   // 含预览默认
  std::vector<Vec2> worldOutHandles;
};

bool BuildMotionPathChrome(const Document &, EntityId layerId, PreviewTime time,
                           int selectedKeyframe, MotionPathChrome &out);
enum class MotionPathHandleKind { None, Keyframe, InTangent, OutTangent };
struct MotionPathHit { MotionPathHandleKind kind; size_t index; };
MotionPathHit HitTestMotionPath(const MotionPathChrome &, Vec2 scenePoint, float radius);
DrawCommandList BuildMotionPathCommands(const MotionPathChrome &, float strokeWidth,
                                        float handleSize);
// 拖拽：scene → 父空间相对切线；缺对端则补默认
Vec2 MotionPathTangentFromScene(const MotionPathChrome &, size_t keyframeIndex, Vec2 scenePoint);
```

```c
void ms_canvas_set_motion_path_selection(MSCanvas *, uint64_t layerId, int selectedKeyframe);
MSMotionPathHit ms_canvas_hit_motion_path(MSCanvas *, MSDocument *, uint64_t compositionId,
                                          double frameTime, float sceneX, float sceneY);
// 拖动手柄：内部 SetSpatialTangents（含对端默认）
void ms_command_motion_path_set_tangent(MSDocument *, uint64_t layerId, int64_t frame,
                                        bool isOut, float sceneX, float sceneY,
                                        /* parent space written via canvas helper or: */
                                        float tangentX, float tangentY);
```

伪代码：

```
draw:
  if selectTool && selectedLayer && position.keyframes >= 2:
    chrome = BuildMotionPathChrome(...)
    play(BuildMotionPathCommands(chrome))  // after masks, with/before selection

hit (select tool, not pen):
  hit = HitTestMotionPath(chrome, scene)
  if Keyframe → set selectedKeyframe
  if In/OutTangent → begin drag → each move: compute tangent, SetSpatialTangents
     (+ default opposite end of segment if missing)
```

## 测试

- B1：已有
- B2 Core：无父 Identity；有父用 parent world；预览手柄位置；hit 优先级 Out/In > KF
- Bridge/App：人机验轨迹与拖拽弧线

## 进度回写

B2 交验 → `ui-pending-verify` → 人机 OK → `done`。
