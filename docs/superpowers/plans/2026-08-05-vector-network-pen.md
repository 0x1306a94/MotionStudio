# Vector Network 钢笔续画 — 实现计划

> **给执行代理：** 必须使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 用 `VectorNetwork` 替换权威创作模型中的 `BezierPath`，使钢笔闭合后仍可续画（Figma 式共享顶点、面填充、边描边）。

**架构：** `ShapePath` / Mask 权威类型为 `Animatable<VectorNetwork>`。求值时编译为多轮廓 `BezierPath`（fill 面）以及独立的边 stroke path。Morph 在 Network 上按同拓扑插值；导出拍平为 Path（动画优先 StrokeEdges，静态优先 FillFaces）。`schemaVersion` 保持 1，JSON 双读静默转换。

**技术栈：** C++17 core、Apple C bridge、Swift/UIKit App、GoogleTest、tgfx adapter。

**Spec：** `docs/superpowers/specs/2026-08-05-vector-network-pen-design.md`

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
| `include/MotionStudio/common/VectorNetwork.h` | 创作图：顶点 + 边 |
| `src/common/VectorNetwork.cpp` | 相等比较、辅助（度数、查找、分配 id） |
| `include/MotionStudio/common/VectorNetworkConvert.h` | 旧单环 BezierPath ↔ Network |
| `src/common/VectorNetworkConvert.cpp` | 静默转换实现 |
| `include/MotionStudio/common/VectorNetworkCompile.h` | `CompileFillFaces` / `CompileStrokeEdges` |
| `src/common/VectorNetworkCompile.cpp` | 绕面行走 + stroke 轮廓 |
| `include/MotionStudio/common/VectorNetworkEdit.h` | 拓扑编辑（加/移/插/删/镜像） |
| `src/common/VectorNetworkEdit.cpp` | 编辑操作实现 |
| `include/MotionStudio/common/BezierPath.h` | 多轮廓编译 DTO（+ 辅助函数） |
| `src/common/BezierPath.cpp` | 轮廓辅助 / isZero |
| `adapter/tgfx/src/TgfxPathBuilder.cpp` | 多轮廓 → tgfx::Path |
| `include/MotionStudio/model/ShapePath.h` / `Layer.h`（Mask） | `Animatable<VectorNetwork>` |
| `include/MotionStudio/undo/PropertyValue.h` | variant 持有 `VectorNetwork` |
| `include/MotionStudio/animation/AnimatableType.h` | 路径枚举改为 `VectorNetwork` |
| `src/animation/Animatable.cpp` / `Interpolator.*` | 显式实例化 + Network Lerp |
| `src/serialization/Serializer.cpp` | 路径 JSON 双读；写出 Network |
| `src/render/SceneEvaluator.cpp` | 求值 Network → fill/stroke 几何 |
| `include/MotionStudio/render/ShapeGeometry.h` | 可选 strokePath |
| `include/MotionStudio/render/PathEditHandles.h` | 基于 Network 的 hit / chrome |
| `src/render/PathEditHandles.cpp` | Network 手柄实现 |
| `bridge/include/motionstudio_bridge.h` | `MSVectorNetwork` + 编辑命令 |
| `bridge/src/common/motionstudio_bridge_path_edit.cpp` | 命令写回 |
| App Swift 钢笔手势 / `PathEditTarget` | `activeVertexId` + `drawing` 会话 |
| 导出（PAG） | Network 拍平为 Path（动画→StrokeEdges；静态→FillFaces） |
| `tests/common/`、`tests/animation/`、`tests/serialization/`、`tests/render/`、`bridge/tests/` | 各 Task 覆盖测试 |

`src/common/`、`tests/common/` 由 CMake 按扩展名自动收集——新增 `.cpp` **无需**改 CMake 列表。

---

