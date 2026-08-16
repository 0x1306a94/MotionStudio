# Phase D：Follow Path 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按任务逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 图层跟随同 composition 内另一图层的 ShapePath / 几何 path：`pathOffset`（0–1 弧长）驱动 position；可选沿切线朝向 + `orientOffset`。

**架构：** Layer 挂 `FollowPath` 约束；`EvaluateFollowPath` 在 `SceneEvaluator::LocalMatrixOf` 求值时覆盖 position/rotation（不改写存储的 Animatable）。弧长采样新建 `PathSampling`；路径几何复用 Rect/Ellipse bake（与 `ConvertGeometryToPathCommand` 同思路，PreviewTime 版）。

**技术栈：** C++17 core、Apple C bridge、GoogleTest、SwiftUI。

**Spec：** `docs/superpowers/specs/2026-07-28-follow-path-design.md`  
**Roadmap：** `docs/superpowers/specs/2026-07-28-path-animation-roadmap.md`

## 全局约束

- 分支：`feature/0x1306a94_path_animation`
- Core/Bridge 测通自动 commit（不 push）；App 后人机验再标 `done`
- Commit：英语、≤120 字符、句号结尾、句中无其他标点
- `git commit --only <files> ...`；不碰无关改动
- 路径层**照常渲染**（不像 track matte）；offset **clamp** 不 wrap
- 自引用 / 无效 path / 缺 geometry → 静默 no-op
- Follow 互环（A→B→A）：求值 visiting 守卫，视为 no-op

## 文件对照

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/animation/PathSampling.h` + `src/animation/PathSampling.cpp` | 弧长 + PointAndTangent |
| `include/MotionStudio/model/FollowPath.h` | 约束结构体 |
| `include/MotionStudio/model/Layer.h` | `FollowPath followPath` 字段 |
| `include/MotionStudio/render/FollowPathEval.h` + `src/render/FollowPathEval.cpp` | `EvaluateLayerPath` / `EvaluateFollowPath` |
| `src/model/PropertyPath.cpp` | `followPath.pathOffset` / `orientOffset` |
| `src/render/SceneEvaluator.cpp` | LocalMatrixOf 接入 Follow |
| `include/MotionStudio/undo/SetFollowPathCommand.h` + cpp | enabled / pathLayerId / orientAlongPath |
| `src/serialization/Serializer.cpp` | layer followPath round-trip |
| Bridge + App Inspector / Timeline | 产品接线 |

---

### Task 1：PathSampling + 单测

**Files:**
- Create: `include/MotionStudio/animation/PathSampling.h`
- Create: `src/animation/PathSampling.cpp`
- Create: `tests/animation/PathSamplingTest.cpp`

**Interfaces:**
```cpp
namespace motion {
struct PathSample {
  Vec2 point;
  Vec2 tangent;  // 方向向量（可未归一化）；零长时用上一段或 (1,0)
};
float PathArcLength(const BezierPath &path);
// arcLength clamp 到 [0, total]；total==0 → 首点 + tangent(1,0)
PathSample PointAndTangentAtArcLength(const BezierPath &path, float arcLength);
}
```

实现要点：每段三次贝塞尔用固定步数（如 32）折线近似累加弧长；`closed` 时末→首再算一段。

- [ ] **Step 1：** 写失败测试 — 水平线段 `(0,0)-(100,0)`：`PathArcLength≈100`；offset 弧长 0/50/100 → 点与切线
- [ ] **Step 2：** 实现 PathSampling
- [ ] **Step 3：** 跑通 `./build/tests/core_tests --gtest_filter='PathSamplingTest.*'`
- [ ] **Step 4：** Commit: `Add PathSampling for arc-length path queries.`

---

### Task 2：FollowPath 模型 + PropertyPath

**Files:**
- Create: `include/MotionStudio/model/FollowPath.h`
- Modify: `include/MotionStudio/model/Layer.h` — 增加 `FollowPath followPath;`
- Modify: `src/model/PropertyPath.cpp` — 解析 `followPath.pathOffset` / `followPath.orientOffset`
- Modify: `tests/model/PropertyPathTest.cpp`（或 LayerTest）

**Interfaces:**
```cpp
struct FollowPath {
  bool enabled = false;
  EntityId pathLayerId;
  Animatable<float> pathOffset{0.f};
  bool orientAlongPath = true;
  Animatable<float> orientOffset{0.f};
};
```

- [ ] **Step 1：** 测默认值；Resolve `followPath.pathOffset` 非空
- [ ] **Step 2：** 落地模型 + PropertyPath
- [ ] **Step 3：** Commit: `Add FollowPath constraint fields on Layer.`

---

### Task 3：EvaluateFollowPath + SceneEvaluator 接入

**Files:**
- Create: `include/MotionStudio/render/FollowPathEval.h`
- Create: `src/render/FollowPathEval.cpp`
- Modify: `src/render/SceneEvaluator.cpp` — `LocalMatrixOf` / `WorldTransformOf`
- Create: `tests/render/FollowPathEvalTest.cpp`（或扩 SceneEvaluatorTest）

**Interfaces:**
```cpp
struct FollowSample {
  Vec2 parentSpacePosition;
  float rotationDegrees = 0.f;
  bool overrideRotation = false;
};

