# Phase A：路径形变（Path Morph）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按任务逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 打通 ShapePath / Mask 的 `BezierPath` 关键帧形变产品闭环：先补 Core/Bridge 端到端单测，再接 App 时间轴轨道与钻石启用动画。

**架构：** 复用已有 `Animatable<BezierPath>` 插值、`SceneEvaluator` 路径求值、Bridge 关键帧 API。Phase A 只补测试与 App 接线（时间轴轨道 + 关键帧开关）；**不**新增 Core 类型、**不**升 schema。

**技术栈：** C++17 core、Apple C bridge、GoogleTest、SwiftUI app。

**Spec：** `docs/superpowers/specs/2026-07-28-path-morph-design.md`  
**Roadmap：** `docs/superpowers/specs/2026-07-28-path-animation-roadmap.md`

## 全局约束

- 分支：`feature/0x1306a94_path_animation`（禁止直接在 master/develop 提交）。
- C++ / Bridge / 测试：任务测通后自动 commit（不 push）。
- App UI（Swift）：实现后把 roadmap 标为 `ui-pending-verify`；人机验证通过后再标 `done`。
- Commit 信息：英语、≤120 字符、句号结尾、句中无其他标点。
- 使用 `git commit --only <files> ...`，不碰无关工作区改动。
- Core/Bridge 任务完成后：roadmap Core/Bridge 列 → `core-done`；App 任务完成后：App UI → `ui-pending-verify`。

---

## 文件对照

| 文件 | 职责 |
|---|---|
| `tests/render/SceneEvaluatorTest.cpp` | 中间帧路径形变求值 |
| `tests/undo/CommandsTest.cpp` | BezierPath 加/删关键帧 undo |
| `tests/serialization/SerializerTest.cpp` | ShapePath path 关键帧 round-trip |
| `bridge/tests/BridgeTest.cpp` | 两 KF → 中间 evaluate；write_at_playhead 分支 |
| `apps/.../Model/MotionDocumentCore.swift` | `addKeyframeBezierPathAtPlayhead` 等 |
| `apps/.../Timeline/Root/TimelineSupport.swift` | 列出 `path` / `masks[i].path` 轨道 |
| `apps/.../Inspector/PathKeyframeInspector.swift` | Shape path 钻石行（新建） |
| `apps/.../Inspector/MasksInspector.swift` | Mask path 钻石 |
| `apps/.../Inspector/InspectorView.swift` | 挂载 PathKeyframeInspector |
| `docs/superpowers/specs/2026-07-28-path-animation-roadmap.md` | 进度表更新 |

---

### Task 1：Core — SceneEvaluator 中间帧路径形变

**Files:**
- Modify: `tests/render/SceneEvaluatorTest.cpp`
- （通常无需改生产代码；若求值错误再修 `src/render/SceneEvaluator.cpp`）

**Interfaces:**
- Consumes: `SceneEvaluator::Evaluate`、`ShapePath::path`、`ShapeGeometryToBezierPath`
- Produces: 中间帧几何等于路径 lerp 的回归测试

- [x] **Step 1：写测试（表征 / 失败均可）**

在 `tests/render/SceneEvaluatorTest.cpp` 追加（缺则补 `#include "MotionStudio/model/ShapePath.h"` 与 `using motion::ShapePath`）：

