# Phase D：Follow Path — 设计说明

日期：2026-07-28  
状态：`done`（人机验证通过）  
分支：`feature/0x1306a94_path_animation`  
总览：[path-animation-roadmap](./2026-07-28-path-animation-roadmap.md)

## 目标

让图层沿 **另一图层的 ShapePath（`path`）** 运动：用可动画 `pathOffset`（0–1，弧长比例）驱动位置；默认沿切线朝向，并支持对齐偏移角。开启约束后 **覆盖** 手调 `position` /（朝向开启时）`rotation`。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 路径来源 | 同 composition 内另一图层的 `path`（ShapePath / 几何转 path） |
| 进度驱动 | `Animatable<float> pathOffset`，语义 0–1（弧长比例；可超出后 clamp） |
| 朝向 | 需要：`orientAlongPath`（默认 true）+ `orientOffset`（度，默认 0） |
| 与 transform | 约束生效时覆盖 position；朝向开时覆盖 rotation；关约束后恢复手调值 |
| 数据落点 | Layer 上可选 `FollowPath` 约束（方案 1） |
| 路径空间 | 路径在 **路径图层局部** 求值，再乘路径图层 **worldTransform** → 跟随层写入的是其 **父空间** position（与现有 position 语义一致） |
| 缺 path / 无效 id | 静默 no-op：不改写 position/rotation |
| closed path | 弧长按闭合环；offset 在 [0,1] clamp（MVP 不循环取模；可后置） |
| 自引用 | `pathLayerId == self` 拒绝 / 视为无效 |

## 非目标（本阶段）

- 跟随 Mask path
- 多路径切换、路径混合
- offset 循环 wrap（`fmod`）
- 反向行驶专用开关（可用 offset 关键帧 1→0 代替）
## 数据模型

```cpp
// model/FollowPath.h（示意）
struct FollowPath {
  bool enabled = false;
  EntityId pathLayerId;                 // 无效 = 未绑定
  Animatable<float> pathOffset{0.f};    // 0..1 弧长比例
  bool orientAlongPath = true;
  Animatable<float> orientOffset{0.f};  // degrees, added to tangent angle
};

// Layer 增加：
std::optional<FollowPath> followPath;   // 或 FollowPath followPath; + enabled
```

属性路径（Bridge / 时间轴）：

- `followPath.pathOffset`
- `followPath.orientOffset`
- 非动画：`followPath.enabled` / `pathLayerId` / `orientAlongPath` 经专用 command 或 set 静态 API

## 求值

```
EvaluateFollowPath(document, layer, time) -> optional<FollowSample>
  FollowSample { Vec2 parentSpacePosition; float rotationDegrees; bool overrideRotation; }

1. 若 !enabled 或 pathLayerId 无效 → nullopt
2. 解析 path 层；取 evaluated BezierPath（path 属性；无则 nullopt）
3. 弧长参数化：PointAndTangentAtArcLength(path, clamp(offset,0,1) * totalLength)
4. worldPoint = pathLayer.worldTransform * localPoint
5. parentSpacePosition = inv(followerParentWorld) * worldPoint
   （无父则 parentSpace = world）
6. 若 orientAlongPath：
     worldTangent 经 pathLayer.world 线性部分变换；
     angle = atan2(ty,tx) * rad2deg + orientOffset
     overrideRotation = true
```

接入点（推荐）：`SceneEvaluator` / `LocalMatrixOf` 在算 follower 的 local 矩阵前，若有 `FollowSample` 则用其 position（及 rotation）替代 `transform.position` / `rotation` 的 evaluate 结果；**不改写** 存储的 Animatable 值。

伪代码：

```
pos = follow ? sample.position : transform.position.evaluate(t)
rot = (follow && sample.overrideRotation) ? sample.rotation
                                          : transform.rotation.evaluate(t)
local = T(pos) * R(rot) * S(scale) * T(-anchor)
```

## 核心几何 API

```cpp
// animation/PathSampling.h（或 common/）
struct PathSample {
  Vec2 point;
  Vec2 tangent;  // 未归一化亦可；朝向用 atan2
};

float PathArcLength(const BezierPath &path);
PathSample PointAndTangentAtArcLength(const BezierPath &path, float arcLength);
```

实现：按段采样折线近似弧长（与现有 trim/stroke 采样密度同级即可）；MVP 精度优先够用、可测。

## Bridge / App（概要）

- 设置/清除 path 层、enabled、orient 开关
- `pathOffset` / `orientOffset` 静态值与关键帧（复用现有 float 命令）
- Inspector：Follow Path 区块（选 path 层、offset 滑条/数值、朝向开关、偏移角）
- 开启时 Transform 的 position（及朝向开时的 rotation）只读灰显
- 时间轴：`pathOffset`（及需要时 `orientOffset`）可打 KF

## 测试

- 直线 path：offset 0/0.5/1 → 端点与中点
- 带切线弧：中点偏离弦
- orient：水平向右 path → rotation ≈ 0 + offset
- 无效 path 层 / 无 path 属性 → 不覆盖 transform
- 序列化 round-trip
- Bridge：set offset 关键帧 → evaluate 位置变化（或通过 canvas/document 读 position 求值若暴露）

## 进度回写

Spec 审过 → plan → 实现；Core/Bridge 测绿 commit；App 人机验 → roadmap D = `done`。
