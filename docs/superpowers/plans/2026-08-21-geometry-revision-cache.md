# Geometry Revision 缓存实现计划

> **给执行代理：** 必须使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 给 `VectorNetwork` / `BezierPath` 打上进程内私有 revision，静态路径跳过 `CompileFillFaces`，Path 类型的 `HashGeometry` 不再逐点比较。

**架构：** 两种值类型各有私有 `revision_`，只通过 `GeometryRevisionAccess` 辅助类访问。`CompileFillFaces` / `CompileStrokeEdges` 共用按 network revision 索引的 thread-local LRU。adapter 的 `HashGeometry` 在 revision 非 0 时用它当 `contentHash`。ShapePath 每帧只 `evaluate` 一次 network。

**技术栈：** C++17 Core、GoogleTest、tgfx adapter（`HashGeometry` / `TgfxPathCache`）。

**Spec：** [docs/superpowers/specs/2026-08-21-geometry-revision-cache-design.md](../specs/2026-08-21-geometry-revision-cache-design.md)

## 全局约束

- 对话用中文；代码和注释用英语。
- 禁止 `dynamic_cast`、C++ 异常、lambda（改用显式函数）。
- `if` / `while` / `switch` 的 `case` 分支体必须用 `{}`。
- 成员用 `= {}` 风格初始化。
- `operator==` 只比几何，不比 `revision_`。
- JSON 不读写 `revision`；不升 `schemaVersion`。
- `GenerateGeometryRevision` 只在 `src/common/GeometryRevision.cpp` 的匿名命名空间。
- 不为 VectorNetwork / BezierPath 引入 `shared_ptr` 或 COW。
- 本计划不做 StaticTimeRanges。
- 本计划全部是非 UI 的 Core / adapter Task：**每个 Task 做完后自动 commit**（仅 commit，不 push）。`git commit --only` 显式列文件；英文句号结尾；不要 `git add -A`。
- spec / plan 已确认：随代码 Task 一并 commit（显式列文件）。
- 每完成一个 Step 或整个 Task，立刻把本文件对应 `- [ ]` 改为 `- [x]`（plan 勾选随该 Task 的代码 commit 一并提交）。

---

### Task 1: 私有 revision + GeometryRevisionAccess

**文件：**
- 修改：`include/MotionStudio/common/VectorNetwork.h`
- 修改：`include/MotionStudio/common/BezierPath.h`
- 新建：`include/MotionStudio/common/GeometryRevision.h`
- 新建：`src/common/GeometryRevision.cpp`
- 修改：`src/common/BezierPath.cpp`
- 测试：`tests/common/GeometryRevisionTest.cpp`

**接口：**
- 依赖：现有 `VectorNetwork` / `BezierPath` 值类型
- 产出：
  - `class GeometryRevisionAccess { static uint64_t Get(...); static void Stamp(...); }`（`include/MotionStudio/common/GeometryRevision.h`）
  - `GenerateGeometryRevision()` 只在 `src/common/GeometryRevision.cpp` 匿名命名空间

- [x] **Step 1: 写失败测试**

新建 `tests/common/GeometryRevisionTest.cpp`（`tests/CMakeLists.txt` 会对 `tests/common` 做 `add_files_by_extension`，自动编进 `core_tests`）：

```cpp
#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"

using motion::BezierPath;
using motion::VectorNetwork;

TEST(GeometryRevisionTest, DefaultIsZeroAndStampIsNonZeroAndIncreases) {
    VectorNetwork network;
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(network), 0u);
    motion::GeometryRevisionAccess::Stamp(network);
    const uint64_t first = motion::GeometryRevisionAccess::Get(network);
    EXPECT_NE(first, 0u);
    motion::GeometryRevisionAccess::Stamp(network);
    EXPECT_GT(motion::GeometryRevisionAccess::Get(network), first);

    BezierPath path;
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path), 0u);
    motion::GeometryRevisionAccess::Stamp(path);
    const uint64_t pathFirst = motion::GeometryRevisionAccess::Get(path);
    EXPECT_NE(pathFirst, 0u);
    motion::GeometryRevisionAccess::Stamp(path);
    EXPECT_GT(motion::GeometryRevisionAccess::Get(path), pathFirst);
}

TEST(GeometryRevisionTest, CopyKeepsRevisionUntilRestamped) {
    VectorNetwork network;
    network.vertices.push_back({1, {0, 0}});
    motion::GeometryRevisionAccess::Stamp(network);
    const uint64_t stamped = motion::GeometryRevisionAccess::Get(network);
    const VectorNetwork copied = network;
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(copied), stamped);
    network.vertices[0].point = {1, 0};
    motion::GeometryRevisionAccess::Stamp(network);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(network), stamped);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(copied), stamped);
}

TEST(GeometryRevisionTest, EqualityIgnoresRevision) {
    VectorNetwork left;
    left.vertices.push_back({1, {0, 0}});
    VectorNetwork right = left;
    motion::GeometryRevisionAccess::Stamp(left);
    motion::GeometryRevisionAccess::Stamp(right);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(left), motion::GeometryRevisionAccess::Get(right));
    EXPECT_EQ(left, right);

    BezierPath pathLeft;
    pathLeft.contours.push_back({{{0, 0}, {}, {}}, false});
    BezierPath pathRight = pathLeft;
    motion::GeometryRevisionAccess::Stamp(pathLeft);
    motion::GeometryRevisionAccess::Stamp(pathRight);
    EXPECT_EQ(pathLeft, pathRight);
}
```

