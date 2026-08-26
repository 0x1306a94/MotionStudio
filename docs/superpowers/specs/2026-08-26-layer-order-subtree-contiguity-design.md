# 图层顺序子树连续性 — 设计说明

日期：2026-08-26
状态：待实现
关联：[数据模型](../../data-model.md)、[Group / Ungroup](./2026-08-19-layer-group-ungroup-design.md)

## 目标

把「同一父级的后代在 `Composition::layers` 数组里必须紧贴其父级」这条**目前无人拥有的隐式不变量**变成 Core 集中强制的显式契约，并删掉 App 侧三份重复推导。

保持扁平存储与序列化格式不变（`layers[]` + `parentId`），不改 `schemaVersion`。

## 非目标

- 不把数据模型或 `document.json` 改成嵌套树
- 不改 `Layer::parentId` 语义、不动 `worldTransform` / `SceneEvaluator` 求值
- 不改 Timeline 的「反转数组 + 按深度缩进」渲染方式
- 不做 group 折叠 / 展开、不做「进入 group 内部编辑」隔离模式
- 不改 `MoveLayerCommand` 的全局线性索引语义（`(parentId, index)` 化不在本 spec）

## 问题

`Composition::layers` 同时承担两件事：

1. **绘制顺序** —— index 0 最底，`SceneEvaluator.cpp:601` 顺序遍历
2. **Timeline 树形显示的隐式前提** —— `TimelineViewController.swift:286` 反转数组，`TimelineLayerTree.parentDepth` 按 `parentId` 链算缩进。因此子层只有紧贴父层时才显示在父层之下

第 2 条是真实不变量，但代码里没有任何地方拥有它。每个改顺序 / 改父子的路径各自重新推导一遍，于是出现三对几乎同名的重复实现：

| 逻辑 | C++ | Swift |
|---|---|---|
| 后代随迁 | `HasSelectedAncestor`（`src/undo/GroupLayers.cpp`） | `movingIDsIncludingDescendants`（`TimelineReorder.swift:206`） |
| 顺序差分 | `MoveSteps`（`src/undo/GroupLayers.cpp`） | `moveSteps`（`TimelineReorder.swift:64`） |
| 父深度 | `ParentDepth`（`src/undo/GroupLayers.cpp`） | `parentDepth`（`TimelineReorder.swift:178`） |

commit `6c7e395` 修的就是这套重复实现里 Core 的 Group 漏了一份「后代随迁」：分组 `Face`(含 `Label`/`FaceShape`) + `Base` 后，`desired` 只搬了 `Face`/`Base` 本身，`Label`/`FaceShape` 留在原位，视觉上变成了 `Base` 的子层。

同类漏法仍然存在：`SetParentCommand` 只改 `parentId`、完全不碰数组位置，任何单独调用它的路径（例如后续做拖拽改父级）都会再犯。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 存储 | 保持扁平 `layers[]` + `parentId`，**不**改树 |
| 不变量归属 | Core 新增纯函数 `NormalizeSubtreeContiguousOrder`，所有产出目标顺序的路径统一过一遍 |
| 函数位置 | `model/`（不是 `undo/`）—— 只依赖 `parentId`，`SetParent` 路径也要用 |
| 排序语义 | 稳定 DFS，后代先出（index 小 = 底层 = 显示在父级之下） |
| 兄弟顺序 | 保留 `desired` 里的相对顺序 |
| 父级缺失 | 视为根（不在 `desired` 里的 parent 同样视为根），不报错 |
| 环 | `Layer::setParent` 已拒环；normalize 遇环时按已访问集合兜底，不死循环 |
| assert | `UndoManager` 的 execute / undo / redo 后 debug 断言不变量成立 |
| `SetParentCommand` | 保留裸命令（供 composite 内部使用）；新增 `MakeSetParentCommand` 工厂返回 `SetParent + MoveLayer steps` 的 composite |
| C ABI | 新增 `ms_command_apply_layer_order`，Core 内部 normalize + 算 move steps |
| Swift 去重 | 删 `TimelineReorder.moveSteps`、`TimelineLayerTree.parentDepth`、`movingIDsIncludingDescendants`；改为经 bridge 取 |
| `schemaVersion` | 不变 |