```cpp
namespace {

BezierPath MakeShiftedSegment(float x0, float x1) {
    BezierPath path;
    path.closed = false;
    path.vertices.push_back({{x0, 0}, {0, 0}, {0, 0}});
    path.vertices.push_back({{x1, 0}, {0, 0}, {0, 0}});
    return path;
}

struct PathScene {
    Document document;
    Composition *composition = nullptr;
    Layer *layer = nullptr;
    ShapePath *pathShape = nullptr;

    PathScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = 100;
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        auto element = std::make_unique<ShapePath>();
        pathShape = element.get();
        content->geometry = std::move(element);
        auto fill = std::make_unique<FillStyle>();
        fill->color.setStaticValue(Color{1, 0, 0, 1});
        layer->styles.push_back(std::move(fill));
    }

    Expected<SceneState, std::string> Evaluate(motion::FrameTime time) {
        return SceneEvaluator::Evaluate(document, composition->id, time);
    }
};

}  // namespace

TEST(SceneEvaluatorTest, AnimatedShapePathMorphsBetweenKeyframes) {
    PathScene scene;
    motion::Keyframe<BezierPath> from;
    from.time = 0;
    from.value = MakeShiftedSegment(0, 10);
    motion::Keyframe<BezierPath> to;
    to.time = 20;
    to.value = MakeShiftedSegment(20, 30);
    scene.pathShape->path.addKeyframe(from);
    scene.pathShape->path.addKeyframe(to);

    Expected<SceneState, std::string> mid = scene.Evaluate(10);
    ASSERT_TRUE(mid.hasValue());
    ASSERT_EQ(mid->layers.size(), 1u);
    ASSERT_FALSE(mid->layers[0].shapeItems.empty());

    const BezierPath baked =
        motion::ShapeGeometryToBezierPath(mid->layers[0].shapeItems[0].geometry);
    ASSERT_EQ(baked.vertices.size(), 2u);
    EXPECT_FLOAT_EQ(baked.vertices[0].point.x, 10.0f);
    EXPECT_FLOAT_EQ(baked.vertices[1].point.x, 20.0f);
}
```

- [x] **Step 2：跑测试**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.AnimatedShapePathMorphsBetweenKeyframes'
```

期望：求值已正确则 PASS（表征测试）。FAIL 则修 `SceneEvaluator` 路径求值直至 PASS。

- [x] **Step 3：提交**

```bash
git commit --only tests/render/SceneEvaluatorTest.cpp src/render/SceneEvaluator.cpp \
  -m "Cover animated ShapePath morph mid-frame evaluation."
```

（若未改 `SceneEvaluator.cpp`，从 `--only` 去掉该文件。）

---

### Task 2：Core — Serializer ShapePath 路径关键帧 round-trip

**Files:**
- Modify: `tests/serialization/SerializerTest.cpp`

**Interfaces:**
- Consumes: `Serializer::Save` / `Load`、`ShapePath::path` 上的 `Animatable<BezierPath>` 关键帧
- Produces: 路径关键帧时间与值的 round-trip 断言

- [x] **Step 1：写测试**

```cpp
TEST(SerializerTest, ShapePathKeyframeRoundTrip) {
    Document original;
    Composition *composition = original.addComposition(std::make_unique<Composition>());
    Layer *layer = original.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto path = std::make_unique<ShapePath>();

    BezierPath p0;
    p0.closed = true;
    p0.vertices.push_back({{0, 0}, {}, {}});
    p0.vertices.push_back({{10, 0}, {}, {}});
    p0.vertices.push_back({{5, 8}, {}, {}});
    BezierPath p1 = p0;
    p1.vertices[2].point = {5, 20};

    Keyframe<BezierPath> kf0;
    kf0.time = 0;
    kf0.value = p0;
    Keyframe<BezierPath> kf1;
    kf1.time = 30;
    kf1.value = p1;
    path->path.addKeyframe(kf0);
    path->path.addKeyframe(kf1);
    content->geometry = std::move(path);

    auto saved = Serializer::Save(original);
    ASSERT_TRUE(saved.hasValue());
    auto loaded = Serializer::Load(*saved);
    ASSERT_TRUE(loaded.hasValue());

    Layer *roundTrip = (*loaded)->entityIndex().findLayer(layer->id);
    ASSERT_NE(roundTrip, nullptr);
    auto *shapeContent = static_cast<ShapeContent *>(roundTrip->content.get());
    ASSERT_NE(shapeContent->geometry, nullptr);
    ASSERT_EQ(shapeContent->geometry->type(), ShapeType::Path);
    const auto &anim = static_cast<ShapePath *>(shapeContent->geometry.get())->path;
    ASSERT_TRUE(anim.isAnimated());
    ASSERT_EQ(anim.keyframes().size(), 2u);
    EXPECT_EQ(anim.keyframes()[0].time, 0);
    EXPECT_EQ(anim.keyframes()[1].time, 30);
    EXPECT_EQ(anim.keyframes()[0].value, p0);
    EXPECT_EQ(anim.keyframes()[1].value, p1);
}
```

`Serializer` 的 Save/Load 调用方式与同文件邻近测试保持一致（照抄 API 名）。

- [x] **Step 2：跑测试**

```bash
./build/tests/core_tests --gtest_filter='SerializerTest.ShapePathKeyframeRoundTrip'
```

期望：PASS。FAIL 则修 `Animatable<BezierPath>` 序列化再跑。

- [x] **Step 3：提交**

```bash
git commit --only tests/serialization/SerializerTest.cpp src/serialization/Serializer.cpp \
  -m "Cover ShapePath BezierPath keyframe serialization round trip."
