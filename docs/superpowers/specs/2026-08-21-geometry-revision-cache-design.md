# Geometry revision 缓存 — 设计说明

日期：2026-08-21
状态：已实现
关联：[时间轴求值](../../timeline-evaluation.md)、[渲染](../../rendering.md)、[PreviewSceneCache](./2026-08-12-preview-scene-cache-design.md)

## 目标

静态 / Hold 的 `VectorNetwork` 在连续帧上跳过 `CompileFillFaces` / `CompileStrokeEdges`；编译出的 `BezierPath` 再让 adapter 用同一枚身份跳过 `BezierPath → tgfx::Path` 的逐点哈希。

查找键是进程内单调 `uint64_t revision`，**不**用 `operator==`，**不**把 `tgfx::Path` 放进 Core。

## 非目标

- StaticTimeRanges / `excludeVaryingRanges`（后续独立 spec）
- `VectorNetwork` / `BezierPath` 改 `shared_ptr` 或内部 COW
- 升 `schemaVersion`；JSON 不写 `revision`
- 改 `PreviewSceneCache` / `FrameCommandCache` 的时间键
- Rect / Ellipse 参数几何（它们没有 `VectorNetwork` 编译）

## 已锁定决策

| 项 | 选择 |
|---|---|
| 类型 | `VectorNetwork` / `BezierPath` 仍是公开值类型；几何字段保持公开 |
| 身份 | 私有 `uint64_t revision_`。类型上**没有** `revision()` / `stampRevision()` |
| 访问 | `GeometryRevisionAccess` 辅助类静态方法 `Get` / `Stamp`；`VectorNetwork` / `BezierPath` 只 `friend class GeometryRevisionAccess`。App / Bridge / 数据模型不调用 |
| 发号 | `GenerateGeometryRevision()` 匿名命名空间，只在 `src/common/GeometryRevision.cpp` |
| `0` | 未打戳：不进编译 LRU；adapter 回退现有逐点 `HashGeometry` |
| `operator==` | 仍只比几何，**不比** `revision_` |
| 序列化 | 不读写 `revision_`；反序列化后 `GeometryRevisionAccess::Stamp` |
| 拷贝 / 静态 `evaluate` / Hold | 默认拷贝带走 `revision_` |
| 内容变化 | `GeometryRevisionAccess::Stamp` |
| 编译缓存位置 | 藏在 `CompileFillFaces` / `CompileStrokeEdges` 里 |
| 编译 key | `GeometryRevisionAccess::Get(network)`（非 time / entity） |
| fill / stroke 派生号 | **各** `GeometryRevisionAccess::Stamp`，禁止 `MixHash(networkRevision, 1/2)` |
| tgfx 缓存 | 仍在 `TgfxPathCache`；Path kind 且 `GeometryRevisionAccess::Get(path) != 0` 时用它当 `contentHash` |
| 漏 stamp | 就地改公开 `vertices`/`edges` 后未 stamp 会脏命中。约定只经工厂 / Edit / Animatable 写入；那些路径负责 stamp |
| COW / shared_ptr | 不做 |
| 双求值 | ShapePath 一次 `evaluatePreview`，同一份 network 同时给 compile 和 `shapeNetwork` |

## 问题

当前每帧：

```
evaluatePreview(VectorNetwork)     // 按值拷贝
  → CompileFillFaces               // planarize + 求交 + 抽面
  → CompileStrokeEdges
  → ShapeGeometry { path, strokePath }
  → TgfxPathCache::Resolve         // HashGeometry 逐顶点
  → BezierToTgfxPath
```

`PreviewSceneCache` / `FrameCommandCache` 按精确 `PreviewTime` 命中。播放时每帧时间不同，几乎全 miss，挡不住顺序播放。

`CompileFillFaces` 是静态路径上最贵的一步。`operator==` / 逐点哈希在大网络上同样贵，不能当查找键。

## 架构