## 为什么不改成嵌套树

树能让连续性变成结构性免费，但代价集中在两处：

- **undo 命令的索引语义**：`MoveLayerCommand(compositionId, from, to)` 是全局线性索引，还带 `mergeWith` 的连续拖拽合并（`src/undo/MoveLayerCommand.cpp`）。改树要变成 `(parentId, index)`，所有下发点、合并逻辑、`AddLayerCommand(..., -1)` 的语义全部重写。
- **序列化与能力**：`document.json` 现在是扁平 `layers[]` + `parentId`，改树需要 `SchemaMigrator`；且树结构**丢掉**了「父子关系与绘制顺序解耦」这一能力 —— AE 里子层可以在父层之下的任意位置绘制，扁平存得下，树存不下。

而树的主要收益（父子跳转不依赖数组邻接）`EntityIndex` 的 O(1) 寻址已经提供。

**何时该反悔**：若将来要做 per-group 裁剪 / 独立 blend 上下文（`src/render/CommandBuilder.cpp` 的 `IsIsolatingGroup` 已有雏形）并允许深层嵌套，或要支持「进入 group 内部编辑」的隔离模式 —— 那时绘制顺序本身就是分层的，树才划算。现阶段未到。

## 架构

```
调用方产出 desired（任意顺序）
        │
        ▼
NormalizeSubtreeContiguousOrder(desired, composition)     model/LayerOrder.h
        │  稳定 DFS，后代紧贴父级之下
        ▼
MoveSteps(current, normalized)                            复用现有差分
        │
        ▼
MoveLayerCommand × N  →  composition.layers
        │
        ▼
IsSubtreeContiguousOrder(composition)  ← UndoManager debug assert
```

单一权威点在 `NormalizeSubtreeContiguousOrder`。调用方不再自己枚举后代。

## 接口

### Core：`include/MotionStudio/model/LayerOrder.h`

```cpp
// Reorders `desired` so every layer's descendants sit immediately below it (lower index =
// drawn first = shown under the parent in the timeline). Sibling relative order and the
// relative order of subtree blocks both follow `desired`. Layers whose parentId is invalid
// or points outside `desired` are treated as roots. Ids not present in the composition are
// dropped; composition layers missing from `desired` are appended in composition order.
std::vector<EntityId> NormalizeSubtreeContiguousOrder(const std::vector<EntityId> &desired,
                                                     const Composition &composition);

// True when composition.layers already satisfies the subtree-contiguous invariant.
bool IsSubtreeContiguousOrder(const Composition &composition);

// Builds the MoveLayerCommand steps that turn `from` into `to`. Returns empty when the two
// are not permutations of each other. Extracted from GroupLayers.cpp so every reorder path
// shares one implementation.
std::vector<std::pair<int, int>> LayerMoveSteps(const std::vector<EntityId> &from,
                                                const std::vector<EntityId> &to);
```

DFS 伪代码：

```
childrenOf = {}                       // parentId -> [ids]，按 desired 顺序 append
roots      = []
for id in desired:
    parent = layerOf(id).parentId
    if parent 有效 且 在 desired 里: childrenOf[parent].append(id)
    else:                             roots.append(id)

visited = {}
emit(id):
    if id in visited: return          // 环兜底
    visited.insert(id)
    for child in childrenOf[id]: emit(child)   // 后代先出
    result.append(id)
for r in roots: emit(r)
```

对 `DuoButton.motionproject` 验证：`desired = [FaceShape, Label, Base, Face, Group]` → roots = `[Group]`，`childrenOf[Group] = [Base, Face]`，`childrenOf[Face] = [FaceShape, Label]` → 结果 `[Base, FaceShape, Label, Face, Group]` ✓ 与 `6c7e395` 的修复等价，但不再依赖调用方自己搬后代。

### Core：`MakeSetParentCommand` 工厂

放 `include/MotionStudio/undo/GroupLayers.h`（该头已是「组合命令工厂」的归属地）：

```cpp
// Reparents a layer and repositions its subtree so the composition stays subtree-contiguous.
// Returns nullptr when the layer is missing or setParent would create a cycle.
std::unique_ptr<Command> MakeSetParentCommand(const Document &document, EntityId compositionId,
                                              EntityId layerId, EntityId newParentId);
```