- [x] **Step 2: 跑测试，确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*'
```

预期：编译失败，`motion::GeometryRevisionAccess::Get` 未声明。

- [x] **Step 3: 加私有 revision 和访问器**

`include/MotionStudio/common/VectorNetwork.h`：前置声明 `class GeometryRevisionAccess;`，加私有字段和 `friend class`。公开的 `vertices` / `edges` 不动：

```cpp
class GeometryRevisionAccess;

struct VectorNetwork {
    // existing Vertex / Edge / vertices / edges / operator== unchanged
  private:
    uint64_t revision_ = 0;
    friend class GeometryRevisionAccess;
};
```

`include/MotionStudio/common/BezierPath.h`：同样处理 `BezierPath`（`contours` 仍公开）。

`include/MotionStudio/common/GeometryRevision.h`：

```cpp
class GeometryRevisionAccess {
  public:
    static uint64_t Get(const VectorNetwork &network);
    static void Stamp(VectorNetwork &network);
    static uint64_t Get(const BezierPath &path);
    static void Stamp(BezierPath &path);
};
```

`src/common/GeometryRevision.cpp`：匿名命名空间里的 `GenerateGeometryRevision()`（atomic 从 1，永不返回 0）加上 `Get` / `Stamp` 实现。

现有 `operator==` 保持只比几何（不要比 `revision_`）。

- [x] **Step 4: 跑测试，确认通过**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*'
```

预期：PASS。

- [x] **Step 5: 勾选本 Task 并 commit**

把本 Task 的 **Status:** 改为 ✅ Done，勾上上面步骤。然后：

```bash
git commit --only \
  include/MotionStudio/common/VectorNetwork.h \
  include/MotionStudio/common/BezierPath.h \
  src/common/GeometryRevision.h \
  src/common/VectorNetwork.cpp \
  src/common/BezierPath.cpp \
  tests/common/GeometryRevisionTest.cpp \
  docs/superpowers/plans/2026-08-21-geometry-revision-cache.md \
  -m "Add private geometry revision stamps for VectorNetwork and BezierPath."
```

若 `src/common/GeometryRevision.h` 尚未被 git 跟踪，先 `git add` 该文件再 `--only` commit。不要加入 spec 或 `docs/README.md`。

**Status:** ✅ Done

---

### Task 2: 工厂 / 编辑 / 动画 / 序列化 stamp

**文件：**
- 修改：`src/common/BezierPath.cpp`（`MakeSingleContour`）
- 修改：`src/common/VectorNetworkConvert.cpp`
- 修改：`src/common/VectorNetworkEdit.cpp`
- 修改：`src/common/PathGeometryEdit.cpp`
- 修改：`src/common/BezierPathTransform.cpp`
- 修改：`src/import/svg/SvgPathConvert.cpp`
- 修改：`src/import/svg/SvgTransform.cpp`
- 修改：`src/animation/Interpolator.cpp`
- 修改：`src/animation/Animatable.cpp`
- 修改：`src/serialization/Serializer.cpp`
- 修改：`tests/common/GeometryRevisionTest.cpp`

**接口：**
- 依赖：Task 1 的 `GeometryRevisionAccess::Stamp` / `Get`
- 产出：所有工厂 / 编辑 / Animatable 写入 / FromJson 都会 stamp；evaluate / Hold 拷贝保留原 stamp；no-op 编辑不换号

编辑函数按值拷入。推荐写法：真正改了几何再 stamp；提前 `return network;` 的 no-op **不要** stamp。