// 从 Shape 层 geometry 取 BezierPath（Path 直接 evaluate；Rect/Ellipse bake；TrimPath/无 → nullopt）
std::optional<BezierPath> EvaluateLayerPath(const Layer &layer, PreviewTime time);

// visiting：防 Follow 互环；失败 → nullopt
std::optional<FollowSample> EvaluateFollowPath(const Document &document, const Layer &layer,
                                               PreviewTime time, std::vector<EntityId> &visiting);
```

求值步骤（与 spec 一致）：
1. `!enabled` / 无效 id / `pathLayerId==self` → nullopt  
2. path 层无有效 path → nullopt  
3. `offset = clamp(pathOffset.evaluate, 0, 1)`；采样局部点/切线  
4. `worldPoint = pathWorld * point`；`parentSpace = inv(followerParentWorld) * worldPoint`（无父则 parentWorld=context，与现有 WorldTransformOf 的 context 一致）  
5. orient：`worldTangent = pathWorld.transformVector(tangent)`；`angle = atan2 + orientOffset`

**SceneEvaluator 改动：**
```cpp
// 原 LocalMatrixOf(Transform, time) 改为需要 Document+Layer：
Mat3 LocalMatrixOf(const Document &document, const Layer &layer, PreviewTime time,
                   const Mat3 &parentWorldOrContext, std::vector<EntityId> &followVisiting) {
  auto sample = EvaluateFollowPath(...); // 内部算 path 层 world 时复用 WorldTransformOf
  Vec2 pos = sample ? sample->parentSpacePosition
                    : layer.transform.position.evaluatePreview(time);
  float rot = (sample && sample->overrideRotation) ? sample->rotationDegrees
                    : layer.transform.rotation.evaluatePreview(time);
  return T(pos)*R(rot)*S(scale)*T(-anchor);
}
```

注意：`WorldTransformOf` 调 `LocalMatrixOf` 时传入「父 world」；Follow 求 path 层 world 时另开 visiting，避免与 parent visiting 混用。

测试：
- 直线 path 层 + follower enabled，offset 0/0.5/1 → evaluated world 锚点位置  
- orient 开：水平 path → rotation≈0；`orientOffset=90` → ≈90  
- 无效 pathLayerId → 仍用手调 position  
- A↔B 互指 → no-op（位置=手调）

- [ ] **Step 1：** 写失败测试  
- [ ] **Step 2：** 实现 Eval + 接线  
- [ ] **Step 3：** 测绿  
- [ ] **Step 4：** Commit: `Evaluate Follow Path when building layer local matrices.`

---

### Task 4：SetFollowPathCommand + 序列化

**Files:**
- Modify: `include/MotionStudio/undo/CommandKind.h` — `SetFollowPath`
- Create: `include/MotionStudio/undo/SetFollowPathCommand.h` + `src/undo/SetFollowPathCommand.cpp`
- Modify: `tests/undo/CommandsTest.cpp`
- Modify: `src/serialization/Serializer.cpp`（及必要时 Dto）
- Modify: `tests/serialization/SerializerTest.cpp`

**Command：**
```cpp
// 设置 enabled + pathLayerId + orientAlongPath；自引用/缺层 → 清为 disabled
class SetFollowPathCommand : public Command {
  SetFollowPathCommand(EntityId layerId, bool enabled, EntityId pathLayerId, bool orientAlongPath);
};
```
`pathOffset` / `orientOffset` 走现有 `SetStaticValue` / `AddKeyframe`（PropertyPath）。

**JSON（layer 可选字段，缺省=默认）：**
```json
"followPath": {
  "enabled": true,
  "pathLayerId": "...",
  "pathOffset": { /* Animatable float DTO */ },
  "orientAlongPath": true,
  "orientOffset": { /* Animatable float DTO */ }
}
```

- [ ] **Step 1：** 命令 undo/redo + merge 同 layer  
- [ ] **Step 2：** Serializer round-trip（含 pathOffset 关键帧）  
- [ ] **Step 3：** Commit: `Add SetFollowPathCommand and followPath serialization.`

---

### Task 5：Bridge API + 单测

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_*.cpp`（layer 查询 + commands）
- Modify: `bridge/tests/BridgeTest.cpp`

