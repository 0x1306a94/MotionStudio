# 图层顺序子树连续性实现计划

> **给执行代理：** 必须使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**目标：** 把「后代在 `Composition::layers` 里紧贴父级」这条隐式不变量收敛为 Core 一个权威纯函数 + debug assert，并删掉 App 侧三份重复推导。

**架构：** 新增 `model/LayerOrder.h` 提供稳定 DFS 的 `NormalizeSubtreeContiguousOrder`（含 `parentOverrides` 重载，供命令工厂对「即将建立的父子关系」归一化）、`IsSubtreeContiguousOrder`、`LayerMoveSteps`。Group / Ungroup / 新增的 `MakeSetParentCommand` / 新 C ABI `ms_command_apply_layer_order` 全部经它产出目标顺序。`UndoManager` 在 execute / undo / redo 后 debug 断言不变量。

**技术栈：** C++17 Core、extern "C" bridge、GoogleTest、Swift（UIKit Timeline）。

**Spec：** [docs/superpowers/specs/2026-08-26-layer-order-subtree-contiguity-design.md](../specs/2026-08-26-layer-order-subtree-contiguity-design.md)

## 全局约束

- 对话用中文；代码和注释用英语。
- 禁止 `dynamic_cast`、C++ 异常、lambda（改用显式函数）。
- `if` / `while` / `switch` 的 `case` 分支体必须用 `{}`；成员用 `= {}` 初始化。
- `enum class` 每个 enumerator 单独一行。
- 不改 `schemaVersion`；不改序列化格式；不动 `SceneEvaluator` 求值。
- 保持扁平存储，**不**改嵌套树。
- 复用现有 `EntityIndex` O(1) 寻址；不要新建平行索引。
- Task 1–4 是 Core / bridge / 非视觉测试：**做完自动 commit**（仅 commit，不 push），`git commit --only` 显式列文件，英文句号结尾，禁止 `git add -A`。
- **Task 5 涉及可见界面（Timeline 缩进与拖拽）：做完停下来给用户看，确认后才 commit。**
- spec / plan 已确认：先单独 commit spec + plan，再开始 Task 1。
- 每完成一个 Step 或整个 Task，立刻把本文件对应 `- [ ]` 改为 `- [x]` 并更新 `**Status:**`，随该 Task 的代码 commit 一并提交。
- 每个 Task 结束前跑：`cmake --build build && ctest --test-dir build --output-on-failure -LE benchmark`，全绿才算完成。

---

### Task 1: LayerOrder 权威纯函数

**Status:** ⬜ Not started

**文件：**
- 新建：`include/MotionStudio/model/LayerOrder.h`
- 新建：`src/model/LayerOrder.cpp`
- 测试：`tests/model/LayerOrderTest.cpp`

**接口：**
- 依赖：`Composition`、`Layer`、`EntityId`
- 产出：
  - `NormalizeSubtreeContiguousOrder(desired, composition)`
  - `NormalizeSubtreeContiguousOrder(desired, composition, parentOverrides)`
  - `IsSubtreeContiguousOrder(composition)`
  - `LayerMoveSteps(from, to)`

- [ ] **Step 1: 写失败测试**

新建 `tests/model/LayerOrderTest.cpp`（`tests/CMakeLists.txt` 若非自动 glob 则需登记）。覆盖 spec 测试清单 1–10：

- 空 / 单层 → 原样
- 无父子关系的扁平列表 → 原样（normalize 不打乱）
- `DuoButton` 结构：`[FaceShape, Label, Base, Face, Group]` → `[Base, FaceShape, Label, Face, Group]`
- 兄弟相对顺序保留（`[Label, FaceShape]` 与 `[FaceShape, Label]` 各自对应）
- 多层嵌套（group 内 group）整块连续
- `parentOverrides` 覆盖 `Layer::parentId`
- `desired` 缺层 → 按 composition 顺序追加；含不存在 id → 丢弃
- 人为构造 `parentId` 环（绕过 `setParent` 直接写字段）不死循环
- `IsSubtreeContiguousOrder` 正例 / 反例
- `LayerMoveSteps`：非排列 → 空；正常逐步应用后等于 `to`

- [ ] **Step 2: 实现**

`src/model/LayerOrder.cpp` 按 spec 伪代码实现稳定 DFS。要点：

- 后代先 `emit`（index 小 = 底层 = 显示在父级之下）
- `childrenOf` 用 `std::unordered_map<EntityId, std::vector<EntityId>>`，按 `desired` 顺序 append 以保留兄弟相对顺序
- `visited` 集合兜底环
- `parentOverrides` 优先于 `Layer::parentId`
- 不带覆盖表的重载转调三参版本传空表
- 避免 lambda：DFS 用显式递归函数（文件内静态函数）
- `LayerMoveSteps` 从 `src/undo/GroupLayers.cpp` 的 `MoveSteps` 原样搬来，改为公开命名