```
GenerateGeometryRevision()     src/ 匿名命名空间；atomic 从 1，永不返回 0
        │
        ▼
GeometryRevisionAccess::Get(network)  ──Core LRU──►  CompiledVectorNetwork
                                                  ├── GeometryRevisionAccess::Stamp(fill)
                                                  └── GeometryRevisionAccess::Stamp(stroke)
                                                        │
                                                        ▼
                                             adapter TgfxPathCache
                                             contentHash = GeometryRevisionAccess::Get(path)
                                                        │
                                                        ▼
                                                   tgfx::Path
```

两层缓存，一次 identity：

1. **Core**：`VectorNetwork.revision → {fill, stroke}` BezierPath
2. **adapter**：`BezierPath.revision → tgfx::Path`（替换 Path kind 的逐点哈希）

Core 不持有 `tgfx::Path`。StaticTimeRanges 之后挂在 `Animatable` / 图层求值上，这层 key 仍是 revision，不是时间。

## 接口

`VectorNetwork` / `BezierPath` 仍是公开值类型。缓存身份不出现在这两个头的公开 API 上：无私有之外的成员方法。读写走独立头 `include/MotionStudio/common/GeometryRevision.h`。

```cpp
class GeometryRevisionAccess {
  public:
    static uint64_t Get(const VectorNetwork &network);
    static void Stamp(VectorNetwork &network);
    static uint64_t Get(const BezierPath &path);
    static void Stamp(BezierPath &path);
};

struct VectorNetwork {
    struct Vertex { /* 不变 */ };
    struct Edge { /* 不变 */ };

    std::vector<Vertex> vertices;
    std::vector<Edge> edges;

    bool operator==(const VectorNetwork &other) const;
    bool operator!=(const VectorNetwork &other) const;

  private:
    uint64_t revision_ = 0;
    friend class GeometryRevisionAccess;
};
```

`BezierPath` 同样：公开 `contours`，私有 `revision_`，`friend class GeometryRevisionAccess`。

`operator==` 不比较 `revision_`。默认拷贝/赋值拷贝 `revision_`。

`GeometryRevisionAccess` 只给 Core 编译缓存、Animatable / Interpolator / Edit / 序列化、adapter `HashGeometry`、以及本 spec 的测试 include。不出现在 Bridge。发号器 `GenerateGeometryRevision()` 放在 `src/common/GeometryRevision.cpp` 匿名命名空间，永不返回 0。

### 编译缓存

现有 `CompileFillFaces` / `CompileStrokeEdges` 签名不变，内部走同一入口：

```cpp
struct CompiledVectorNetwork {
    BezierPath fill;
    BezierPath stroke;
};

const CompiledVectorNetwork &CompileVectorNetwork(const VectorNetwork &network);
```

`CompileVectorNetwork` 可公开（SceneEvaluator 一次取 fill+stroke，少一次查找），也可只留在 `.cpp`。实现时 SceneEvaluator / Mask 热路径必须一次取两个结果。

查找：

```
if (GeometryRevisionAccess::Get(network) == 0) {
    compile uncached;
    GeometryRevisionAccess::Stamp(fill);
    GeometryRevisionAccess::Stamp(stroke);
    do not insert LRU;
}
else if (LRU hit) {
    return cached;  // fill/stroke revisions unchanged
}
else {
    compile;
    GeometryRevisionAccess::Stamp(fill);
    GeometryRevisionAccess::Stamp(stroke);
    insert LRU keyed by GeometryRevisionAccess::Get(network);
}
```

- LRU 容量 **64**，thread_local（预览 / 导出 / 测试线程各一份，无全局锁）
- 返回的 `const CompiledVectorNetwork &` 只在下一次同线程 `CompileVectorNetwork` 之前有效。调用方立刻拷出 `fill` / `stroke`。`CompileFillFaces` / `CompileStrokeEdges` 仍按值返回
- 未命中才跑现有 `BuildCurvePlanarNetwork` + face walk / stroke walk
- 内部中间 `VectorNetwork`（planar）**不** stamp

### adapter

`HashGeometry(const ShapeGeometry &, FillRule)`：

- `kind == Path` 且 `GeometryRevisionAccess::Get(geometry.path) != 0`：`contentHash = GeometryRevisionAccess::Get(geometry.path)`（再 mix `kind` / `fillRule`，与现结构兼容）
- 否则：保持现有逐点哈希（Rect / Ellipse / 未打戳 Path）