### Task 1: VectorNetwork 类型 + BezierPath→Network 转换

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/common/VectorNetwork.h`
- Create: `src/common/VectorNetwork.cpp`
- Create: `include/MotionStudio/common/VectorNetworkConvert.h`
- Create: `src/common/VectorNetworkConvert.cpp`
- Test: `tests/common/VectorNetworkTest.cpp`

**Interfaces:**
- Produces:
  - `struct VectorNetwork { struct Vertex { uint32_t id; Vec2 point; }; struct Edge { uint32_t id; uint32_t start; uint32_t end; Vec2 startTangent; Vec2 endTangent; }; std::vector<Vertex> vertices; std::vector<Edge> edges; };`
  - `int VertexDegree(const VectorNetwork&, uint32_t vertexId);`
  - `const VectorNetwork::Vertex* FindVertex(const VectorNetwork&, uint32_t id);`
  - `uint32_t AllocVertexId(const VectorNetwork&);` / `AllocEdgeId`
  - `VectorNetwork BezierPathToVectorNetwork(const BezierPath& path);` — Task 1 期间读 **旧** 单环字段（见下）
  - `BezierPath VectorNetworkToSingleRingBezierPath(const VectorNetwork&)` — 仅用于简单环测试；非单环可返回空

**Task 1 与 Task 2 的衔接：** Task 2 落地多轮廓前，`BezierPath` 仍是 `vertices`+`closed`，转换读这些字段。Task 2 之后改为读 `contours` / 使用 `MakeSingleContour` 辅助。

- [x] **Step 1: 编写会失败的测试**
- [x] **Step 2: 跑测试 — 预期编译/链接失败或 FAIL**
- [x] **Step 3: 实现 VectorNetwork + 转换**
- [x] **Step 4: 跑测试 — 预期 PASS**
- [x] **Step 5: Commit** — **跳过**，除非用户要求。

---

### Task 2: 多轮廓 BezierPath + tgfx builder

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/common/BezierPath.h`
- Modify: `src/common/BezierPath.cpp`
- Modify: 所有 `BezierPath` 上 `.vertices` / `.closed` 的调用点（grep 后改）
- Modify: `adapter/tgfx/src/TgfxPathBuilder.cpp`
- Modify: `tests/common/BezierPathTest.cpp`
- Update: `BezierPathToVectorNetwork` 改为基于单轮廓辅助

**Interfaces:**
- Produces:
```cpp
struct BezierPath {
    struct Vertex { Vec2 point; Vec2 inTangent; Vec2 outTangent; /* == */ };
    struct Contour {
        std::vector<Vertex> vertices;
        bool closed = false;
    };
    std::vector<Contour> contours;
    bool isZero() const;
    bool operator==(const BezierPath&) const;
};
BezierPath MakeSingleContour(std::vector<BezierPath::Vertex> vertices, bool closed);
bool IsSingleContour(const BezierPath&);
// PathResample / 仍假设单环的调用方迁移用：
const BezierPath::Contour* PrimaryContour(const BezierPath&); // 首轮廓或 nullptr
```

- [x] **Step 1: 编写/更新多轮廓 isZero 与相等性失败测试**

```cpp
TEST(BezierPathTest, MultiContourEquality) {
    BezierPath a = MakeSingleContour({{{0,0},{},{}}, {{1,0},{},{}}}, true);
    BezierPath b = a;
    EXPECT_EQ(a, b);
    b.contours.push_back(a.contours.front());
    EXPECT_NE(a, b);
}
```

- [x] **Step 2: 改结构体；更新调用点使工程可编译**

各处迁移模式：

```cpp
// 旧
path.vertices.push_back(...);
path.closed = true;
// 新
BezierPath::Contour contour;
contour.vertices.push_back(...);
contour.closed = true;
path.contours.push_back(std::move(contour));
```

`BezierToTgfxPath`：

```cpp
tgfx::Path BezierToTgfxPath(const BezierPath &path, FillRule fillRule) {
    tgfx::Path result;
    for (const BezierPath::Contour &contour : path.contours) {
        if (contour.vertices.empty()) {
            continue;
        }
        const auto &first = contour.vertices.front();
        result.moveTo(first.point.x, first.point.y);
        for (size_t i = 1; i < contour.vertices.size(); ++i) {
            AppendBezierSegment(result, contour.vertices[i - 1], contour.vertices[i]);
        }
        if (contour.closed && contour.vertices.size() > 1) {
            AppendBezierSegment(result, contour.vertices.back(), first);
            result.close();
        }
    }
    result.setFillType(ToTgfxFillType(fillRule));
    return result;
}
```

