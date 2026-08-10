# Pen Inspector Vertex Position Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 钢笔选中点时 Inspector Position X/Y 显示/编辑该点场景坐标；ESC 先取消选点再退出钢笔。

**Architecture:** Bridge 暴露 local→scene 点变换（与 path edit 的 `ScenePointToLocal` 对称）；`MotionDocumentCore` 组合 `evaluateVectorNetwork` + 变换得到场景坐标；`TransformInspector` 在有选中点时切换 Position 读写到 `networkEditMoveVertex`；`exitPenTool` 两步取消。

**Tech Stack:** C++17 bridge、GoogleTest、SwiftUI Inspector、UIKit `EditorViewController`。

**Spec:** `docs/superpowers/specs/2026-08-10-pen-inspector-vertex-position-design.md`

## Global Constraints

- 坐标系：场景坐标显示/编辑；写入转局部（复用 `networkEditMoveVertex` / `ScenePointToLocal`）。
- 无选中点（`activeVertexId == 0`）：Position 仍绑包围盒 layout position。
- 选中点时 Position 关键帧按钮隐藏。
- ESC 仅改 `exitPenTool`；切 Select / toggle 钢笔仍直接 `finishPenTool()`。
- 读失败回退包围盒；标签仍为 Position X/Y。
- Commit：120 字符内英语、句号结尾；每 Task 同步本 plan checkbox + Status。

## File Map

| 文件 | 职责 |
|---|---|
| `bridge/include/motionstudio_bridge.h` | 声明 `ms_layer_transform_local_point` |
| `bridge/src/common/motionstudio_bridge_path_edit.cpp` | 实现：复用 `LayerWorldTransform` |
| `bridge/tests/BridgeTest.cpp` | local↔scene 往返测 |
| `apps/.../Model/MotionDocumentCore.swift` | `pathEditVertexScenePosition` + bridge 封装 |
| `apps/.../Inspector/TransformInspector.swift` | Position 条件切换 |
| `apps/.../Inspector/InspectorView.swift` | 传入 `pathEditTarget` |
| `apps/.../Editor/EditorViewController+Commands.swift` | ESC 两步 `exitPenTool` |

---

### Task 1: Bridge `ms_layer_transform_local_point` + 测试

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（靠近 `ms_layer_local_bounds` 声明区）
- Modify: `bridge/src/common/motionstudio_bridge_path_edit.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`
- Modify: `docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md`（勾选本 Task）

**Interfaces:**
- Produces:
```c
// Maps a layer-local point to composition scene space using the evaluated
// layer worldTransform (same source as path-edit ScenePointToLocal).
// Returns false when the layer is missing or has no evaluated world transform.
bool ms_layer_transform_local_point(MSDocument *document, uint64_t layerId, int64_t frame,
                                    float localX, float localY, float *sceneX, float *sceneY);
```
- Consumes: existing anonymous `LayerWorldTransform` in `motionstudio_bridge_path_edit.cpp`

- [x] **Step 1: Write the failing test**

在 `BridgeTest.cpp` 的 `BridgeVectorNetworkTest` 附近追加：