`TgfxPathCache` 的 key 结构不变。adapter 热路径已经把 fill / stroke 拆成两份 `ShapeGeometry`（`strokePath` 清空或搬到 `path`），因此 fill 与 stroke 用各自 `BezierPath.revision`，不会串。

导出路径 `PagStrokeOutline.cpp` 的 `BezierToTgfxPath` **不**走 `TgfxPathCache`，本阶段不改。

## Stamp 契约

| 事件 | `revision_` |
|---|---|
| 拷贝 / 静态 `evaluate` / Hold / `takeKeyframe` | 默认拷贝带走 |
| 工厂产出新几何 | `GeometryRevisionAccess::Stamp` |
| 就地改完再写回 `Animatable` | `setStaticValue` / `addKeyframe` / `updateKeyframe` 再 stamp |
| `Interpolator::Lerp` 写出的新值 | stamp（`result = from` 会带上旧号，改点后必须换） |
| 反序列化 | JSON 无此字段；`FromJson` 结束时 stamp |
| 编译输出的 fill / stroke | cache miss 时各 stamp；hit 时保持 |
| 编辑 no-op（目标不存在、原样返回） | **不**换号 |
| `0` | 不进编译 LRU |

写入路径一律 `GeometryRevisionAccess::Stamp`，避免「先改 `vertices[i].point` 再 `setStaticValue`」仍拿旧号。undo 恢复会领新号、多编译一次，可接受。

公开 `vertices` / `edges` / `contours` 仍可就地改。未 stamp 时旧号还在，编译 LRU **会脏命中**。因此 stamp 必须落在所有写回 Document 的边界（Edit 函数、Animatable 写入、工厂），而不是依赖调用方记得。测试覆盖 Edit / Animatable 这些边界；不把「随便改 `vertices` 再 compile」当成合法用法。

### 必须 stamp 的落点

**工厂 / 转换**

- `PathToVectorNetwork`
- `BezierPathToVectorNetwork` / `ContourToVectorNetwork`
- `MakeSingleContour`
- `VectorNetworkToSingleRingBezierPath`（经 `MakeSingleContour`）
- `RectToBezierPath` / `EllipseToBezierPath`（给 hit-test / 导出用的 BezierPath；adapter 的 Rect/Ellipse 快路径不走这份）

**编辑（几何确实变了才 stamp）**

- `VectorNetworkEdit.*`（`AddVertex` / `MoveVertex` / `InsertVertexOnEdge` / …）
- `PathGeometryEdit.*`
- `TransformNetwork` / `TransformBezierPath` / `RecenterNetwork` / `RecenterPath`

**动画**

- `Interpolator<VectorNetwork>::Lerp`：拓扑不同 hold `from`（保留 from 的 `revision_`）；同拓扑写出新 network 后 `GeometryRevisionAccess::Stamp`
- `Interpolator<BezierPath>::Lerp`：同样
- `Animatable<VectorNetwork>` / `Animatable<BezierPath>` 的 `setStaticValue` / `addKeyframe` / `updateKeyframe`（`if constexpr`，其它 T 不动）

**序列化**

- `VectorNetworkFromJson` / `BezierPathFromJson` 成功返回前 stamp
- `ToJson` 不写 `revision`

**求值**

- `evaluate` / `evaluatePreview` **不** stamp
- ShapePath：一次 `evaluatePreview`，结果同时用于 `CompileVectorNetwork` 和 `EvaluatedLayer.shapeNetwork`

默认构造 `revision_ == 0` 的 network 不进 LRU。测试覆盖「未 stamp → 两次 Compile 结果几何相等但 `GeometryRevisionAccess::Get(fill)` 不同 / 不入 LRU」。

## SceneEvaluator

ShapePath 现状求值两次（`shapeNetwork` + `CollectGeometry`）。改为：

```cpp
const VectorNetwork network = shape.path.evaluatePreview(time);
evaluated.shapeNetwork = network;
const CompiledVectorNetwork &compiled = CompileVectorNetwork(network);
ShapeGeometry geometry = MakePathGeometry(compiled.fill);
geometry.strokePath = compiled.stroke;
```