`PathResample` / `Interpolator<BezierPath>` / `PathGeometryEdit`：暂时只操作 `PrimaryContour`，**或**改为遍历轮廓——优先：PathGeometryEdit 在 Task 7 被 Network 编辑替换前保持单轮廓，并在 PathGeometryEdit 内 `assert` / `IsSingleContour`。

更新 `BezierPathToVectorNetwork`：

```cpp
if (path.contours.size() == 1) {
  // 将该 contour 按旧 vertices+closed 方式转换
}
```

- [x] **Step 3: 构建并跑 BezierPath / PathGeometryEdit / tgfx 相关测试**

```bash
cmake --build build
./build/tests/core_tests --gtest_filter='BezierPathTest.*:PathGeometryEdit*'
# 若有 tgfx 测试：
ctest --test-dir build -R 'tgfx' --output-on-failure
```

预期：PASS（或修完残留调用点）。

- [x] **Step 4: Commit** — **跳过**，除非用户要求。

---

### Task 3: CompileFillFaces + CompileStrokeEdges

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/common/VectorNetworkCompile.h`
- Create: `src/common/VectorNetworkCompile.cpp`
- Test: `tests/common/VectorNetworkCompileTest.cpp`

**Interfaces:**
- Produces:
```cpp
BezierPath CompileFillFaces(const VectorNetwork &network);
BezierPath CompileStrokeEdges(const VectorNetwork &network);
```

**Fill 算法：**

1. 建邻接：每个顶点 id 列出入射半边 `(edgeIndex, outgoing)`，出方向用极角排序（三次端方向 ≈ `±tangent`；切线近零时用弦 `end-start`）。
2. 按角度排序邻接。
3. 对每条尚未用于绕面的有向半边：在顶点处取一致的最左/最右转弯（全程同一 CW 或 CCW）走到回到起点，记录环。
4. 丢掉无界面（例如平面嵌入下 **丢弃 |面积| 最大的面**，保留有界面）。三角形外框 + 中心点连三顶点：预期 **3** 个三角形 fill contour。
5. 每个环映射为 `closed=true` 的 `BezierPath::Contour`，按边方向填 in/out 切线。

**Stroke：** 每条边一个开放 contour：两端点为 `start.point` / `end.point`，对应端 `outTangent=startTangent`、`inTangent=endTangent`（另一侧 handle 为零）。

- [x] **Step 1: 编写会失败的测试**

```cpp
TEST(VectorNetworkCompileTest, TriangleFanYieldsThreeFillFaces) {
    // P0 在内部，P1 P2 P3 为外三角；边 P0-P1,P0-P2,P0-P3,P1-P2,P2-P3,P3-P1
    VectorNetwork network = /* 构造 */;
    const BezierPath fill = CompileFillFaces(network);
    EXPECT_EQ(fill.contours.size(), 3u);
    for (const auto &c : fill.contours) {
        EXPECT_TRUE(c.closed);
        EXPECT_EQ(c.vertices.size(), 3u);
    }
    const BezierPath stroke = CompileStrokeEdges(network);
    EXPECT_EQ(stroke.contours.size(), 6u);
}