- [ ] **Step 3: 验证**

`./build/tests/core_tests --gtest_filter='LayerOrderTest.*'` 全绿，随后全量 ctest。

---

### Task 2: Group / Ungroup 接入 normalize

**Status:** ⬜ Not started

**文件：**
- 修改：`src/undo/GroupLayers.cpp`
- 测试：`tests/undo/GroupLayersTest.cpp`

**接口：**
- 依赖：Task 1 的 `NormalizeSubtreeContiguousOrder`（含 overrides 重载）、`LayerMoveSteps`
- 产出：无新公开接口；`GroupLayers.cpp` 内部改为委托 Task 1

- [ ] **Step 1: 写失败测试**

`tests/undo/GroupLayersTest.cpp` 补 spec 用例 12：Ungroup 后剩余层仍满足 `IsSubtreeContiguousOrder`。构造 group 内含 group 的两级结构，ungroup 外层后断言不变量与各层 index。

- [ ] **Step 2: 实现**

`MakeGroupLayersCommand`：
- 删掉 commit `6c7e395` 引入的 `topLevelSet` 后代枚举（`idSet` 恢复为仅选中层）
- 保留「只把选中层挪到一起」的 `desired` 构造
- 构造 `parentOverrides`：选中层 → 新 group id；新 group → `commonParent`
- `desired = NormalizeSubtreeContiguousOrder(desired, *composition, parentOverrides)`
- `SetParentCommand` 仍只对选中层下发（`selectedOrder` 恢复为仅选中层，不含后代）

`MakeUngroupLayersCommand`：
- 末尾追加 normalize 后的 move steps；`parentOverrides` 把被 ungroup 的孩子指向 `restoredParent`
- 注意 `RemoveLayerCommand` 会移除 group 本身，normalize 的 `desired` 不含被移除的 group id

删掉 `GroupLayers.cpp` 内的本地 `MoveSteps` 与 `ParentDepth`（改用 Task 1 的 `LayerMoveSteps`；`ParentDepth` 仅 `SortGroupsDeepFirst` 用，评估是否也搬去 `LayerOrder`——若只此一处使用可保留在原文件）。

- [ ] **Step 3: 验证**

`GroupLayersTest.*` 全绿（含既有 `KeepsExistingSubtreeContiguous`），随后全量 ctest。

---

### Task 3: MakeSetParentCommand + UndoManager assert

**Status:** ⬜ Not started

**文件：**
- 修改：`include/MotionStudio/undo/GroupLayers.h`
- 修改：`src/undo/GroupLayers.cpp`
- 修改：`src/undo/UndoManager.cpp`
- 测试：`tests/undo/GroupLayersTest.cpp`

**接口：**
- 依赖：Task 1、Task 2
- 产出：`MakeSetParentCommand(document, compositionId, layerId, newParentId)`

- [ ] **Step 1: 写失败测试**

spec 用例 13–14：
- 把层挂到已有 group 下，数组位置随之移动，`IsSubtreeContiguousOrder` 成立；undo 完全还原（顺序与 `parentId` 都回原样）
- 环（挂到自己的后代下）/ 目标层缺失 → 返回 nullptr

- [ ] **Step 2: 实现 MakeSetParentCommand**

组成 `CompositeCommand("Set Parent")`：`SetParentCommand` + normalize 后的 `MoveLayerCommand` steps。
- 先用 `Layer::canSetParent` 或等价的环检测前置判断（`setParent` 已拒环，工厂需提前返回 nullptr 而非产出半残命令）——实现时确认 `Layer` 是否已暴露只读的环检测；若无，在 `LayerOrder` 或本文件加文件内静态辅助函数
- `parentOverrides = { layerId: newParentId }`
- 裸 `SetParentCommand` 保留不动

- [ ] **Step 3: UndoManager debug assert**

`execute` / `undo` / `redo` 末尾，debug 构建下对文档内所有 composition 断言 `IsSubtreeContiguousOrder`。参照项目现有 `documentFingerprint` 式 debug 校验的接法（先查明现有宏 / 开关约定，勿自造新机制）。

⚠️ 此步可能让既有测试暴露出别处破坏不变量的路径。若发生，**停下来向用户汇报**该路径，不要静默放宽 assert。

- [ ] **Step 4: 验证**

全量 ctest（debug + ASan）全绿。

---

### Task 4: C ABI apply_layer_order + parent_depth

**Status:** ⬜ Not started

