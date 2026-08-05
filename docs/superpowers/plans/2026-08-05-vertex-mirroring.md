# Vertex Mirroring（Inspector 三段式）— 实现计划

> **给执行代理：** 必须使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 去掉双击顶点切换平滑；在 Inspector 用 Figma 式三段 Mirroring 控件设置并持久化 `Vertex.mirrorMode`；拖切线按 mode 约束。

**架构：** `VertexMirrorMode` 挂在 `VectorNetwork::Vertex` 上并序列化。`SetVertexMirrorMode` 立刻改几何；`MoveEdgeTangent(..., mirror)` 在 `mirror==true` 时按该点 mode 约束对侧（Alt → `mirror=false` 仅改本侧）。App 只负责 UI 与去掉双击。

**技术栈：** C++17 core、Apple C bridge、SwiftUI Inspector、GoogleTest。

**Spec：** `docs/superpowers/specs/2026-08-05-vertex-mirroring-design.md`

## 全局约束

- 分支：留在当前 feature 分支（如 `feature/pen_optimize`）；未经明确要求不得往 `master` 提交。
- **禁止自动 commit。** 除非用户明确要求提交，否则跳过所有 Commit 步骤。
- 按本 plan 实现时：每完成一个 Step/Task 立刻把 checkbox 改为 `[x]`，并更新 `**Status:**`（即使未 commit）。
- Commit 信息（仅当用户要求提交时）：英语、≤120 字符、句号结尾、句中无其他标点。
- `schemaVersion` 保持为 `1`。
- 宣称 Core 完成前优先 ASan 构建：
  `cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON && cmake --build build`
- 遵守现有编码规范（禁异常、禁 `dynamic_cast`、错误用 Expected）。

---

## 文件对照

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/common/VectorNetwork.h` | `VertexMirrorMode` + `Vertex.mirrorMode` |
| `src/common/VectorNetwork.cpp` | Vertex 相等含 `mirrorMode` |
| `include/MotionStudio/common/VectorNetworkEdit.h` | `SetVertexMirrorMode`；更新 `MoveEdgeTangent` 注释；删除 `ToggleVertexSmooth` |
| `src/common/VectorNetworkEdit.cpp` | 实现 set / mode-aware move；删 toggle |
| `src/serialization/Serializer.cpp` | 读写 `mirrorMode` |
| `src/animation/Interpolator.cpp` | Lerp 保留左关键帧 `mirrorMode`（已由 `result = from` 隐式满足，加测试钉死） |
| `bridge/include/motionstudio_bridge.h` | `MS_VERTEX_MIRROR`、vertex 字段、`set_mirror_mode`；删/替 `toggle_smooth` |
| `bridge/src/common/BridgeInternals.cpp` | ABI ↔ Core 互转 `mirrorMode` |
| `bridge/src/common/motionstudio_bridge_path_edit.cpp` | set 命令；删 toggle 实现 |
| `apps/.../Canvas/CanvasViewController.swift` | 去掉双击 toggle |
| `apps/.../Inspector/VertexMirroringInspector.swift`（新建） | 三段控件 |
| `apps/.../Inspector/InspectorView.swift` | 嵌入 mirroring |
| `apps/.../Model/MotionDocumentCore.swift` | set / read API |
| `tests/common/VectorNetworkEditTest.cpp` | set + move 测试；改原 toggle 用例 |
| `tests/serialization/SerializerTest.cpp` | round-trip / 缺字段 |
| `tests/animation/VectorNetworkMorphTest.cpp` | hold mirrorMode |
| `bridge/tests/BridgeTest.cpp` | set_mirror_mode；改 toggle 用例 |
| Spec / pen docs | 去掉「双击 toggle」表述 |

---

### Task 1: `VertexMirrorMode` + `SetVertexMirrorMode`

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/common/VectorNetwork.h`
- Modify: `src/common/VectorNetwork.cpp`
- Modify: `include/MotionStudio/common/VectorNetworkEdit.h`
- Modify: `src/common/VectorNetworkEdit.cpp`
- Modify: `tests/common/VectorNetworkEditTest.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class VertexMirrorMode : uint8_t { None = 0, Angle = 1, AngleLength = 2 };
  // Vertex { uint32_t id; Vec2 point; VertexMirrorMode mirrorMode = None; }
  VectorNetwork SetVertexMirrorMode(VectorNetwork network, uint32_t vertexId,
                                    VertexMirrorMode mode);
  ```