组成：`SetParentCommand(layerId, newParentId)` + normalize 后的 `MoveLayerCommand` steps。裸 `SetParentCommand` 保留，仅用于「已在 composite 内、后续会统一 normalize」的场合（`MakeGroupLayersCommand` / `MakeUngroupLayersCommand`）。

### Bridge：`ms_command_apply_layer_order`

```c
// Applies an absolute model order (bottom -> top). Ids are normalized so subtrees stay
// contiguous, then emitted as one undo unit. Returns false on no-op.
bool ms_command_apply_layer_order(MSDocument *document, uint64_t compositionId,
                                  const uint64_t *layerIds, size_t count);

// Number of parentId hops from the layer to its root. 0 for a root layer or a missing layer.
int ms_layer_parent_depth(MSDocument *document, uint64_t layerId);
```

`ms_command_apply_layer_order` 内部：normalize → `LayerMoveSteps` → 包成一个 `CompositeCommand("Reorder Layers")`。这让 Swift 不再需要自己算 move steps，且拖拽 / Arrange 路径自动受不变量保护。

## 接入点

### `MakeGroupLayersCommand`

删掉 `6c7e395` 引入的 `topLevelSet` 后代枚举，恢复成「只把选中层挪到一起」，末尾统一 normalize：

```cpp
std::vector<EntityId> desired = /* remaining[0..insertAt) + selectedOrder + group + remaining[insertAt..) */;
desired = NormalizeSubtreeContiguousOrder(desired, *composition);
```

注意 normalize 在 `AddLayerCommand` 之后的顺序上运行，但此时新 group 还没进 `composition`，其 `parentId` 也未被 `SetParentCommand` 应用。因此 normalize **不能**只读 `composition`：需要能感知「即将建立的父子关系」。

解决：`MakeGroupLayersCommand` 自行构造 `parentOf` 覆盖表（把选中层的 parent 预设为新 group、新 group 的 parent 预设为 `commonParent`），并调用接受覆盖表的重载：

```cpp
// Same as above, but `parentOverrides` wins over Layer::parentId. Lets command factories
// normalize against the parent relationships they are about to establish. Ids that appear
// in `parentOverrides` are kept even when the composition does not contain them yet, so a
// layer about to be added can be ordered alongside its future children.
std::vector<EntityId> NormalizeSubtreeContiguousOrder(
    const std::vector<EntityId> &desired, const Composition &composition,
    const std::unordered_map<EntityId, EntityId> &parentOverrides);
```

不带覆盖表的重载转调此版本，传空表。

### `MakeUngroupLayersCommand`

`RemoveLayerCommand` 移除 group 后，原孩子的位置目前靠运气正确。改为在末尾追加 normalize 后的 move steps，`parentOverrides` 把被 ungroup 的孩子指向 `restoredParent`。

注意 normalize 会把「composition 里有、`desired` 里缺」的层按 composition 顺序追加回来，因此待删除的 group 必须在 normalize **之后**再滤掉一次。此时它们已是无子的根，移除不破坏其余子树的连续性。

### `UndoManager` debug assert

`execute` / `undo` / `redo` 之后，对文档内所有 composition 断言 `IsSubtreeContiguousOrder`。参照项目现有 `documentFingerprint` 式 debug 校验的接法，仅 debug 生效。这是把不变量真正钉死的地方 —— 以后任何新命令破坏它都会在测试里炸。

### Swift 去重

| 删除 | 替代 |
|---|---|
| `TimelineReorder.moveSteps`（`TimelineReorder.swift:64`） | `ms_command_apply_layer_order` 内部完成 |
| `TimelineLayerTree.parentDepth`（`:178`） | `ms_layer_parent_depth` |
| `TimelineLayerTree.movingIDsIncludingDescendants`（`:206`） | normalize 已保证子树随迁，调用方只传选中层 |

`MotionDocumentCore.applyLayerOrder`（`MotionDocumentCore.swift:1855`）改为直接转发 `ms_command_apply_layer_order`，不再在 Swift 侧算 steps、不再自己比较 `current != desired`（Core 返回 false 即 no-op）。