```cpp
VectorNetwork MoveVertex(VectorNetwork network, uint32_t id, Vec2 point) {
    VectorNetwork::Vertex *vertex = FindVertex(network, id);
    if (vertex == nullptr) {
        return network;
    }
    vertex->point = point;
    GeometryRevisionAccess::Stamp(network);
    return network;
}
```

不要用对象指针判断是否变化（编辑按值拷贝）。也不要在每个 `.cpp` 搞一套 `StampIfChanged` 去比 buffer 指针——走「改了就 stamp、no-op 原样返回」即可。

- [x] **Step 1: 给 GeometryRevisionTest 补 stamp 边界用例**

追加到 `tests/common/GeometryRevisionTest.cpp`：

```cpp
#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/common/VectorNetworkEdit.h"
#include "MotionStudio/serialization/Serializer.h"

TEST(GeometryRevisionTest, MakeSingleContourStamps) {
    const BezierPath path = motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(path), 0u);
}

TEST(GeometryRevisionTest, BezierPathToVectorNetworkStamps) {
    const BezierPath path = motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true);
    const VectorNetwork network = motion::BezierPathToVectorNetwork(path);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(network), 0u);
}

TEST(GeometryRevisionTest, MoveVertexStampsAndMissingVertexDoesNot) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const uint64_t before = motion::GeometryRevisionAccess::Get(network);
    ASSERT_NE(before, 0u);
    const VectorNetwork missing = motion::MoveVertex(network, 999, {1, 1});
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(missing), before);
    const VectorNetwork moved = motion::MoveVertex(network, network.vertices[0].id, {1, 1});
    EXPECT_NE(motion::GeometryRevisionAccess::Get(moved), before);
}

TEST(GeometryRevisionTest, EvaluatePreviewKeepsStaticRevision) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    motion::Animatable<VectorNetwork> path;
    path.setStaticValue(network);
    const uint64_t stored = motion::GeometryRevisionAccess::Get(path.staticValue());
    EXPECT_NE(stored, 0u);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluatePreview(0)), stored);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluatePreview(10)), stored);
}

TEST(GeometryRevisionTest, HoldKeepsKeyframeRevision) {
    VectorNetwork a = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    VectorNetwork b = a;
    b.vertices[1].point = {20, 0};
    motion::GeometryRevisionAccess::Stamp(b);
    motion::Animatable<VectorNetwork> path;
    motion::Keyframe<VectorNetwork> kf0;
    kf0.time = 0;
    kf0.value = a;
    kf0.easing = motion::Easing::Hold();
    motion::Keyframe<VectorNetwork> kf1;
    kf1.time = 10;
    kf1.value = b;
    path.addKeyframe(kf0);
    path.addKeyframe(kf1);
    const uint64_t fromRevision = motion::GeometryRevisionAccess::Get(path.keyframes()[0].value);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluate(0)), fromRevision);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluate(5)), fromRevision);
}

TEST(GeometryRevisionTest, LerpSameTopologyGetsNewRevision) {
    VectorNetwork from = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    VectorNetwork to = from;
    to.vertices[1].point = {20, 0};
    motion::GeometryRevisionAccess::Stamp(to);
    const VectorNetwork lerped = motion::Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(lerped), motion::GeometryRevisionAccess::Get(from));
    EXPECT_NE(motion::GeometryRevisionAccess::Get(lerped), motion::GeometryRevisionAccess::Get(to));
}

TEST(GeometryRevisionTest, LerpDifferentTopologyKeepsFromRevision) {
    VectorNetwork from = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    VectorNetwork to = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const VectorNetwork held = motion::Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(held), motion::GeometryRevisionAccess::Get(from));
}

TEST(GeometryRevisionTest, JsonRoundTripOmitsRevisionAndRestamps) {
    motion::Document document;
    motion::Composition *composition = document.addComposition(std::make_unique<motion::Composition>());
    motion::Layer *layer = document.addLayer(composition->id, std::make_unique<motion::Layer>(motion::LayerType::Shape));
    auto *content = static_cast<motion::ShapeContent *>(layer->content.get());
    auto shape = std::make_unique<motion::ShapePath>();
    shape->path.setStaticValue(motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true)));
    content->geometry = std::move(shape);
    const std::string json = motion::Serializer::serialize(document);
    EXPECT_EQ(json.find("\"revision\""), std::string::npos);
    auto first = motion::Serializer::deserialize(json);
    auto second = motion::Serializer::deserialize(json);
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    const auto *a = static_cast<const motion::ShapePath *>(
        static_cast<const motion::ShapeContent *>((*first)->compositions[0]->layers[0]->content.get())->geometry.get());
    const auto *b = static_cast<const motion::ShapePath *>(
        static_cast<const motion::ShapeContent *>((*second)->compositions[0]->layers[0]->content.get())->geometry.get());
    const uint64_t revA = motion::GeometryRevisionAccess::Get(a->path.staticValue());
    const uint64_t revB = motion::GeometryRevisionAccess::Get(b->path.staticValue());
    EXPECT_NE(revA, 0u);
    EXPECT_NE(revB, 0u);
    EXPECT_NE(revA, revB);
    EXPECT_EQ(a->path.staticValue(), b->path.staticValue());
}
```