```

---

### Task 3：Core — BezierPath 加/删关键帧 undo

**Files:**
- Modify: `tests/undo/CommandsTest.cpp`

**Interfaces:**
- Consumes: `AddKeyframeCommand`、`RemoveKeyframeCommand`、`PropertyPath{layerId, "path"}`、`KeyframeData`
- Produces: undo 恢复 ShapePath.path 的静态值 / 先前关键帧

- [x] **Step 1：写测试**

沿用 `CommandsTest.cpp` 里已有的 `AddKeyframeCommandTest` fixture。最小示意：

```cpp
TEST(AddKeyframeCommandTest, BezierPathAddAndUndo) {
    // 建 Document + Shape 层 + ShapePath（静态空或简单路径）。
    // PropertyPath property{layer->id, "path"};
    BezierPath value;
    value.vertices.push_back({{1, 2}, {}, {}});
    value.vertices.push_back({{3, 4}, {}, {}});
    Keyframe<BezierPath> keyframe;
    keyframe.time = 12;
    keyframe.value = value;

    scene.execute<AddKeyframeCommand>(property, KeyframeData{keyframe});
    ASSERT_TRUE(pathAnimatable->isAnimated());
    EXPECT_EQ(pathAnimatable->keyframes().size(), 1u);

    scene.undo();
    EXPECT_FALSE(pathAnimatable->isAnimated());
}
```

用与其他命令测试相同的 scene helper，把 `pathAnimatable` 接到 `ShapePath::path`；优先扩展已有 helper，不要另起一套框架。

若尚未覆盖删除，再加：

```cpp
TEST(RemoveKeyframeCommandTest, BezierPathRemoveAndUndo) {
    // 加两关键帧，删一个，undo，断言数量与值恢复。
}
```

- [x] **Step 2：跑测试**

```bash
./build/tests/core_tests --gtest_filter='*BezierPath*Keyframe*'
```

期望：PASS。

- [x] **Step 3：提交**

```bash
git commit --only tests/undo/CommandsTest.cpp \
  -m "Cover BezierPath keyframe add and remove undo."
```

---

### Task 4：Bridge — 中间帧 evaluate + write_at_playhead 分支

**Files:**
- Modify: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Consumes: `ms_command_add_path_layer`、`ms_command_add_keyframe_bezier_path`、`ms_property_evaluate_bezier_path`、`ms_property_keyframe_count`、`ms_property_keyframe_time_at`、`ms_command_write_bezier_path_at_playhead`、`ms_property_is_animated`

- [x] **Step 1：写测试**

```cpp
TEST(BridgeBezierPathTest, MorphEvaluateMidpointAndKeyframeTimes) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_path_layer(document, compositionId);

    MSBezierVertex a[2] = {{0, 0, 0, 0, 0, 0}, {10, 0, 0, 0, 0, 0}};
    MSBezierPath from{a, 2, false};
    MSBezierVertex b[2] = {{20, 0, 0, 0, 0, 0}, {30, 0, 0, 0, 0, 0}};
    MSBezierPath to{b, 2, false};

    ms_command_add_keyframe_bezier_path(document, layerId, "path", 0, &from);
    ms_command_add_keyframe_bezier_path(document, layerId, "path", 20, &to);
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "path"), 2);
    EXPECT_EQ(ms_property_keyframe_time_at(document, layerId, "path", 0), 0);
    EXPECT_EQ(ms_property_keyframe_time_at(document, layerId, "path", 1), 20);

    MSBezierPath *mid = ms_property_evaluate_bezier_path(document, layerId, "path", 10);
    ASSERT_NE(mid, nullptr);
    ASSERT_EQ(mid->count, 2u);
    EXPECT_FLOAT_EQ(mid->vertices[0].pointX, 10.0f);
    EXPECT_FLOAT_EQ(mid->vertices[1].pointX, 20.0f);
    ms_bezier_path_free(mid);
    ms_document_destroy(document);
}