- Consumes: 现有 `VertexDegree` / `FindVertex` / `HandleAtVertex` / `SetHandleAtVertex` / `SafeNormalize`

- [x] **Step 1: 编写会失败的测试**

在 `tests/common/VectorNetworkEditTest.cpp` 增加（并 `using motion::SetVertexMirrorMode; using motion::VertexMirrorMode;`）：

```cpp
VectorNetwork MakeDegreeTwoChain() {
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}, {3, {20, 0}}};
    network.edges = {{1, 1, 2, {}, {}}, {2, 2, 3, {}, {}}};
    return network;
}

TEST(VectorNetworkEditTest, SetMirrorModeNoneClearsHandles) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::Angle);
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::None);
    EXPECT_EQ(FindVertex(network, 2)->mirrorMode, VertexMirrorMode::None);
    EXPECT_TRUE(IsNearZeroVec(HandleAt(network, 1, 2)));  // helper or inline
    EXPECT_TRUE(IsNearZeroVec(HandleAt(network, 2, 2)));
}

TEST(VectorNetworkEditTest, SetMirrorModeAngleGeneratesIndependentLengths) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::Angle);
    EXPECT_EQ(FindVertex(network, 2)->mirrorMode, VertexMirrorMode::Angle);
    // Handles collinear opposite; lengths = chord/3 each (= 10/3).
    // Assert direction via cross≈0 and opposite signs; lengths approx 10/3.
}

TEST(VectorNetworkEditTest, SetMirrorModeAngleLengthEqualizes) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::AngleLength);
    // Both handle lengths equal (10/3 + 10/3)/2 = 10/3 on this chain.
}

TEST(VectorNetworkEditTest, SetMirrorModeDegreeNotTwoWritesModeOnly) {
    // Hub degree 3: set AngleLength → mirrorMode updated, handles unchanged (still zero).
}
```

测试里可用文件内小 helper 取 handle，勿引入新公共 API。

- [x] **Step 2: 跑测试 — 预期编译失败**

```bash
cmake --build build --target core_tests -j8 2>&1 | tail -40
```

Expected: `SetVertexMirrorMode` / `VertexMirrorMode` 未声明。

- [x] **Step 3: 实现类型 + SetVertexMirrorMode**

`VectorNetwork.h`：

```cpp
enum class VertexMirrorMode : uint8_t {
    None = 0,
    Angle = 1,
    AngleLength = 2,
};

struct Vertex {
    uint32_t id = 0;
    Vec2 point = {};
    VertexMirrorMode mirrorMode = VertexMirrorMode::None;
    ...
};
```

`VectorNetwork.cpp`：`Vertex::operator==` 加入 `mirrorMode`。

`VectorNetworkEdit.h`：声明 `SetVertexMirrorMode`；**暂留** `ToggleVertexSmooth`（Task 2 再删）。

`SetVertexMirrorMode` 逻辑（写在 `VectorNetworkEdit.cpp`）：

1. 找顶点；缺失则原样返回。
2. **始终** `vertex->mirrorMode = mode`。
3. 若 `VertexDegree != 2`：返回（不改 handle）。
4. 定位两条 incident edges；解析 incoming（`end==id`）/ outgoing（`start==id`），否则用邻居弦方向。
5. `None`：两侧 handle 清零。
6. `Angle`：`direction = normalize(next-prev)`；`inLen = |p-prev|/3`；`outLen = |next-p|/3`；设 `incoming.endTangent = -direction*inLen`，`outgoing.startTangent = direction*outLen`（非定向链用 `SetHandleAtVertex` 对称写法，对齐现 `ToggleVertexSmooth`）。
7. `AngleLength`：同方向；`len = (inLen+outLen)*0.5f`；两侧均用 `len`。

- [x] **Step 4: 跑测试 — 预期 PASS**

```bash
./build/tests/core_tests --gtest_filter='VectorNetworkEditTest.SetMirrorMode*'
```

Expected: PASS。

- [x] **Step 5: Commit** — **跳过**，除非用户要求。

---