Hold 用例依赖现有 Hold 缓动：`evaluate` 返回前一关键帧的值。按需 include `MotionStudio/model/Document.h`、`Composition.h`、`Layer.h`、`ShapeContent.h`、`ShapePath.h`。

- [x] **Step 2: 跑测试，确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*'
```

预期：FAIL — 工厂 / MoveVertex / setStaticValue / Lerp / deserialize 还没 stamp。

- [x] **Step 3: 在所有写入边界 stamp**

`MakeSingleContour`（`src/common/BezierPath.cpp`）：拼好 `path` 后、return 前调用 `GeometryRevisionAccess::Stamp(path)`。空输入也 stamp（已知空路径有非 0 身份即可）。

`ContourToVectorNetwork` / `BezierPathToVectorNetwork`（`src/common/VectorNetworkConvert.cpp`）：返回的 network 有顶点就 stamp；空 `{}` 保持 0。`VectorNetworkToSingleRingBezierPath` 已经走 `MakeSingleContour`，会带上 stamp；空 `{}` 保持 0。

`src/import/svg/SvgPathConvert.cpp`：`PathToVectorNetwork` 返回前 `GeometryRevisionAccess::Stamp(network)`。

`src/import/svg/SvgTransform.cpp` 的 `TransformNetwork`：改完顶点/切线后 stamp。`ApplyResidualBake` 接着 `setStaticValue` 会再 stamp 一次，可以接受。

`src/common/BezierPathTransform.cpp`：拼好 `result` 后 stamp。`path.contours.empty()` 时保持 0。

`src/common/VectorNetworkEdit.cpp` — 成功路径 stamp：`AddVertex`、`AddEdge`（真正追加了边）、`MoveVertex`、`MoveEdgeTangent`（边存在）、`InsertVertexOnEdge`、`RemoveVertex`（顶点存在）、`RemoveEdge`（删掉了边）、`SetVertexMirrorMode`（顶点存在；degree≠2 只改 mode 也 stamp）、`RecenterNetwork`（中心不是近零）。`ToggleVertexSmooth` 走 `SetVertexMirrorMode`：后者已 stamp 就不要再 stamp；Toggle 在调用 Set 之前 no-op 则不 stamp。

`src/common/PathGeometryEdit.cpp` — 同样规则：`MoveVertex`、`MoveInTangent`、`MoveOutTangent`、`InsertVertexOnSegment`、`RemoveVertex`、`ClosePath`、`AppendVertex`、`ToggleVertexSmooth`、`RecenterPath`。no-op 返回保留传入的 revision。

`src/animation/Interpolator.cpp`：
- `Interpolator<VectorNetwork>::Lerp`：`!SameNetworkTopology` 时 `return from`（revision 不变）。写出插值点/切线后 `GeometryRevisionAccess::Stamp(result)`。
- `Interpolator<BezierPath>::Lerp`：不匹配返回 `from`；成功 lerp 后 stamp `result`。

`src/animation/Animatable.cpp` — 在 `setStaticValue`、`addKeyframe`、`updateKeyframe` 里，存进去之前：

```cpp
if constexpr (std::is_same_v<T, VectorNetwork> || std::is_same_v<T, BezierPath>) {
    GeometryRevisionAccess::Stamp(value);           // setStaticValue
    GeometryRevisionAccess::Stamp(keyframe.value);  // addKeyframe / updateKeyframe
}
```

`takeKeyframe` / `evaluatePreview` 不 stamp。

`src/serialization/Serializer.cpp`：
- `BezierPathFromJson`：每次成功 `return path` 前 stamp。
- `VectorNetworkFromJson`：成功 `return network` 前 stamp。
- `VectorNetworkToJson` / BezierPath 的 ToJson：**不要**写 `revision`。

`RectToBezierPath` / `EllipseToBezierPath` 走 `MakeSingleContour`，不必再 stamp。

- [x] **Step 4: 跑测试，确认通过**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*:VectorNetworkTest.*:VectorNetworkEditTest.*:VectorNetworkCompileTest.*'
```