```cpp
TEST(BridgeVectorNetworkTest, TransformLocalPointMatchesSceneAddAndMove) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_path_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);

    // Translate layer so local != scene.
    ms_command_set_static_vec2(document, layerId, "transform.position", 40.0f, 60.0f);

    uint32_t vertexId = 0;
    ms_command_network_edit_add_vertex(document, layerId, MS_PATH_EDIT_SHAPE, 0, 0, 140.0f, 160.0f,
                                       &vertexId);
    ASSERT_NE(vertexId, 0u);

    MSVectorNetwork *network = ms_property_evaluate_vector_network(document, layerId, "path", 0);
    ASSERT_NE(network, nullptr);
    ASSERT_GE(network->vertexCount, 1u);
    float localX = 0.0f;
    float localY = 0.0f;
    bool found = false;
    for (size_t i = 0; i < network->vertexCount; ++i) {
        if (network->vertices[i].id == vertexId) {
            localX = network->vertices[i].x;
            localY = network->vertices[i].y;
            found = true;
            break;
        }
    }
    ms_vector_network_free(network);
    ASSERT_TRUE(found);

    float sceneX = 0.0f;
    float sceneY = 0.0f;
    ASSERT_TRUE(ms_layer_transform_local_point(document, layerId, 0, localX, localY, &sceneX, &sceneY));
    EXPECT_NEAR(sceneX, 140.0f, 1e-3f);
    EXPECT_NEAR(sceneY, 160.0f, 1e-3f);

    // Move via scene API then re-read scene position.
    ms_command_network_edit_move_vertex(document, layerId, MS_PATH_EDIT_SHAPE, 0, 0, vertexId, 200.0f,
                                        220.0f);
    network = ms_property_evaluate_vector_network(document, layerId, "path", 0);
    ASSERT_NE(network, nullptr);
    found = false;
    for (size_t i = 0; i < network->vertexCount; ++i) {
        if (network->vertices[i].id == vertexId) {
            localX = network->vertices[i].x;
            localY = network->vertices[i].y;
            found = true;
            break;
        }
    }
    ms_vector_network_free(network);
    ASSERT_TRUE(found);
    ASSERT_TRUE(ms_layer_transform_local_point(document, layerId, 0, localX, localY, &sceneX, &sceneY));
    EXPECT_NEAR(sceneX, 200.0f, 1e-3f);
    EXPECT_NEAR(sceneY, 220.0f, 1e-3f);

    EXPECT_FALSE(ms_layer_transform_local_point(document, 999999u, 0, 0.0f, 0.0f, &sceneX, &sceneY));

    ms_document_destroy(document);
}
```

- [x] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target bridge_test
./build/bridge/tests/bridge_test --gtest_filter='BridgeVectorNetworkTest.TransformLocalPointMatchesSceneAddAndMove'
```

Expected: FAIL（符号未定义或链接失败）

> 若 binary 路径不同，用 `find build -name bridge_test` 或 `ctest --test-dir build -R TransformLocalPoint --output-on-failure`。

- [x] **Step 3: Declare + implement**

`motionstudio_bridge.h`（`ms_layer_local_bounds` 后）：

```c
// Maps a layer-local point to composition scene space using the evaluated
// layer worldTransform (same source as path-edit ScenePointToLocal).
// Returns false when the layer is missing or has no evaluated world transform.
bool ms_layer_transform_local_point(MSDocument *document, uint64_t layerId, int64_t frame,
                                    float localX, float localY, float *sceneX, float *sceneY);
```

`motionstudio_bridge_path_edit.cpp`（`LayerWorldTransform` 旁，匿名 namespace外）：

```cpp
bool ms_layer_transform_local_point(MSDocument *document, uint64_t layerId, int64_t frame,
                                    float localX, float localY, float *sceneX, float *sceneY) {
    DocumentLock guard(document);
    if (document == nullptr || sceneX == nullptr || sceneY == nullptr) {
        return false;
    }
    motion::Mat3 world = motion::Mat3::Identity();
    if (!LayerWorldTransform(*document->document, EntityId{layerId},
                             static_cast<FrameTime>(frame), world)) {
        return false;
    }
    const Vec2 scene = world.transformPoint(Vec2{localX, localY});
    *sceneX = scene.x;
    *sceneY = scene.y;
    return true;
}
```

（若 `DocumentLock` / `document->document` 空指针检查与文件内其它 API 风格不一致，对齐同文件现有写法。）

- [x] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target bridge_test
./build/bridge/tests/bridge_test --gtest_filter='BridgeVectorNetworkTest.TransformLocalPointMatchesSceneAddAndMove'
```

Expected: PASS

- [x] **Step 5: Commit**

同步本 plan Task 1 全部 checkbox → `[x]`，`**Status:** ✅ Done`。

```bash
git commit --only \
  bridge/include/motionstudio_bridge.h \
  bridge/src/common/motionstudio_bridge_path_edit.cpp \
  bridge/tests/BridgeTest.cpp \
  docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md \
  -m "Add bridge local-to-scene point transform for path vertices."
```

---

### Task 2: `MotionDocumentCore.pathEditVertexScenePosition`

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（靠近 `evaluateVectorNetwork` / path-edit API）
- Modify: `docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md`