### Task 2: `MoveEdgeTangent` 按 mode + 删除 `ToggleVertexSmooth`

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/common/VectorNetworkEdit.h`
- Modify: `src/common/VectorNetworkEdit.cpp`
- Modify: `tests/common/VectorNetworkEditTest.cpp`
- Modify: 所有 `ToggleVertexSmooth(VectorNetwork` 调用方（本 Task 只改 Core 测试；Bridge 在 Task 4）

**Interfaces:**
- Produces: 更新后的
  ```cpp
  VectorNetwork MoveEdgeTangent(VectorNetwork, uint32_t edgeId, bool atStart,
                                Vec2 tangent, bool mirror);
  ```
  `mirror==false`：只改本侧，不改 `mirrorMode`。  
  `mirror==true`：按该端点 `mirrorMode`（None=只改本侧；Angle=对侧反向保长；AngleLength=对侧=-tangent）。度数≠2 忽略镜像。
- Removes: `ToggleVertexSmooth`（Network 版）

- [x] **Step 1: 编写会失败的测试**

```cpp
TEST(VectorNetworkEditTest, MoveEdgeTangentAnglePreservesOppositeLength) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::Angle);
    // Drag outgoing handle to length 6 along +X.
    network = MoveEdgeTangent(network, /*edge 2*/ 2, /*atStart*/ true, {6, 0}, true);
    // Other handle at vertex 2 should be approximately {-10/3, 0} length preserved from set,
    // or if set made both 10/3, opposite length stays 10/3 → {-10/3, 0}.
}

TEST(VectorNetworkEditTest, MoveEdgeTangentAngleLengthNegates) {
    network = SetVertexMirrorMode(..., AngleLength);
    network = MoveEdgeTangent(..., {4, 0}, true);
    // Opposite ≈ {-4, 0}
}

TEST(VectorNetworkEditTest, MoveEdgeTangentMirrorFalseDoesNotTouchOther) {
    // Set AngleLength, then Move with mirror=false → only dragged side changes.
}

TEST(VectorNetworkEditTest, MoveEdgeTangentNoneIgnoresMirrorFlagForOtherSide) {
    // mirrorMode None, mirror=true → still only this side (Figma corner).
}
```

把原 `ToggleSmooth*` 测试改为 `SetVertexMirrorMode` 等价断言，或删除。

- [x] **Step 2: 跑测试 — 预期 FAIL（旧 Move 行为是恒 `-tangent`）**

```bash
./build/tests/core_tests --gtest_filter='VectorNetworkEditTest.MoveEdgeTangent*'
```

- [x] **Step 3: 实现 MoveEdgeTangent + 删除 ToggleVertexSmooth**

伪代码：

```cpp
// after setting this side's tangent:
if (!mirror) return network;
const Vertex* v = FindVertex(network, vertexId);
if (v == nullptr || VertexDegree(...) != 2) return network;
Edge* other = FindEdge(...OtherIncidentEdgeId...);
switch (v->mirrorMode) {
  case None: return network;
  case Angle: {
    Vec2 opp = HandleAtVertex(*other, vertexId);
    float len = IsNearZero(opp) ? length(tangent) : length(opp);
    Vec2 dir = SafeNormalize(Vec2{-tangent.x, -tangent.y});
    SetHandleAtVertex(*other, vertexId, dir * len);
    break;
  }
  case AngleLength:
    SetHandleAtVertex(*other, vertexId, Vec2{-tangent.x, -tangent.y});
    break;
}
```

删除 `ToggleVertexSmooth` 声明与实现。

- [x] **Step 4: 全量 VectorNetworkEdit 测试 PASS**

```bash
./build/tests/core_tests --gtest_filter='VectorNetworkEditTest.*'
```

- [x] **Step 5: Commit** — **跳过**，除非用户要求。

---

### Task 3: 序列化 + Morph hold `mirrorMode`

**Status:** ✅ Done

**Files:**
- Modify: `src/serialization/Serializer.cpp`（`VectorNetworkToJson` / `VectorNetworkFromJson`）
- Modify: `tests/serialization/SerializerTest.cpp`
- Modify: `tests/animation/VectorNetworkMorphTest.cpp`
- Note: `Interpolator` 已 `result = from` 再改 point；`mirrorMode` 自然 hold。加测试即可。若将来改成从 `to` 拷顶点，需显式 `vertex.mirrorMode = from.mirrorMode`。

- [x] **Step 1: 失败测试**

```cpp
TEST(SerializerTest, RoundTripVertexMirrorMode) {
    VectorNetwork network;
    network.vertices = {{1, {0, 0}, VertexMirrorMode::AngleLength},
                        {2, {1, 0}, VertexMirrorMode::None}};
    network.edges = {{1, 1, 2, {}, {}}};
    // save/load document or VectorNetwork JSON path used by SerializerTest helpers
    // EXPECT round-trip mirrorMode
}