预期：PASS。原有几何测试仍只比 `operator==`。

- [x] **Step 5: 勾选本 Task 并 commit**

把本 Task 的 **Status:** 改为 ✅ Done，勾上上面步骤。然后 commit 本 Task 改过的源码、测试、spec 和本 plan 文件（显式列路径）。示例信息：`Stamp geometry revisions at factories edits animation and JSON load.`

**Status:** ✅ Done

---

### Task 3: CompileFillFaces / CompileStrokeEdges LRU

**文件：**
- 修改：`include/MotionStudio/common/VectorNetworkCompile.h`
- 修改：`src/common/VectorNetworkCompile.cpp`
- 修改：`tests/common/GeometryRevisionTest.cpp`

**接口：**
- 依赖：`GeometryRevisionAccess::Get` / `Stamp`
- 产出：
  - `struct CompiledVectorNetwork { BezierPath fill; BezierPath stroke; };`
  - `const CompiledVectorNetwork &CompileVectorNetwork(const VectorNetwork &network);` — 引用只在同线程下一次 `CompileVectorNetwork` 之前有效
  - 现有 `CompileFillFaces` / `CompileStrokeEdges` 变成从 `CompileVectorNetwork` 拷出结果的薄封装

- [x] **Step 1: 写失败的编译缓存测试**

追加到 `tests/common/GeometryRevisionTest.cpp`：

```cpp
#include "MotionStudio/common/VectorNetworkCompile.h"

TEST(GeometryRevisionTest, StampedCompileHitsKeepFillRevision) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const BezierPath first = motion::CompileFillFaces(network);
    const BezierPath second = motion::CompileFillFaces(network);
    EXPECT_EQ(first, second);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(first), 0u);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(first), motion::GeometryRevisionAccess::Get(second));
}

TEST(GeometryRevisionTest, UnstampedCompileDoesNotShareFillRevision) {
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}, {3, {0, 10}}};
    network.edges = {{1, 1, 2, {}, {}}, {2, 2, 3, {}, {}}, {3, 3, 1, {}, {}}};
    ASSERT_EQ(motion::GeometryRevisionAccess::Get(network), 0u);
    const BezierPath first = motion::CompileFillFaces(network);
    const BezierPath second = motion::CompileFillFaces(network);
    EXPECT_EQ(first, second);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(first), motion::GeometryRevisionAccess::Get(second));
}

TEST(GeometryRevisionTest, CompileVectorNetworkSharesLookup) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const motion::CompiledVectorNetwork compiled = motion::CompileVectorNetwork(network);
    EXPECT_EQ(compiled.fill, motion::CompileFillFaces(network));
    EXPECT_EQ(compiled.stroke, motion::CompileStrokeEdges(network));
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(compiled.fill),
              motion::GeometryRevisionAccess::Get(motion::CompileFillFaces(network)));
}
```

立刻把 `CompiledVectorNetwork` 拷出来，不要把返回的引用跨另一次 compile 使用。

- [x] **Step 2: 跑测试，确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.StampedCompile*:GeometryRevisionTest.UnstampedCompile*:GeometryRevisionTest.CompileVectorNetwork*'
```

预期：编译失败（没有 `CompileVectorNetwork`），或 FAIL（已 stamp 的 compile 两次 fill revision 不同）。

- [x] **Step 3: 实现 LRU**

`include/MotionStudio/common/VectorNetworkCompile.h`：

```cpp
struct CompiledVectorNetwork {
    BezierPath fill;
    BezierPath stroke;
};

// fill + stroke from one lookup. The reference is valid only until the next
// CompileVectorNetwork call on this thread; copy fill/stroke out immediately.
const CompiledVectorNetwork &CompileVectorNetwork(const VectorNetwork &network);
```

`src/common/VectorNetworkCompile.cpp`：保留 `BuildCurvePlanarNetwork` / `BoundedFillFacesFromNetwork` / 现有 stroke walk 作为无缓存 helper。在 `motion` 命名空间末尾加：

```cpp
struct CompileCache {
    static constexpr size_t Capacity = 64;
    std::list<std::pair<uint64_t, CompiledVectorNetwork>> order;
    std::unordered_map<uint64_t, std::list<std::pair<uint64_t, CompiledVectorNetwork>>::iterator> index;
    CompiledVectorNetwork uncached;
};

CompileCache &ThreadCompileCache() {
    static thread_local CompileCache cache;
    return cache;
}