调用点：`TimelineSidebarView.swift:382`（Arrange）、`:431`/`:439`（拖拽落点）、`:461` —— 去掉 `movingIDsIncludingDescendants` 包装，直接传选中层集合给 `TimelineReorder.arrangedLayerIDs` / `reorderedLayerIDs`。

`TimelineReorder.arrangedLayerIDs` / `reorderedLayerIDs` / `uiInsertSlot` / `layerBlockFrames` **保留** —— 它们是 UI 落点几何计算，不是树结构推导。

## 测试

`tests/model/LayerOrderTest.cpp`（新建）：

1. 空 `desired` → 空结果；单层 → 原样
2. 无父子关系 → 原样返回（normalize 不打乱扁平列表）
3. `DuoButton` 结构：`[FaceShape, Label, Base, Face, Group]` → `[Base, FaceShape, Label, Face, Group]`
4. 兄弟相对顺序保留：`childrenOf[Face] = [Label, FaceShape]` 与 `[FaceShape, Label]` 分别得到对应结果
5. 多层嵌套（group 内 group）子树整块连续
6. `parentOverrides` 生效：override 覆盖 `Layer::parentId`
7. `desired` 缺层 → 缺的按 composition 顺序追加；`desired` 含不存在 id → 丢弃
8. 环（人为构造 `parentId` 环，绕过 `setParent`）不死循环
9. `IsSubtreeContiguousOrder`：正例 / 反例各一
10. `LayerMoveSteps`：非排列返回空；正常情况逐步应用后等于 `to`

`tests/undo/GroupLayersTest.cpp`（补充）：

11. `KeepsExistingSubtreeContiguous` 继续全绿（已存在，`6c7e395` 引入）
12. Ungroup 后剩余层仍满足子树连续
13. `MakeSetParentCommand`：把层挂到已有 group 下，数组位置随之移动；undo 完全还原
14. `MakeSetParentCommand` 环 / 目标缺失 → 返回 nullptr

`bridge/tests/BridgeTest.cpp`（补充 `bridge_test`）：

15. `ms_command_apply_layer_order` 传打乱顺序 → 落地为 normalize 后的顺序，一次 undo 全部还原
16. `ms_command_apply_layer_order` 传当前顺序 → 返回 false，不产生 undo 条目
17. `ms_layer_parent_depth`：根 = 0，两级嵌套 = 2，缺失 id = 0

现有 841 个测试须继续全绿。

## 文件

| 文件 | 变更 |
|---|---|
| `include/MotionStudio/model/LayerOrder.h` | 新建：normalize / is-contiguous / move-steps |
| `src/model/LayerOrder.cpp` | 新建：稳定 DFS 实现 |
| `include/MotionStudio/undo/GroupLayers.h` | 新增 `MakeSetParentCommand` 声明 |
| `src/undo/GroupLayers.cpp` | 删 `topLevelSet` 枚举与本地 `MoveSteps`/`ParentDepth`；Group / Ungroup 接 normalize；实现 `MakeSetParentCommand` |
| `src/undo/UndoManager.cpp` | execute / undo / redo 后 debug assert |
| `bridge/include/motionstudio_bridge.h` | `ms_command_apply_layer_order`、`ms_layer_parent_depth` |
| `bridge/src/common/motionstudio_bridge_commands.cpp` | 上述实现 |
| `apps/.../Timeline/Root/TimelineReorder.swift` | 删 `moveSteps` / `parentDepth` / `movingIDsIncludingDescendants` |
| `apps/.../Model/MotionDocumentCore.swift` | `applyLayerOrder` 转发新 C ABI；新增 `layerParentDepth` |
| `apps/.../Timeline/Sidebar/TimelineSidebarView.swift` | 去掉后代枚举包装；缩进改用 `layerParentDepth` |
| `tests/model/LayerOrderTest.cpp` | 新建 |
| `tests/undo/GroupLayersTest.cpp` | 补 Ungroup / SetParent 用例 |
| `bridge/tests/BridgeTest.cpp` | 补 apply-order / parent-depth 用例 |
| `docs/README.md` | 索引链接 |

`schemaVersion` 不变。