TEST(SerializerTest, MissingMirrorModeDefaultsToNone) {
    // Parse JSON vertex object without mirrorMode field → None
}

TEST(VectorNetworkMorphTest, MirrorModeHoldsFromLeftKeyframe) {
    VectorNetwork from = ...; from.vertices[1].mirrorMode = Angle;
    VectorNetwork to = from; to.vertices[1].mirrorMode = AngleLength;
    to.vertices[1].point = {20, 0};
    auto mid = Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    EXPECT_EQ(FindVertex(mid, midId)->mirrorMode, VertexMirrorMode::Angle);
}
```

- [x] **Step 2: 实现 JSON**

写：

```cpp
vertices.push_back({
  {"id", vertex.id},
  {"point", Vec2ToJson(vertex.point)},
  {"mirrorMode", static_cast<int>(vertex.mirrorMode)},
});
```

读：若存在 `mirrorMode` 且为 0/1/2 则用，否则 `None`。`push_back({*id, *point})` 改为带 mode 的 Vertex。

- [x] **Step 3: 跑测试 PASS**

```bash
./build/tests/core_tests --gtest_filter='SerializerTest.*Mirror*|VectorNetworkMorphTest.MirrorMode*'
```

- [x] **Step 4: Commit** — **跳过**，除非用户要求。

---

### Task 4: Bridge ABI + `set_mirror_mode`，移除 toggle_smooth

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/BridgeInternals.cpp`（`AllocateMSVectorNetwork` / `FromMSVectorNetwork`）
- Modify: `bridge/src/common/motionstudio_bridge_path_edit.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`
- Modify: `apps/.../Model/MotionDocumentCore.swift`（Swift 侧 API；可与 Task 5 同改，本 Task 至少声明 C API）
- Optional: 保留 `ms_bezier_toggle_vertex_smooth`（Bezier 纯函数）不动；**删除** `ms_command_path_edit_toggle_smooth`

**Interfaces:**
```c
typedef CF_CLOSED_ENUM(int, MS_VERTEX_MIRROR) {
    MS_VERTEX_MIRROR_NONE = 0,
    MS_VERTEX_MIRROR_ANGLE = 1,
    MS_VERTEX_MIRROR_ANGLE_LENGTH = 2,
};

typedef struct MSVectorNetworkVertex {
    uint32_t id;
    float x;
    float y;
    MS_VERTEX_MIRROR mirrorMode;
} MSVectorNetworkVertex;

void ms_command_path_edit_set_mirror_mode(MSDocument *document, uint64_t layerId,
                                          MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                          uint32_t vertexId, MS_VERTEX_MIRROR mode);
```

读 mode：通过已有 `ms_property_get_vector_network`（或等价）取网络后读 vertex；不必单独 query API。

- [x] **Step 1: Bridge 失败测试**

改 `BridgeVectorNetworkTest.ToggleSmoothSharedHubIsNoOp` → `SetMirrorModeSharedHubWritesModeWithoutGeometry`：对 hub `set_mirror_mode(ANGLE)`，断言 handles 仍近零、mode 为 ANGLE。

新增度数=2 set Angle 后 handles 非零。

删除对 `ms_command_path_edit_toggle_smooth` 的调用。

- [x] **Step 2: 实现 ABI + 命令**

命令体：读 network → `SetVertexMirrorMode` → 写回（与其它 path_edit 相同 playhead 策略）。

互转：`mirrorMode` 字段；旧调用方若栈上构造 `MSVectorNetworkVertex` 需补零（Swift / FreeTransformDrag）。

- [x] **Step 3:**

```bash
cmake --build build --target bridge_test -j8
./build/bridge/bridge_test --gtest_filter='BridgeVectorNetworkTest.*'
```

Expected: PASS。修复所有因 vertex 结构体增字段导致的编译错误。

- [x] **Step 4: Commit** — **跳过**，除非用户要求。

---