CompiledVectorNetwork CompileUncached(const VectorNetwork &network) {
    CompiledVectorNetwork compiled;
    compiled.fill = BoundedFillFacesFromNetwork(BuildCurvePlanarNetwork(network));
    compiled.stroke = /* existing CompileStrokeEdges body extracted as CompileStrokeEdgesUncached(network) */;
    GeometryRevisionAccess::Stamp(compiled.fill);
    GeometryRevisionAccess::Stamp(compiled.stroke);
    return compiled;
}

const CompiledVectorNetwork &CompileVectorNetwork(const VectorNetwork &network) {
    CompileCache &cache = ThreadCompileCache();
    const uint64_t revision = GeometryRevisionAccess::Get(network);
    if (revision == 0) {
        cache.uncached = CompileUncached(network);
        return cache.uncached;
    }
    const auto found = cache.index.find(revision);
    if (found != cache.index.end()) {
        cache.order.splice(cache.order.begin(), cache.order, found->second);
        return found->second->second;
    }
    if (cache.order.size() >= CompileCache::Capacity) {
        cache.index.erase(cache.order.back().first);
        cache.order.pop_back();
    }
    cache.order.push_front({revision, CompileUncached(network)});
    cache.index.emplace(revision, cache.order.begin());
    return cache.order.front().second;
}

BezierPath CompileFillFaces(const VectorNetwork &network) {
    return CompileVectorNetwork(network).fill;
}

BezierPath CompileStrokeEdges(const VectorNetwork &network) {
    return CompileVectorNetwork(network).stroke;
}
```

把当前 `CompileStrokeEdges` 函数体抽成同匿名命名空间里的 `CompileStrokeEdgesUncached`，供 `CompileUncached` 调用。planar 中间 network **不** stamp。

LRU 不要用 lambda，写显式函数。

- [x] **Step 4: 跑测试**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*:VectorNetworkCompileTest.*'
```

预期：PASS。原有 compile 测试仍检查 contour 数量。

- [x] **Step 5: 勾选本 Task 并 commit**

把本 Task 的 **Status:** 改为 ✅ Done，勾上上面步骤。然后 commit。示例信息：`Cache compiled fill and stroke paths by VectorNetwork revision.`

**Status:** ✅ Done

---

### Task 4: SceneEvaluator 一次 evaluate + 一次 compile

**文件：**
- 修改：`src/render/SceneEvaluator.cpp`
- 修改：`tests/common/GeometryRevisionTest.cpp`（revision 测试集中放这里；不必复用 PathScene fixture）

**接口：**
- 依赖：Task 3 的 `CompileVectorNetwork`
- 产出：ShapePath 只 evaluate 一次 network；fill+stroke 来自一次 cache 查找；`shapeNetwork` 就是这份 network

- [x] **Step 1: 写跨帧 identity 测试**

加到 `tests/common/GeometryRevisionTest.cpp`，include SceneEvaluator 相关头：

```cpp
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/FillStyle.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/render/SceneEvaluator.h"

TEST(GeometryRevisionTest, StaticShapePathKeepsCompiledRevisionAcrossFrames) {
    motion::Document document;
    motion::Composition *composition = document.addComposition(std::make_unique<motion::Composition>());
    composition->duration = 100;
    motion::Layer *layer = document.addLayer(composition->id, std::make_unique<motion::Layer>(motion::LayerType::Shape));
    layer->outPoint = 100;
    auto *content = static_cast<motion::ShapeContent *>(layer->content.get());
    auto shape = std::make_unique<motion::ShapePath>();
    shape->path.setStaticValue(motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true)));
    content->geometry = std::move(shape);
    auto fill = std::make_unique<motion::FillStyle>();
    fill->color.setStaticValue(motion::Color{1, 0, 0, 1});
    layer->styles.push_back(std::move(fill));

    auto frame0 = motion::SceneEvaluator::Evaluate(document, composition->id, 0);
    auto frame1 = motion::SceneEvaluator::Evaluate(document, composition->id, 1);
    ASSERT_TRUE(frame0.hasValue());
    ASSERT_TRUE(frame1.hasValue());
    ASSERT_FALSE(frame0->layers.empty());
    ASSERT_FALSE(frame0->layers[0].shapeItems.empty());
    const uint64_t rev0 = motion::GeometryRevisionAccess::Get(frame0->layers[0].shapeItems[0].geometry.path);
    const uint64_t rev1 = motion::GeometryRevisionAccess::Get(frame1->layers[0].shapeItems[0].geometry.path);
    EXPECT_NE(rev0, 0u);
    EXPECT_EQ(rev0, rev1);
    EXPECT_EQ(frame0->layers[0].shapeNetwork, frame1->layers[0].shapeNetwork);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(frame0->layers[0].shapeNetwork),
              motion::GeometryRevisionAccess::Get(frame1->layers[0].shapeNetwork));
}
```