Mask：

```cpp
const VectorNetwork network = mask.path.evaluatePreview(time);
evaluatedMask.network = network;
evaluatedMask.path = CompileFillFaces(network);  // 内部走 CompileVectorNetwork
```

## 与后续 StaticTimeRanges 的关系

```
StaticTimeRanges（后做）     时间 → 这段 Animatable 没变，连 evaluate 都跳过
        ↓ 仍会调用的那些帧
CompileVectorNetwork（本 spec）  revision → 已编译 BezierPath
        ↓
TgfxPathCache                    GeometryRevisionAccess::Get(path) → tgfx::Path
```

本层 key 不含时间 / entity。静态区间首帧、路径 morph、多层共用同一份已 stamp 的 network、路径编辑 / 导出，之后仍走这层。

## 测试

新文件 `tests/common/GeometryRevisionTest.cpp`（或并入现有 `VectorNetworkCompileTest` / `VectorNetworkTest`）：

1. 默认构造 `GeometryRevisionAccess::Get == 0`；stamp 后非 0，连续两次 stamp 递增
2. 拷贝保留 revision；改几何后 stamp 则变
3. `operator==` 忽略 revision（两份几何相同、revision 不同 → 相等）
4. JSON round-trip：输出无 `revision` 字段；反序列化后 revision ≠ 0，且两次反序列化 revision 不同
5. 同一 network revision 两次 `CompileFillFaces`：fill 几何相等 **且** fill 的 revision 相同（命中 LRU）
6. revision == 0：两次 compile 几何相等，但不要求 fill revision 相同，且不污染后续 stamp 过的条目
7. `Interpolator` 同拓扑 lerp：结果 revision 既不等于 from 也不等于 to
8. `Interpolator` 拓扑不同：返回 from，revision 保持 from
9. `evaluatePreview` 无关键帧：返回值 revision 等于 `staticValue()` 的 revision
10. Hold / 钳制到首末帧：revision 等于对应关键帧
11. `MoveVertex` 命中 stamp；顶点不存在 no-op 不换号
12. ShapePath 静态路径连续两帧：`shapeItems[0].geometry.path` 的 revision 相同（回归双求值 + 编译缓存）

adapter（`tgfx_adapter_test`，若改 `HashGeometry`）：

- Path kind + 非 0 revision：`HashGeometry` 不随无关 `strokePath` 变化；stamp 后 hash 变
- revision == 0：仍走逐点哈希（与现行为一致）

现有 `VectorNetworkCompileTest` / 序列化 round-trip / SceneEvaluator 路径用例应继续全绿（它们比几何，不比 revision）。

## 文件

| 文件 | 变更 |
|---|---|
| `include/MotionStudio/common/VectorNetwork.h` | 私有 `revision_`；`friend class GeometryRevisionAccess` |
| `include/MotionStudio/common/BezierPath.h` | 同上 |
| `include/MotionStudio/common/GeometryRevision.h` | `GeometryRevisionAccess::Get` / `Stamp` |
| `src/common/GeometryRevision.cpp` | 发号器 + `Get` / `Stamp` 实现 |
| `include/MotionStudio/common/VectorNetworkCompile.h` | 可选公开 `CompileVectorNetwork` |
| `src/common/VectorNetworkCompile.cpp` | thread_local LRU |
| `src/common/VectorNetworkEdit.cpp` / `PathGeometryEdit.cpp` / `VectorNetworkConvert.cpp` / `BezierPath.cpp` | stamp |
| `src/import/svg/SvgPathConvert.cpp` / `SvgTransform.cpp` | stamp |
| `src/animation/Interpolator.cpp` / `Animatable.cpp` | lerp / 写入 stamp |
| `src/serialization/Serializer.cpp` | FromJson stamp；ToJson 不动 |
| `src/render/SceneEvaluator.cpp` | 一次 evaluate；一次 compile |
| `adapter/tgfx/src/TgfxPathBuilder.cpp` | `HashGeometry` Path 走 revision |
| `tests/common/GeometryRevisionTest.cpp` | 上表用例 |
| `docs/README.md` | 索引链接 |

schemaVersion 不变。