TEST(VectorNetworkCompileTest, OpenChainHasNoFill) {
    VectorNetwork network = BezierPathToVectorNetwork(MakeOpenSegment());
    EXPECT_TRUE(CompileFillFaces(network).contours.empty());
    EXPECT_EQ(CompileStrokeEdges(network).contours.size(), 1u);
}
```

- [x] **Step 2: 实现编译；跑测试至 PASS**

- [x] **Step 3: Commit** — **跳过**，除非用户要求。

---

### Task 4: 模型 + PropertyValue + Animatable + Interpolator\<VectorNetwork\>

**Status:** ✅ Done

**Files:**
- Modify: `ShapePath.h`、`Layer.h`（Mask.path）
- Modify: `PropertyValue.h`、`KeyframeData.h`
- Modify: `AnimatableType.h`、`Animatable.cpp`
- Modify: `Interpolator.h` / `Interpolator.cpp`
- Modify: `PropertyPath.cpp`（仅当类型假设被破坏时）
- Test: `tests/animation/VectorNetworkMorphTest.cpp`（新建）
- 将既有 BezierPath morph 测试改为 Network（或夹具转换）

**Interfaces:**
- `AnimatableType::VectorNetwork` 替换 `AnimatableType::BezierPath`
- `PropertyValue = variant<float, Vec2, Color, VectorNetwork, string>`
- `Interpolator<VectorNetwork>::Lerp`：
  - 拓扑不同（顶点 id、边 id、或任一边 `start`/`end` 不一致）→ 返回 `from`（hold）
  - 否则：按 id 插值各 `vertex.point`；按 id 插值各边两端切线

```cpp
static VectorNetwork Lerp(const VectorNetwork &from, const VectorNetwork &to, float t) {
    if (!SameTopology(from, to)) {
        return from;
    }
    VectorNetwork result = from;
    // 按 id 映射到 to，插值 point 与 tangents
    return result;
}
```

**不要**对 Network 做旧 BezierPath 那种按顶点数 resample。

- [x] **Step 1: 编写会失败的 morph 测试（同拓扑 / 异拓扑）**

- [x] **Step 2: 切换模型类型 + Animatable 显式实例化 + Interpolator**

- [x] **Step 3: 修复 undo/commands/测试里构造 BezierPath 属性值导致的编译错误 — 改用 Network + 转换辅助**

- [x] **Step 4: 运行**

```bash
./build/tests/core_tests --gtest_filter='VectorNetworkMorphTest.*:Animatable*:Interpolator*:Commands*'
```

- [x] **Step 5: Commit** — **跳过**，除非用户要求。

---

### Task 5: 序列化双读（不升 schema）

**Status:** ✅ Done

**Files:**
- Modify: `src/serialization/Serializer.cpp`（BezierPathToJson / FromJson 路径改为 Network）
- Test: `tests/serialization/SerializerTest.cpp`

**Interfaces:**
- 写出：`{"vertices":[{"id":...,"point":...}], "edges":[{"id":...,"start":...,"end":...,"startTangent":...,"endTangent":...}]}`
- 读入：
  - 含 `edges` 数组 → 解析为 Network
  - 否则含 `vertices` + `closed` → 旧轮廓 BezierPathFromJson → BezierPathToVectorNetwork
- `SchemaMigrator::currentVersion()` 保持 **1**

- [x] **Step 1: 测试**

- [x] **Step 2: 实现双读 / Network 写出**

- [x] **Step 3: Serializer 测试 PASS；确认 schemaVersion 字段仍为 1**

- [x] **Step 4: Commit** — **跳过**，除非用户要求。

---

### Task 6: SceneEvaluator 的 fill / stroke 几何分离

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/render/ShapeGeometry.h`（必要时改 cpp）
- Modify: `src/render/SceneEvaluator.cpp`
- Modify: CommandBuilder / hit-test（若 geometry 增加 strokePath）
- Modify: mask 求值：覆盖路径用 CompileFillFaces（mask 按填充语义）
- Test: `tests/render/SceneEvaluatorTest.cpp`

**Interfaces:**
- 扩展 path 类几何：

```cpp
struct ShapeGeometry {
    // ...
    BezierPath path;        // fill 面（CompileFillFaces）
    BezierPath strokePath;  // CompileStrokeEdges；为空时 stroke 回退用 path
};
```

- ShapePath 的 CollectGeometry：

```cpp
const VectorNetwork network = shape.path.evaluatePreview(time);
ShapeGeometry geometry = MakePathGeometry(CompileFillFaces(network));
geometry.strokePath = CompileStrokeEdges(network);
geometries.push_back(std::move(geometry));
```

- ApplyLayerStyles：Fill 用 `geometry.path`；Stroke 用 `geometry.strokePath.contours.empty() ? geometry.path : geometry.strokePath`

- FollowPath / EvaluateLayerPath：无 fill 面时用 CompileStrokeEdges；否则取最长 closed fill contour（代码注释写明规则）。

- [x] **Step 1: 测试三角扇夹具 — fill item 3 个 contour，stroke item 6 个**