**Interfaces:**
- Consumes: `ms_layer_transform_local_point`, `evaluateVectorNetwork`
- Produces:
```swift
/// Scene-space position of a path vertex; nil when missing.
func pathEditVertexScenePosition(layerID: UInt64, path: String, frame: Int64,
                                 vertexId: UInt32) -> CGPoint?
```

- [x] **Step 1: Add Swift wrapper**

```swift
func pathEditVertexScenePosition(layerID: UInt64, path: String, frame: Int64,
                                 vertexId: UInt32) -> CGPoint?
{
    guard vertexId != 0,
          let network = evaluateVectorNetwork(entityID: layerID, path: path, frame: frame),
          let vertex = network.vertices.first(where: { $0.id == vertexId })
    else {
        return nil
    }
    var sceneX: Float = 0
    var sceneY: Float = 0
    guard ms_layer_transform_local_point(handle, layerID, frame, vertex.x, vertex.y, &sceneX, &sceneY)
    else {
        return nil
    }
    return CGPoint(x: CGFloat(sceneX), y: CGFloat(sceneY))
}
```

- [x] **Step 2: Smoke-build App libraries if gen_xcode 已就绪**

优先 Xcode MCP `BuildProject`；否则：

```bash
# 仅确认 bridging header 可见；完整 App 构建可放到 Task 3
apps/gen_mac
```

若环境无 Xcode，至少确保 Task 1 bridge 测已过，本步以代码审查为准。

- [x] **Step 3: Commit**

同步 plan Task 2。

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md \
  -m "Add pathEditVertexScenePosition helper on MotionDocumentCore."
```

---

### Task 3: TransformInspector Position 条件切换

**Status:** ⏳ Pending

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift`
- Modify: `docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md`

**Interfaces:**
- Consumes: `pathEditVertexScenePosition`, `networkEditMoveVertex`, `PathEditTarget`
- Produces: Position 行在有选中点时绑顶点场景坐标并隐藏关键帧按钮

- [ ] **Step 1: 扩展 `TransformInspector` 入参与 Position 分支**

在 `TransformInspector` 增加：

```swift
let pathEditTarget: PathEditTarget?
```

`body` 内 Position 逻辑（保留 Anchor/Scale/Rotation/Opacity 不变）：

```swift
let editingVertex = pathEditTarget.map { target in
    target.layerID == layerID && target.activeVertexId != 0
} ?? false

let vertexScene: CGPoint? = {
    guard editingVertex, let target = pathEditTarget else { return nil }
    return core.pathEditVertexScenePosition(layerID: target.layerID,
                                            path: target.propertyPath,
                                            frame: playheadFrame,
                                            vertexId: target.activeVertexId)
}()

let useVertexPosition = vertexScene != nil
let displayX: Float
let displayY: Float
let positionShowsKeyframe: Bool
let positionRowEditable: Bool

if let vertexScene, useVertexPosition {
    displayX = Float(vertexScene.x)
    displayY = Float(vertexScene.y)
    positionShowsKeyframe = false
    positionRowEditable = isEditable
} else {
    let layout = core.evaluateLayoutPosition(compositionID: compositionID,
                                             layerID: layerID,
                                             frame: playheadFrame)
    displayX = Float(layout.dx)
    displayY = Float(layout.dy)
    positionShowsKeyframe = true
    positionRowEditable = positionEditable  // 现有：isEditable && !followEnabled
}

NumberPropertyRow(label: TransformField.positionX.label,
                  value: displayX,
                  hasKeyframeAtPlayhead: positionShowsKeyframe ? hasKeyframe(.position) : false,
                  isEditable: positionRowEditable,
                  showsKeyframeButton: positionShowsKeyframe,
                  showsStepButtons: true)
{ newValue in
    if useVertexPosition, let target = pathEditTarget, let vertexScene {
        perform("Move Vertex") {
            core.networkEditMoveVertex(layerID: target.layerID, kind: target.kind,
                                       maskIndex: target.maskIndex, frame: playheadFrame,
                                       vertexId: target.activeVertexId,
                                       scenePoint: CGPoint(x: CGFloat(newValue),
                                                           y: vertexScene.y))
        }
    } else {
        setLayoutPosition(CGVector(dx: CGFloat(newValue), dy: CGFloat(displayY)))
    }
} onToggleKeyframe: { _ in
    guard positionShowsKeyframe else { return }
    toggleVec2Keyframe(.position)
}

// Position Y 对称：x 用 vertexScene.x / displayX，y 用 newValue
```