**API（示意）：**
```c
bool ms_layer_follow_path_enabled(MSDocument*, uint64_t layerId);
uint64_t ms_layer_follow_path_layer_id(MSDocument*, uint64_t layerId);
bool ms_layer_follow_path_orient(MSDocument*, uint64_t layerId);
void ms_command_set_follow_path(MSDocument*, uint64_t layerId, bool enabled,
                                uint64_t pathLayerId, bool orientAlongPath);
// pathOffset / orientOffset：已有 ms_property_* / ms_command_set_static_float / add_keyframe
```

- [ ] 测 set + 读回；`followPath.pathOffset` 关键帧经 property API  
- [ ] roadmap Core/Bridge → `core-done`  
- [ ] Commit: `Expose Follow Path over the bridge.`

---

### Task 6：App Inspector + 时间轴

**Files:**
- Create: `apps/.../Inspector/FollowPathInspector.swift`
- Modify: `InspectorView.swift`、`TransformInspector.swift`（follow 生效时 position / 朝向开时 rotation 灰显）
- Modify: `MotionDocumentCore.swift`
- Modify: `TimelineSupport.swift` — 列出 `followPath.pathOffset`（及有 KF 时的 `orientOffset`）

UI：
- Toggle enabled；Picker 选 path 层（同 composition、非 self）  
- Orient 开关；offset / orientOffset 数值行 + 钻石（复用 float 关键帧模式）  
- 开启 follow 时 Transform 的 position（及 orient 开时 rotation）`isEditable: false`，仍可显示**求值后**数值若 Bridge 有 evaluate（否则显示存储值并标注 Follow）

- [ ] 实现 UI  
- [ ] roadmap App → `ui-pending-verify`  
- [ ] Commit: `Add Follow Path inspector and timeline tracks.`

---

### Task 7：收尾

- [ ] `ctest --test-dir build -R 'PathSampling|FollowPath|Serializer|Bridge' --output-on-failure`（或全量相关）  
- [ ] 人机：A 画 path，B 开 Follow，打 pathOffset 0→1 KF，确认沿路径 + 朝向  
- [ ] roadmap D → `done`；spec 状态 → `done`

---

## Spec 覆盖自检

| Spec 项 | Task |
|---|---|
| FollowPath 字段 / pathOffset 动画 | 2, 4, 5, 6 |
| 覆盖 position/rotation 不写回 Animatable | 3 |
| 路径层仍渲染 | 无需改（无 usedAsMatteOnly） |
| 弧长采样 / closed clamp | 1, 3 |
| 无效/自引用 no-op | 3, 4 |
| 序列化 | 4 |
| Bridge / Inspector / 时间轴 | 5, 6 |
| 非目标（mask / wrap） | 不做 |