- [x] **Step 2: 跑测试，看是失败还是已经绿**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.StaticShapePathKeepsCompiledRevisionAcrossFrames'
```

若 Task 3 的 `CompileFillFaces` 封装已经让它通过，仍保留为回归，并继续做 Step 3（双 evaluate 仍是浪费）。失败则由 Step 3 修。

- [x] **Step 3: 一次 evaluate，一次 compile**

`CollectGeometry` 的 Path 分支现在自己 evaluate。不要改它给别的调用方用的语义。在 Shape 图层分支（`src/render/SceneEvaluator.cpp` 的 `LayerType::Shape`）就地 compile：

```cpp
const auto &shapeContent = static_cast<const ShapeContent &>(*layer.content);
EvaluatedLayer evaluated;
FillCommonLayerFields(document, layer, time, world, opacity, evaluated);
std::vector<ShapeGeometry> geometries;
if (shapeContent.geometry != nullptr && shapeContent.geometry->type() == ShapeType::Path) {
    const auto &shape = static_cast<const ShapePath &>(*shapeContent.geometry);
    const VectorNetwork network = shape.path.evaluatePreview(time);
    evaluated.shapeNetwork = network;
    const CompiledVectorNetwork &compiled = CompileVectorNetwork(network);
    ShapeGeometry geometry = MakePathGeometry(compiled.fill);
    geometry.strokePath = compiled.stroke;
    geometries.push_back(std::move(geometry));
} else if (shapeContent.geometry) {
    CollectGeometry(*shapeContent.geometry, time, geometries);
}
if (!layer.styles.empty()) {
    ApplyLayerStyles(document, layer, time, 1.0f, geometries, evaluated.shapeItems);
}
```

`CollectGeometry` 的 Path 分支留给其它调用方（FollowPath / 导出仍自己 `evaluatePreview` + `CompileFillFaces`）。Mask 继续 `CompileFillFaces(network)`，会打到同一份 LRU。

立刻把 `compiled.fill` / `compiled.stroke` 拷进 `ShapeGeometry`，不要把 `CompiledVectorNetwork &` 留过下一次 compile。

- [x] **Step 4: 跑测试**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*:SceneEvaluatorTest.*:FollowPathEvalTest.*'
```

预期：PASS。

- [x] **Step 5: 勾选本 Task 并 commit**

把本 Task 的 **Status:** 改为 ✅ Done，勾上上面步骤。然后 commit。示例信息：`Evaluate ShapePath networks once and reuse compiled geometry.`

**Status:** ✅ Done

---

### Task 5: adapter HashGeometry 使用 BezierPath revision

**文件：**
- 修改：`adapter/tgfx/src/TgfxPathBuilder.cpp`
- 修改：`adapter/tgfx/tests/TgfxPathCacheTest.cpp`

**接口：**
- 依赖：`GeometryRevisionAccess::Get(const BezierPath &)`
- 产出：Path 类型且 revision 非 0 时，`HashGeometry` 的 `contentHash` 用 revision；Rect / Ellipse / 未 stamp 的 Path 仍走逐点哈希

- [ ] **Step 1: 写失败的 HashGeometry 测试**

追加到 `adapter/tgfx/tests/TgfxPathCacheTest.cpp`：

```cpp
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "TgfxPathBuilder.h"

TEST(TgfxPathCacheTest, PathHashUsesRevisionWhenStamped) {
    motion::BezierPath path = motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true);
    motion::ShapeGeometry geometry = motion::MakePathGeometry(path);
    const uint64_t hash = motion::HashGeometry(geometry, motion::FillRule::NonZero);
    geometry.strokePath.contours.push_back(path.contours.front());
    EXPECT_EQ(hash, motion::HashGeometry(geometry, motion::FillRule::NonZero));
    motion::GeometryRevisionAccess::Stamp(geometry.path);
    EXPECT_NE(hash, motion::HashGeometry(geometry, motion::FillRule::NonZero));
}

TEST(TgfxPathCacheTest, UnstampedPathHashFollowsVertices) {
    motion::BezierPath path;
    path.contours.push_back({{{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true});
    ASSERT_EQ(motion::GeometryRevisionAccess::Get(path), 0u);
    motion::ShapeGeometry geometry = motion::MakePathGeometry(path);
    const uint64_t hash = motion::HashGeometry(geometry, motion::FillRule::NonZero);
    geometry.path.contours[0].vertices[1].point = {11, 0};
    EXPECT_NE(hash, motion::HashGeometry(geometry, motion::FillRule::NonZero));
}
```