注意：`NumberPropertyRow` 已有 `showsKeyframeButton`（默认 `true`），选中点时传 `false`。

- [ ] **Step 2: `InspectorView` 传入 target**

替换现有 `TransformInspector(...)` 调用：

```swift
TransformInspector(core: core,
                   compositionID: core.firstCompositionID,
                   layerID: layerID,
                   pathEditTarget: {
                       guard editorState.tool == .pen,
                             let target = editorState.pathEditTarget,
                             target.layerID == layerID
                       else {
                           return nil
                       }
                       return target
                   }(),
                   isEditable: isEditable,
                   perform: perform)
```

- [ ] **Step 3: Build App（Xcode MCP 优先）**

探测 `user-xcode` → `XcodeListWindows` → `BuildProject`。  
MCP 不可用时：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug \
  -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64"
```

Expected: BUILD SUCCEEDED

- [ ] **Step 4: 手动验证清单（实现者勾选）**

1. 钢笔选点 → Position ≈ 画布该点场景位置  
2. 改 Position X/Y → 点移动，不是整层包围盒平移意图  
3. 画布拖点 → Inspector 同步  
4. 取消选点 → Position 回到包围盒，关键帧按钮恢复  

- [ ] **Step 5: Commit**

同步 plan Task 3。

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift \
  docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md \
  -m "Bind inspector Position to selected pen vertex in scene space."
```

---

### Task 4: ESC 先取消选中点

**Status:** ⏳ Pending

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift`（`exitPenTool`）
- Modify: `docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md`
- Modify: `docs/superpowers/specs/2026-08-10-pen-inspector-vertex-position-design.md`（状态改为已实现）

**Interfaces:**
- Consumes: `PathEditTarget.clearVertexSelection()`, `finishPenTool()`
- Produces: 两步 ESC 行为

- [ ] **Step 1: 改写 `exitPenTool`**

```swift
@objc func exitPenTool() {
    guard editorState.tool == .pen else {
        return
    }
    if var target = editorState.pathEditTarget, target.activeVertexId != 0 {
        target.clearVertexSelection()
        editorState.pathEditTarget = target
        return
    }
    finishPenTool()
}
```

勿改 `finishPenTool` / `activateSelectTool` / `togglePenTool` 的一步退出语义。

- [ ] **Step 2: Build App 确认编译**

同 Task 3 构建路径。Expected: BUILD SUCCEEDED

- [ ] **Step 3: 手动验证**

1. 选中点 → ESC → 仍钢笔、无选中、tangent chrome 消失、Position 回包围盒  
2. 再 ESC → 退出钢笔（shape 仍会 recenter）  
3. 无选中时 ESC → 一次退出钢笔  
4. 有选中时点 Select 工具 → 仍一步退出钢笔  

- [ ] **Step 4: Commit**

同步 plan Task 4 全部完成；spec 状态改为「已实现」。

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift \
  docs/superpowers/plans/2026-08-10-pen-inspector-vertex-position.md \
  docs/superpowers/specs/2026-08-10-pen-inspector-vertex-position-design.md \
  -m "Make Escape clear pen vertex selection before leaving pen tool."
```

---

## Spec Coverage Checklist

| Spec 要求 | Task |
|---|---|
| 选中点 Position = 场景坐标 | Task 2 + 3 |
| 写入场景→局部 | Task 3（`networkEditMoveVertex`） |
| 无选中点 = 包围盒 | Task 3 |
| 隐藏关键帧按钮 | Task 3 |
| ESC 两步 | Task 4 |
| 切工具一步退出 | Task 4（不改） |
| 读失败回退 | Task 3（`vertexScene == nil`） |
| Mask/Shape 共用 | Task 3（`pathEditTarget.propertyPath` / kind） |