### Task 5: App — 去掉双击 + Inspector Mirroring

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Canvas/CanvasViewController.swift`（删 `lastPenVertexClick*` / 双击 toggle 分支）
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/VertexMirroringInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Xcode：把新 Swift 文件加入 `MotionStudioApp` target（若工程为 folder sync 则自动；否则手动加）

**Interfaces (Swift):**
```swift
func pathEditSetMirrorMode(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int,
                           frame: Int64, vertexId: UInt32, mode: MS_VERTEX_MIRROR)
func networkVertexMirrorMode(...) -> MS_VERTEX_MIRROR  // from get vector network
func networkVertexDegree(...) -> Int                  // reuse existing if any; else count edges
```

- [x] **Step 1: 删除双击平滑**

在 `endPenPress`（或等价）删除：

- `lastPenVertexClickTime` / `lastPenVertexClickIndex` / `penDoubleClickInterval`
- 双击调用 `pathEditToggleSmooth` 的分支

单击选点 / 续画逻辑保持不变。

删除 `pathEditToggleSmooth` Swift 包装（若无其它调用）。

- [x] **Step 2: MotionDocumentCore 包装**

```swift
func pathEditSetMirrorMode(...) {
    ms_command_path_edit_set_mirror_mode(handle, ...)
    changed()
}
```

读 mode：`getVectorNetwork` → 找 `vertexId` → `mirrorMode`。

度数：遍历 edges 计数，或已有 helper。

- [x] **Step 3: `VertexMirroringInspector`**

显示条件（由父视图传入）：`tool == .pen`、`pathEditTarget` 有 `activeVertexId != 0`、可编辑。

```swift
struct VertexMirroringInspector: View {
    // core, layerID, kind, maskIndex, vertexId, degree, isEditable, perform
    var body: some View {
        VStack(alignment: .leading) {
            Text("Mirroring").font(.caption).foregroundStyle(.secondary)
            Picker("", selection: binding) {
                Image(...).tag(MS_VERTEX_MIRROR_NONE)           // 角点 SF Symbol 或自定义
                Image(...).tag(MS_VERTEX_MIRROR_ANGLE)
                Image(...).tag(MS_VERTEX_MIRROR_ANGLE_LENGTH)
            }
            .pickerStyle(.segmented)
            .disabled(!isEditable || degree != 2)
        }
    }
}
```

图标：可用 SF Symbols 近似（如 `point.topleft.down.to.point.bottomright.curvepath` 等），不必像素级复制 Figma。

`InspectorView`：在 `PathKeyframeInspector` 附近，当 `editorState.tool == .pen` 且 target 匹配当前 layer（shape 或 mask）且有选中顶点时嵌入。

- [x] **Step 4: 编译 App**

优先 Xcode MCP `BuildProject`（`MotionStudio.xcworkspace`）；不可用则 `xcodebuild` Catalyst。

Expected: BUILD SUCCEEDED。

- [x] **Step 5: 人机验收（执行代理勾选前请用户确认或自行点验）**

对照 spec 验收 1–5。

- [x] **Step 6: Commit** — **跳过**，除非用户要求。

---

### Task 6: 文档同步

**Status:** ✅ Done

**Files:**
- Modify: `docs/superpowers/specs/2026-08-05-vertex-mirroring-design.md` — 状态改为已实现（或实现中）
- Modify: `docs/superpowers/specs/2026-08-05-vector-network-pen-design.md` — Mirroring 行改为三段 Inspector；删「双击」若有
- Modify: `docs/superpowers/specs/2026-07-28-pen-path-edit-design.md` — 双击顶点行改为 Inspector Mirroring
- Modify: 本 plan 全部 Task Status / checkbox

- [x] **Step 1: 改上述文档中过时「双击 ToggleSmooth」表述**
- [x] **Step 2: 本 plan 全部 Task 标 ✅ Done**
- [x] **Step 3: Commit** — **跳过**，除非用户要求。

---

## Spec 覆盖自检

| Spec 项 | Task |
|---|---|
| 三段 mode 持久化 | 1, 3 |
| Set 立刻改几何 | 1 |
| Move 按 mode；Alt 临时断开 | 2（App 仍传 `!alt`） |
| 默认 None；度数≠2 UI 禁用 / 几何 no-op | 1, 5 |
| Morph hold mode | 3 |
| 不升 schema；缺字段 None | 3 |
| 去掉双击；Inspector | 5 |
| Bridge set API | 4 |
| 文档 | 6 |