- [ ] **Step 2: 跑测试，确认失败**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tests/tgfx_adapter_test --gtest_filter='TgfxPathCacheTest.PathHash*:TgfxPathCacheTest.UnstampedPathHash*'
```

二进制路径若不同，改用 `ctest --test-dir build -R 'TgfxPathCacheTest.PathHash' --output-on-failure`。预期：第一条 FAIL（现在的 hash 会吃进 strokePath 顶点 / 忽略 revision）。

- [ ] **Step 3: 切换 Path 哈希**

`adapter/tgfx/src/TgfxPathBuilder.cpp` 的 `HashGeometry`，Path 分支：

```cpp
case ShapeGeometryKind::Path: {
    const uint64_t revision = GeometryRevisionAccess::Get(geometry.path);
    if (revision != 0) {
        hash = MixHash(hash, revision);
        break;
    }
    hash = MixHash(hash, geometry.path.contours.size());
    // existing per-vertex loop unchanged
    break;
}
```

这里不要 hash `strokePath`。adapter 已经把 fill / stroke 拆成两份 `ShapeGeometry`（`drawPath` 清空 `strokePath`；描边把 stroke 轮廓搬到 `path`）。fill 和 stroke 因此用各自的 BezierPath revision。

- [ ] **Step 4: 跑测试**

```bash
cmake --build build --target tgfx_adapter_test core_tests
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*'
ctest --test-dir build -R 'TgfxPathCacheTest' --output-on-failure
```

预期：PASS。

- [ ] **Step 5: 勾选本 Task 并 commit**

把本 Task 的 **Status:** 改为 ✅ Done，勾上上面步骤。然后 commit。示例信息：`Hash stamped path geometry by BezierPath revision.`

**Status:** 未开始

---

### Task 6: 全量验收

**文件：** 只改本 plan 的 checkbox（以及 Step 3 的 spec 状态）

- [ ] **Step 1: 开 ASan 构建并跑相关测试**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build
./build/tests/core_tests --gtest_filter='GeometryRevisionTest.*:VectorNetworkCompileTest.*:VectorNetworkEditTest.*:VectorNetworkTest.*:SceneEvaluatorTest.*'
ctest --test-dir build -R 'TgfxPathCacheTest' --output-on-failure
```

预期：列出的测试全 PASS，无 ASan 报告。

- [ ] **Step 2: 确认 JSON 仍无 revision 字段**

再跑 `GeometryRevisionTest.JsonRoundTripOmitsRevisionAndRestamps`。预期：PASS。

- [ ] **Step 3: 更新 spec 状态（先改文件，等用户确认再 commit）**

上述测试通过后，把 `docs/superpowers/specs/2026-08-21-geometry-revision-cache-design.md` 的 `状态：待确认` 改成 `状态：已实现`。spec / plan / `docs/README.md` 随本 Task 一并 commit。

- [ ] **Step 4: 勾选本 Task**

本 Task 无代码变更，不单独 commit。

**Status:** 未开始

---

## Spec 覆盖

| Spec 要求 | Task |
|---|---|
| 私有 `revision_`，无公开 `revision()` / `stampRevision()` | 1 |
| `GeometryRevisionAccess` 辅助类 `Get` / `Stamp`；类型只 friend | 1 |
| `GenerateGeometryRevision` 只在 src 匿名命名空间、永不返回 0 | 1 |
| `operator==` 忽略 revision | 1 |
| 工厂 / 编辑 / lerp / Animatable 写入 / FromJson stamp | 2 |
| no-op 编辑不换号 | 2 |
| evaluate / Hold 拷贝保留 revision | 2 |
| JSON 不写 revision；反序列化重新 stamp | 2 |
| `CompileVectorNetwork` LRU 64、thread_local、key = network revision | 3 |
| revision 0 不进 LRU | 3 |
| fill / stroke 各自 stamp | 3 |
| ShapePath 一次 evaluate + 一次 compile | 4 |
| Mask 仍走 `CompileFillFaces`（同一 LRU） | 4 |
| `HashGeometry` Path 用 BezierPath revision | 5 |
| 未 stamp 的 Path 仍走逐点哈希 | 5 |
| 不做 COW / StaticTimeRanges / 升 schemaVersion | 全部（明确不做） |