TEST(BridgeBezierPathTest, WriteAtPlayheadStaticThenAnimated) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_path_layer(document, compositionId);

    MSBezierVertex v0[2] = {{0, 0, 0, 0, 0, 0}, {5, 0, 0, 0, 0, 0}};
    MSBezierPath staticPath{v0, 2, false};
    ms_command_write_bezier_path_at_playhead(document, layerId, "path", 0, &staticPath);
    EXPECT_FALSE(ms_property_is_animated(document, layerId, "path"));

    // 钻石语义：显式加关键帧才启用动画。
    ms_command_add_keyframe_bezier_path(document, layerId, "path", 0, &staticPath);
    EXPECT_TRUE(ms_property_is_animated(document, layerId, "path"));

    MSBezierVertex v1[2] = {{0, 0, 0, 0, 0, 0}, {15, 0, 0, 0, 0, 0}};
    MSBezierPath keyed{v1, 2, false};
    ms_command_write_bezier_path_at_playhead(document, layerId, "path", 10, &keyed);
    EXPECT_EQ(ms_property_keyframe_count(document, layerId, "path"), 2);

    ms_document_destroy(document);
}
```

若聚合初始化与 `MSBezierPath` 字段顺序不符，按 `StaticRoundTripAndKeyframe` 同文件写法改。

- [x] **Step 2：跑测试**

```bash
cmake --build build --target bridge_test
./build/bridge/tests/bridge_test --gtest_filter='BridgeBezierPathTest.MorphEvaluateMidpointAndKeyframeTimes:BridgeBezierPathTest.WriteAtPlayheadStaticThenAnimated'
```

（二进制路径以 CMake 为准；可用 `ctest -N -R Bridge` 查找。）

期望：PASS。

- [x] **Step 3：更新 roadmap 进度并提交**

在 `docs/superpowers/specs/2026-07-28-path-animation-roadmap.md` 将 Phase A 的 Core/Bridge 标为 `core-done`。

```bash
git commit --only bridge/tests/BridgeTest.cpp \
  docs/superpowers/specs/2026-07-28-path-animation-roadmap.md \
  -m "Cover Bridge BezierPath morph evaluate and playhead write."
```

---

### Task 5：App — MotionDocumentCore + 时间轴 path 轨道

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift`

**Interfaces:**
- Consumes: `ms_property_evaluate_bezier_path`、`ms_command_add_keyframe_bezier_path`、`ms_bezier_path_free`、`keyframes` / `keyframeFrames`
- Produces: `addKeyframeBezierPathAtPlayhead(entityID:path:frame:)` 供钻石切换

- [x] **Step 1：加 Core 辅助方法**

```swift
/// 在 `frame` 用属性当前求值路径添加 BezierPath 关键帧。
func addKeyframeBezierPathAtPlayhead(entityID: UInt64, path: String, frame: Int64) {
    guard let evaluated = ms_property_evaluate_bezier_path(handle, entityID, path, frame) else {
        return
    }
    defer { ms_bezier_path_free(evaluated) }
    ms_command_add_keyframe_bezier_path(handle, entityID, path, frame, evaluated)
    changed()
}
```

- [x] **Step 2：时间轴轨道**

在 `timelineAnimatedPropertyPaths` 的 cornerRadius 块之后：

```swift
if core.hasBezierPath(entityID: layerID, path: "path"),
   !core.keyframes(entityID: layerID, path: "path").isEmpty
{
    paths.append("path")
}
```

在 `timelineMaskTracks` 中，与 opacity/feather/expansion 并列增加候选  
`("masks[\(index)].path", "\(name) Path")`。

在 `buildTimelineRows` 的 size/cornerRadius 行之后：

```swift
if animatedPaths.contains("path") {
    rows.append(timelinePropertyRow(core: core, layerID: layerID, path: "path", label: "Path"))
}
```