**文件：**
- 修改：`bridge/include/motionstudio_bridge.h`
- 修改：`bridge/src/common/motionstudio_bridge_commands.cpp`
- 测试：`bridge/tests/BridgeTest.cpp`

**接口：**
- 依赖：Task 1–3
- 产出：
  - `bool ms_command_apply_layer_order(MSDocument *, uint64_t compositionId, const uint64_t *layerIds, size_t count)`
  - `int ms_layer_parent_depth(MSDocument *, uint64_t layerId)`

- [ ] **Step 1: 写失败测试**

`bridge/tests/BridgeTest.cpp` 补 spec 用例 15–17：
- 传打乱顺序 → 落地为 normalize 后的顺序，一次 undo 全部还原
- 传当前顺序 → 返回 false，不产生 undo 条目
- `ms_layer_parent_depth`：根 = 0、两级嵌套 = 2、缺失 id = 0

- [ ] **Step 2: 实现**

`ms_command_apply_layer_order`：`DocumentLock` → normalize → `LayerMoveSteps` → 包一个 `CompositeCommand("Reorder Layers")` 经 `Execute` 下发；steps 为空返回 false。`layerIds == nullptr && count > 0` 直接返回 false（沿用 `ms_command_group_layers` 的入参校验风格）。

`ms_layer_parent_depth`：走 `EntityIndex`，缺失返回 0。

- [ ] **Step 3: 验证**

`ctest --test-dir build -R 'BridgeTest' --output-on-failure`，随后全量 ctest。

---

### Task 5: Swift 去重（涉及可见界面）

**Status:** ⬜ Not started

**⚠️ 本 Task 改动 Timeline 缩进与拖拽行为，属可见界面：做完停下来给用户确认，确认后才 commit。**

**文件：**
- 修改：`apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineReorder.swift`
- 修改：`apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- 修改：`apps/MotionStudioApp/MotionStudioApp/Timeline/Sidebar/TimelineSidebarView.swift`

**接口：**
- 依赖：Task 4 的两个 C ABI
- 产出：Swift 侧不再自行推导树结构

- [ ] **Step 1: MotionDocumentCore 转发**

`applyLayerOrder`（`MotionDocumentCore.swift:1855`）改为直接调 `ms_command_apply_layer_order`，删掉 Swift 侧的 `current != desired` 比较与 `TimelineReorder.moveSteps` 调用；返回 true 时 `changed()`。新增 `layerParentDepth(_ layerID: UInt64) -> Int` 转发 `ms_layer_parent_depth`。

- [ ] **Step 2: 删重复实现**

从 `TimelineReorder.swift` 删除：
- `TimelineReorder.moveSteps`（:64）
- `TimelineLayerTree.parentDepth`（:178）
- `TimelineLayerTree.movingIDsIncludingDescendants`（:206）
- 若 `hasSelectedAncestor`（:268）随之无引用则一并删（`groupingIDsStrippingNested` / `canGroup` 仍在用则保留）

**保留**：`arrangedLayerIDs` / `reorderedLayerIDs` / `uiInsertSlot` / `layerBlockFrames` —— UI 落点几何，不是树结构推导。

- [ ] **Step 3: 更新调用点**

`TimelineSidebarView.swift`：
- `:174` 缩进改用 `document.core.layerParentDepth(row.layerID)`
- `:382`（Arrange）、`:461`：去掉 `movingIDsIncludingDescendants` 包装，直接传选中层集合
- `:431`/`:439`（拖拽落点）：同上

删除因此变成孤儿的 `parentOf` 字典构造（仅 `canGroup` 仍需要则保留）。

- [ ] **Step 4: 验证**

优先 Xcode MCP 编译 `MotionStudioApp`（Mac Catalyst），不可用回退 `xcodebuild`。然后**手动验证**（见下）并把结果给用户看，等确认后再 commit。

---

## 验证

**Core / bridge（Task 1–4）**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure -LE benchmark
```

现有 841 个用例须全绿，新增用例全绿。

**端到端（Task 5）**

用 `DuoButton.motionproject`（`~/Library/Containers/com.taihe.MotionStudio/Data/Documents/Projects/`）复现原始 bug：

1. 打开工程，确认 Layer 树为 `Face`(`Label`, `FaceShape`) / `Base`
2. 选中 `Face` + `Base` → Group
3. **预期**：`Group`(`Face`(`Label`, `FaceShape`), `Base`)，无需任何手动拖动
4. ⌘Z → 完全回到步骤 1 的树形
5. 拖动 `Face` 到 `Base` 之下再拖回 → 每次子层随父层整块移动，缩进正确
6. 选中 `Group` → Ungroup → 回到步骤 1 的树形与位置

截图或录屏交用户确认后再 commit Task 5。