- [x] **Step 2: 实现；跑 SceneEvaluator + HitTest 回归**

- [x] **Step 3: Commit** — **跳过**，除非用户要求。

---

### Task 7: VectorNetworkEdit（替换创作侧 PathGeometryEdit）

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/common/VectorNetworkEdit.h`
- Create: `src/common/VectorNetworkEdit.cpp`
- Test: `tests/common/VectorNetworkEditTest.cpp`
- 保留 PathGeometryEdit 供残留单轮廓工具使用，无引用后可删

**Interfaces:**

```cpp
VectorNetwork AddVertex(VectorNetwork n, Vec2 point, uint32_t *outId);
VectorNetwork AddEdge(VectorNetwork n, uint32_t start, uint32_t end, uint32_t *outId);
// 已存在同向边则 no-op（有向 start→end）
VectorNetwork MoveVertex(VectorNetwork n, uint32_t id, Vec2 point);
VectorNetwork MoveEdgeTangent(VectorNetwork n, uint32_t edgeId, bool atStart, Vec2 tangent, bool mirror);
// 仅当该端顶点度数==2 时 mirror；否则忽略 mirror
VectorNetwork InsertVertexOnEdge(VectorNetwork n, uint32_t edgeId, float t, uint32_t *outId);
VectorNetwork RemoveVertex(VectorNetwork n, uint32_t id); // 同时删入射边
VectorNetwork RemoveEdge(VectorNetwork n, uint32_t id);
VectorNetwork RecenterNetwork(VectorNetwork n, Vec2 &localCenterOut);
int VertexDegree(const VectorNetwork&, uint32_t id); // Task 1 已有
```

- [x] **Step 1: 测试闭合三角后续边、共享点移动、mirror 度数规则**

- [x] **Step 2: 实现；测试 PASS**

- [x] **Step 3: Commit** — **跳过**，除非用户要求。

---

### Task 8: PathEditHandles 改基于 Network

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/render/PathEditHandles.h`
- Modify: `src/render/PathEditHandles.cpp`
- Modify: `tests/render/PathEditHandlesTest.cpp`

**Interfaces:**
- PathEditHandles 存 `VectorNetwork localNetwork`（替代或并存于原 BezierPath localPath）
- Hit 种类：Vertex、EdgeTangent（edge id + 哪一端）、Edge（插点）、None
- Chrome：用 CompileStrokeEdges 画黄线；顶点方块；切线手柄画在选中顶点相关边上；度数≠2 仍可显示每边手柄，但拖动不 mirror
- BuildPathEditHandles 从求值层（shape/mask）取出 Network

- [x] **Step 1: 更新共享顶点与边插点的 hit 测试**

- [x] **Step 2: 实现；PathEditHandles 测试 PASS**

- [x] **Step 3: Commit** — **跳过**，除非用户要求。

---

### Task 9: Bridge ABI

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_path_edit.cpp`（+ 属性读写）
- Modify: `bridge/tests/BridgeTest.cpp`
- Swift: MotionDocumentCore 路径相关 API

**Interfaces（C）：**

```c
typedef struct MSVectorNetwork { /* lengths + pointers to vertex/edge POD arrays */ } MSVectorNetwork;
void ms_vector_network_free(MSVectorNetwork *);

MSVectorNetwork *ms_property_evaluate_vector_network(...);
void ms_command_set_static_vector_network(...);
void ms_command_add_keyframe_vector_network(...);

void ms_command_network_edit_add_vertex(...);
void ms_command_network_edit_add_edge(...);
void ms_command_network_edit_move_vertex(...);
void ms_command_network_edit_move_edge_tangent(..., bool at_start, bool mirror);
void ms_command_network_edit_insert_on_edge(...);
void ms_command_network_edit_remove_vertex(...);
void ms_command_network_edit_recenter_shape(...);