Mask path 行：`timelineMaskTracks` 含 path 后，现有对 `timelineMaskTracks` 的循环会自动带上。

- [x] **Step 3：编译 App（无自动测试）**

优先 Xcode MCP `BuildProject`；否则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug \
  -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64" build
```

期望：BUILD SUCCEEDED。

- [x] **Step 4：暂不单独提交**（与 Task 6 App UI 一并提交，方便人机冒烟）。

---

### Task 6：App — Path / Mask Path 钻石 UI

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/PathKeyframeInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/MasksInspector.swift`
- Modify: `docs/superpowers/specs/2026-07-28-path-animation-roadmap.md`

**Interfaces:**
- Consumes: `MotionDocumentCore.addKeyframeBezierPathAtPlayhead`、`removeKeyframe`、`keyframeFrames`、`hasBezierPath`
- Produces: 与 Transform/Fill 同语义的钻石切换

- [x] **Step 1：PathKeyframeInspector**

仅当 `core.hasBezierPath(entityID: layerID, path: "path")` 时显示：

```swift
struct PathKeyframeInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let playheadFrame: Int64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    private let path = "path"

    var body: some View {
        let hasKeyframe = core.keyframeFrames(entityID: layerID, path: path)
            .contains(playheadFrame)
        HStack {
            Text("Path")
            Spacer()
            Button {
                toggle(hasKeyframe: hasKeyframe)
            } label: {
                Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                    .foregroundStyle(hasKeyframe ? .yellow : .secondary)
                    .id(hasKeyframe)
            }
            .buttonStyle(.plain)
            .disabled(!isEditable)
            .help(hasKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
        }
        .font(.callout)
        .id("path-kf-\(core.revision)-\(hasKeyframe)")
    }

    private func toggle(hasKeyframe: Bool) {
        guard isEditable else { return }
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeBezierPathAtPlayhead(entityID: layerID, path: path,
                                                     frame: playheadFrame)
            }
        }
    }
}
```

在 `InspectorView` 的 shape 相关区域挂载（仅 `hasBezierPath(..., "path")` 时）。

- [x] **Step 2：Mask path 钻石**

在 `MasksInspector` 标题行（铅笔旁）为 `masks[index].path` 加同样钻石，调用 `addKeyframeBezierPathAtPlayhead` / `removeKeyframe`。

- [x] **Step 3：编译 App**

同 Task 5。期望：BUILD SUCCEEDED。

- [x] **Step 4：更新 roadmap**

Phase A App UI → `ui-pending-verify`；Core/Bridge 保持 `core-done`。

- [x] **Step 5：提交 App + roadmap**（本地编译绿即可；人机验证仍待）

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/PathKeyframeInspector.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/MasksInspector.swift \
  docs/superpowers/specs/2026-07-28-path-animation-roadmap.md \
  -m "Add Path and Mask path keyframe UI and timeline tracks."
```

若需把新 Swift 文件加入 Xcode 工程 / pbxproj，一并列入 `--only`。

- [ ] **Step 6：人机验收清单**（用户确认前不要标 `done`）

- [x] Path / Mask Path 钻石可在 playhead 启用 / 取消关键帧  
- [x] 时间轴出现 Path / Mask Path 轨道菱形  
- [x] 两帧改形后播放形态连续变化  
- [x] ⌘Z 正确  

用户确认后，将 roadmap Phase A 两列都标为 `done`。

---

## Spec 覆盖对照

| Spec 要求 | Task |
|---|---|
| SceneEvaluator 中间帧形变 | Task 1 |
| Serializer path KF round-trip | Task 2 |
| 加/删关键帧 undo | Task 3 |
| Bridge 中间 evaluate + write 分支 | Task 4 |
| 时间轴 `path` / `masks[i].path` 轨道 | Task 5 |
| 钻石启用动画 | Task 6 |
| 钢笔已动画时 upsert | 钢笔已实现；Task 4 write 测试覆盖 |
| 不新增 Core 类型 / 不升 schema | 全部任务 |
| Roadmap 进度回写 | Task 4、6 |

## 本计划不做

Trim Path、Motion Path 可视化、Follow Path、顶点对应 UI、Lottie 导出完善。