// 优先替换旧 ms_command_path_edit_*；迁移期可留薄包装，随后删除。
```

Canvas hit：尽量返回 vertex id / edge id，而非仅顶点下标。

- [x] **Step 1: Bridge 测试 — Network round-trip + 闭合后 add_edge**

- [x] **Step 2: 实现 ABI；更新 Swift 包装**

- [x] **Step 3: Commit** — **跳过**，除非用户要求。

---

### Task 10: App 钢笔会话（activeVertexId + drawing）

**Status:** 🔄 进行中（待人机验证）

**Files:**
- Modify: PathEditTarget.swift / EditorState
- Modify: EditorViewController 等钢笔手势相关代码
- **即使之后允许 Core commit，App 也须人机验证后再提交**

**行为（来自 spec）：**

| 条件 | 行为 |
|---|---|
| 空网络 + 空白 | 新建 path 层 + 首顶点；active=id；drawing=true |
| drawing + 空白 | AddVertex + AddEdge(active→新点)；active=新点 |
| drawing + 已有 V ≠ active | AddEdge(active→V) 或已存在则 no-op；active=V |
| drawing + active 自身 | drawing=false；保持高亮 |
| 非 drawing + 顶点 V | active=V；drawing=true；不建边 |
| 非 drawing + 空白 | 新组件起点顶点；active=新点；drawing=true |
| 命中边 | InsertVertexOnEdge；选中新点 |

去掉对 closed / 仅首点 CloseRing 的依赖。

- [x] **Step 1: 实现状态机并接通 bridge 命令**

- [ ] **Step 2: 人机验证清单**
  - 闭合三角后点 P1 再点空白 → 新边
  - 点 P1 再点 P2 → 仅切换高亮，无重复边
  - 拖共享顶点 → 所有关联边跟随
  - 度数>2 → mirroring 无效

- [ ] **Step 3: Commit App** — 仅在用户确认验证通过并要求提交后。

---

### Task 11: 导出拍平

**Status:** ✅ Done

**Files:**
- Modify: 读取 ShapePath::path / mask path 的 PAG 导出路径
- Test: 更新既有导出测试夹具为 Network

**行为：** 输出格式中不保留 Network。静态：`CompileFillFaces`（空则 StrokeEdges）。多关键帧：优先 `CompileStrokeEdges` 以保持 PAG path morph 顶点对应（禁止对 FillFaces 采样折线 morph）。

- [x] **Step 1: 修编译；增/改一条含共享顶点 Network 的导出测试**

- [x] **Step 1b: 动画 path 导出改走 StrokeEdges + 回归测试**（`AnimatedPathExportsCubicMorphStableNotSampledFill`）

- [x] **Step 2: Commit** — **跳过**，除非用户要求。

---

### Task 12: Spec 状态 + 残留消费方清理

**Status:** 🔄 进行中（ASan 全测已绿；App 人机验证未完成）

**Files:**
- Grep 残留 Animatable\<BezierPath\> / AnimatableType::BezierPath / 旧路径 JSON 写出
- Update: docs/superpowers/specs/2026-08-05-vector-network-pen-design.md 状态 → 实现中 / 验证后 done
- 修 FollowPath / MotionPath / TextPath / MaskPathBake / PathOverlay 等调用方

- [x] **Step 1: Grep 干净；ASan 下 core_tests + bridge_test 全绿**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure -LE benchmark
```

（`Animatable<BezierPath>` 仍保留于导出临时路径 / ConvertBezierPath，属预期。）

- [ ] **Step 2: Commit** — **跳过**，除非用户要求。

---

## Spec 覆盖对照

| Spec 要求 | Task |
|---|---|
| VectorNetwork 替换权威 BezierPath | 1, 4 |
| 切线在边上；仅度数==2 mirroring | 1, 7 |
| Fill 全部最小面 | 3, 6 |
| Stroke 全部边 | 3, 6 |
| Morph 在 Network；不对编译 Path morph | 4 |
| 异拓扑 hold | 4 |
| Shape + Mask | 4, 6 |
| 双读；不升 schema | 5 |
| 闭合后续画 / 高亮规则 | 10 |
| 共享点拖动 | 7, 10 |
| Network recenter | 7, 9 |
| 导出拍平 | 11 |
| 多轮廓 BezierPath + tgfx | 2 |
| 禁止自动 commit | 全局约束 |

## 非范围（不要做）

- 区域独立填色 / 布尔运算
- 导出保留 Network
- 拓扑改动自动传播到全部关键帧
- 提升 schemaVersion
