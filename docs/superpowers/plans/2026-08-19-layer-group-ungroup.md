# Layer Group / Ungroup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **每完成一个 Step/Task 必须立刻把本文件对应勾选改为 `[x]` 并更新 Task Status，随 commit 提交（见 AGENTS.md「按 plan 实现」）。**

**Goal:** 选中图层可编组进 `LayerType::Group`、选中 Group 可解组；底部时间轴**左侧 Layer 树**按 `parentId` 缩进；一次操作一条 undo。

**Architecture:** Core 用 `SetParentCommand` + `MakeGroupLayersCommand` / `MakeUngroupLayersCommand` 组装 `CompositeCommand`。Bridge 薄封装。App 负责选中、菜单、快捷键，以及左侧 Layer 树缩进与拖 Group 时带走子孙。右侧轨道不缩进。

**Tech Stack:** C++17 core / GoogleTest、extern "C" bridge、UIKit 时间轴、Swift Testing。

**Spec:** `docs/superpowers/specs/2026-08-19-layer-group-ungroup-design.md`

## Global Constraints

- 编组：≥1 层、同一 composition、同一 `parentId`；去掉已被其他选中层包含的子孙。
- 新建 Group：单位 transform、名 `"Group"`、`inPoint`/`outPoint` = 子层并集；插在选中最前层之上并把选中层收成连续块。
- 解组：只拆选中的 `LayerType::Group`；当前帧 bake；只写子层无关键帧的 position/rotation/scale。
- 不做预合成编组/解组、折叠、drop-to-parent、子层关键帧空间重映射。
- 禁止 `dynamic_cast`、异常、lambda；`if`/`switch`/`while` 分支必须 `{}`。
- 提交：每任务结束 commit（不推送）；英语一句、句号结尾、无其它标点。
- 源码 glob 已覆盖 `src/undo` / `tests/undo`；新 cpp 不必改 CMake。Swift 新文件放 App 子目录即可。

## File Map

| 区域 | 文件 |
|---|---|
| SetParent | Create: `include/MotionStudio/undo/SetParentCommand.h`、`src/undo/SetParentCommand.cpp` |
| Group/Ungroup 组装 | Create: `include/MotionStudio/undo/GroupLayers.h`、`src/undo/GroupLayers.cpp` |
| CommandKind | Modify: `include/MotionStudio/undo/CommandKind.h` |
| Core 测试 | Modify: `tests/undo/CommandsTest.cpp`；Create: `tests/undo/GroupLayersTest.cpp` |
| Bridge | Modify: `bridge/include/motionstudio_bridge.h`、`bridge/src/common/motionstudio_bridge_commands.cpp`、`bridge/tests/BridgeTest.cpp` |
| Swift 树辅助 | Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineReorder.swift`、`TimelineSupport.swift`、`TimelineReorderTests.swift` |
| 缩进 UI | Modify: `TimelineSidebarView.swift` |
| 菜单/选中 | Modify: `AppDelegate.swift`、`EditorViewController.swift`、`EditorViewController+Commands.swift`、`MotionDocumentCore.swift` |

---

### Task 1: `SetParentCommand`

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/undo/SetParentCommand.h`
- Create: `src/undo/SetParentCommand.cpp`
- Modify: `include/MotionStudio/undo/CommandKind.h`（加 `SetParent`）
- Modify: `tests/undo/CommandsTest.cpp`

**Interfaces:**
- Consumes: `Layer::setParent`、`UndoManager::execute`
- Produces: `SetParentCommand(EntityId layerId, EntityId newParentId)`；`CommandKind::SetParent`；`describe()` = `"Set Parent"`

- [x] **Step 1: Write the failing tests**

在 `CommandsTest.cpp` 的 `Scene` fixture 后追加（`Scene` 已有一个 Shape 层）：

```cpp
TEST(SetParentCommandTest, SetsParentAndUndo) {
    Scene scene;
    Layer *parent =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
    scene.execute<SetParentCommand>(scene.layer->id, parent->id);
    EXPECT_EQ(scene.layer->parentId, parent->id);

    scene.undo.undo(scene.document);
    EXPECT_FALSE(scene.layer->parentId.isValid());

    scene.undo.redo(scene.document);
    EXPECT_EQ(scene.layer->parentId, parent->id);
}

TEST(SetParentCommandTest, RejectsCycleWithoutChangingParent) {
    Scene scene;
    Layer *parent =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
    ASSERT_TRUE(scene.layer->setParent(parent->id, scene.document));
    scene.execute<SetParentCommand>(parent->id, scene.layer->id);
    EXPECT_FALSE(parent->parentId.isValid());
}
```

补 `#include "MotionStudio/undo/SetParentCommand.h"` 和 `using motion::SetParentCommand`。

- [x] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target core_tests && ./build/tests/core_tests --gtest_filter='SetParentCommandTest.*'
```

Expected: 编译失败（缺类型）或链接失败。

- [x] **Step 3: Implement `SetParentCommand`**

`CommandKind.h` 在 `MoveLayer` 后加 `SetParent,`。

```cpp
// SetParentCommand.h
#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class SetParentCommand : public Command {
  public:
    // layerId: layer whose parent changes.
    // newParentId: new parent; invalid id clears the parent.
    SetParentCommand(EntityId layerId, EntityId newParentId);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    EntityId newParentId_ = {};
    std::optional<EntityId> oldParentId_ = {};
    bool applied_ = false;
};

}  // namespace motion
```

`execute`：`findLayer`；若 `!oldParentId_` 则记下当前 `parentId`；`setParent` 成功则 `applied_ = true`，失败保持原 parent 且不要把这次当成可 undo 的成功（`applied_` 仍 false，但 `oldParentId_` 已记——undo 时若 `!applied_` 直接 return）。`undo`：仅当 `applied_` 时 `setParent(*oldParentId_)`。

- [x] **Step 4: Run tests to verify they pass**

```bash
./build/tests/core_tests --gtest_filter='SetParentCommandTest.*'
```

Expected: PASS。

- [x] **Step 5: Update this plan** — Task 1 全部 `[x]`，`Status: ✅ Done`。

- [x] **Step 6: Commit**

```bash
git commit --only include/MotionStudio/undo/SetParentCommand.h src/undo/SetParentCommand.cpp include/MotionStudio/undo/CommandKind.h tests/undo/CommandsTest.cpp docs/superpowers/plans/2026-08-19-layer-group-ungroup.md -m "Add an undoable set parent command."
```

---

### Task 2: `MakeGroupLayersCommand`

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/undo/GroupLayers.h`
- Create: `src/undo/GroupLayers.cpp`
- Create: `tests/undo/GroupLayersTest.cpp`

**Interfaces:**
- Consumes: `AddLayerCommand`、`MoveLayerCommand`、`SetParentCommand`、`CompositeCommand`、`IndexOfLayer`
- Produces:

```cpp
std::unique_ptr<Command> MakeGroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, EntityId &outGroupId);
```

`outGroupId` 在失败时保持 invalid。成功时 command 的 `describe()` 为 `"Group"`。

- [x] **Step 1: Write the failing tests**

`tests/undo/GroupLayersTest.cpp`（自建 Document，不要依赖 CommandsTest 的匿名 `Scene`）：

```cpp
TEST(GroupLayersTest, GroupsSiblingsUnderIdentityGroup) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *b = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    a->transform.position.setStaticValue(Vec2{10, 0});
    b->transform.position.setStaticValue(Vec2{40, 0});
    const Mat3 worldA = a->worldTransform(0, document);
    const Mat3 worldB = b->worldTransform(0, document);

    EntityId groupId;
    std::unique_ptr<Command> command =
        MakeGroupLayersCommand(document, composition->id, {a->id, b->id}, groupId);
    ASSERT_NE(command, nullptr);
    undo.execute(document, std::move(command));

    Layer *group = document.entityIndex().findLayer(groupId);
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->type(), LayerType::Group);
    EXPECT_EQ(group->name, "Group");
    EXPECT_EQ(a->parentId, groupId);
    EXPECT_EQ(b->parentId, groupId);
    EXPECT_EQ(a->worldTransform(0, document), worldA);
    EXPECT_EQ(b->worldTransform(0, document), worldB);
    EXPECT_GT(IndexOfLayer(*composition, groupId), IndexOfLayer(*composition, a->id));
    EXPECT_GT(IndexOfLayer(*composition, groupId), IndexOfLayer(*composition, b->id));

    undo.undo(document);
    EXPECT_EQ(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_FALSE(a->parentId.isValid());
}

TEST(GroupLayersTest, SingleLayerAllowed) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    EntityId groupId;
    std::unique_ptr<Command> command =
        MakeGroupLayersCommand(document, composition->id, {a->id}, groupId);
    ASSERT_NE(command, nullptr);
    undo.execute(document, std::move(command));
    EXPECT_EQ(a->parentId, groupId);
}

TEST(GroupLayersTest, DifferentParentsIsNoop) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *g1 = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *g2 = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *b = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    ASSERT_TRUE(a->setParent(g1->id, document));
    ASSERT_TRUE(b->setParent(g2->id, document));
    EntityId groupId;
    EXPECT_EQ(MakeGroupLayersCommand(document, composition->id, {a->id, b->id}, groupId), nullptr);
}

TEST(GroupLayersTest, StripsDescendantsOfSelectedAncestors) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *child = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    ASSERT_TRUE(child->setParent(group->id, document));
    EntityId outerId;
    std::unique_ptr<Command> command =
        MakeGroupLayersCommand(document, composition->id, {group->id, child->id}, outerId);
    ASSERT_NE(command, nullptr);
    undo.execute(document, std::move(command));
    EXPECT_EQ(group->parentId, outerId);
    EXPECT_EQ(child->parentId, group->id);
}
```

`IndexOfLayer` 在 `src/undo/CommandHelpers.h`，测试可从 composition.layers 手写下标比较，或把 `IndexOfLayer` 测里复制一小段循环，**不要** include 私有 `CommandHelpers.h`。用公开 API：扫 `composition->layers`。

- [x] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target core_tests && ./build/tests/core_tests --gtest_filter='GroupLayersTest.*'
```

Expected: 编译失败。

- [x] **Step 3: Implement grouping**

`GroupLayers.h`：

```cpp
#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class Document;

// Builds a CompositeCommand that wraps sibling layers in a new Group.
// Returns nullptr when the selection cannot be grouped. On success writes the
// new group id to outGroupId.
std::unique_ptr<Command> MakeGroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, EntityId &outGroupId);

// Built in Task 3; declare it here so the header is complete.
std::unique_ptr<Command> MakeUngroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, FrameTime time);

}  // namespace motion
```

Task 2 里 `MakeUngroupLayersCommand` 先 `return nullptr;`。

`MakeGroupLayersCommand` 逻辑（禁止 lambda）：

1. `findComposition`；收集 `layerIds` 去重，丢掉找不到的层或 composition 不匹配的层。
2. 丢掉「`parentId` 链上另有选中 id」的层。
3. 剩余为空，或 `parentId` 不完全相同 → `nullptr`。
4. `current` = composition.layers 的 id 序。`topIndex` = 选中 id 在 `current` 里的最大下标。
5. `remaining` = `current` 去掉选中。`insertAt` = remaining 里原下标 `< topIndex` 的个数。`selectedOrder` = `current` 中选中 id 的相对序。
6. `auto group = std::make_unique<Layer>(LayerType::Group);` `name = "Group"`；`parentId = commonParent`（直接写字段，父已在文档中）；`inPoint`/`outPoint` = 选中层 min/max。`outGroupId = group->id`。
7. `desired` = remaining[0, insertAt) + selectedOrder + [groupId] + remaining[insertAt, end)。
8. `CompositeCommand("Group")`：`AddLayerCommand(compositionId, group, -1)` 追加；再对 `afterAdd = current + [groupId]` 与 `desired` 做与 Swift `moveSteps` 相同的逐步移动，每步 `MoveLayerCommand`；最后对每个 selected id `SetParentCommand(id, groupId)`。
9. 返回 composite。

C++ `moveSteps` 放 `GroupLayers.cpp` 匿名命名空间，抄 `TimelineReorder.moveSteps` 的命令式循环，不要用 lambda。

- [x] **Step 4: Run tests to verify they pass**

```bash
cmake --build build --target core_tests && ./build/tests/core_tests --gtest_filter='GroupLayersTest.*:SetParentCommandTest.*'
```

Expected: Group 相关 PASS；Ungroup 尚未实现，不要在本任务加 Ungroup 测试。

- [x] **Step 5: Update this plan** — Task 2 `[x]`，`Status: ✅ Done`。

- [x] **Step 6: Commit**

```bash
git commit --only include/MotionStudio/undo/GroupLayers.h src/undo/GroupLayers.cpp tests/undo/GroupLayersTest.cpp docs/superpowers/plans/2026-08-19-layer-group-ungroup.md -m "Group sibling layers under a new identity group."
```

---

### Task 3: `MakeUngroupLayersCommand`

**Status:** ✅ Done

**Files:**
- Modify: `src/undo/GroupLayers.cpp`
- Modify: `tests/undo/GroupLayersTest.cpp`

**Interfaces:**
- Consumes: `SetParentCommand`、`SetStaticValueCommand`、`RemoveLayerCommand`、`Layer::localTransform`
- Produces: 成功时 `"Ungroup"` composite；无选中 Group 时 `nullptr`

- [x] **Step 1: Write the failing tests**

```cpp
TEST(GroupLayersTest, UngroupIdentityRestoresParentAndWorld) {
    // group two shapes, then ungroup {groupId}
    // children parent cleared; world matrices unchanged; group gone
}

TEST(GroupLayersTest, UngroupBakesStaticTransform) {
    // group; SetStaticValueCommand group transform.position to {100,0}
    // ungroup at frame 0
    // child world x shifted by 100; child position is static
}

TEST(GroupLayersTest, UngroupSkipsKeyframedChildPosition) {
    // child position has a keyframe; group; move group; ungroup
    // child.keyframes() still 1 and value unchanged
}

TEST(GroupLayersTest, UngroupIgnoresNonGroupSelection) {
    EXPECT_EQ(MakeUngroupLayersCommand(document, composition->id, {shape->id}, 0), nullptr);
}
```

Bake 测试用 `undo.execute` 跑 `SetStaticValueCommand` 改 Group 的 `transform.position`，再 `MakeUngroupLayersCommand`。

- [x] **Step 2: Run test to verify it fails**

Expected: Ungroup 测试 FAIL（返回 nullptr 或未 bake）。

- [x] **Step 3: Implement ungroup**

选中里 `type() == Group` 的层。按 parent 链深度降序（先拆深的）。对每个 Group G：

- `Glocal = G.localTransform(time)`
- 直接子层（`parentId == G.id`）按模型序
- 若 `Glocal != Mat3::Identity()`：`composed = Glocal * child.localTransform(time)`；保留 `child.anchorPoint.evaluate(time)`；分解：
  - `rotation = atan2(composed.values[3], composed.values[0]) * 180 / pi`
  - `scale.x = hypot(values[0], values[3])`，`scale.y = hypot(values[1], values[4])`
  - `position = composed.transformPoint(anchor)`
  - `transform.position` / `rotation` / `scale` 仅当 `!isAnimated()` 时 `SetStaticValueCommand`
- `SetParentCommand(child, G.parentId)`
- `RemoveLayerCommand(compositionId, G.id)`

无 Group → `nullptr`。

- [x] **Step 4: Run tests**

```bash
cmake --build build --target core_tests && ./build/tests/core_tests --gtest_filter='GroupLayersTest.*:SetParentCommandTest.*'
```

Expected: PASS。

- [x] **Step 5: Update this plan** — Task 3 `[x]`，`Status: ✅ Done`。

- [x] **Step 6: Commit**

```bash
git commit --only src/undo/GroupLayers.cpp tests/undo/GroupLayersTest.cpp docs/superpowers/plans/2026-08-19-layer-group-ungroup.md -m "Ungroup layers and bake a static group transform."
```

---

### Task 4: Bridge API

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（在 `ms_command_remove_layer` 旁）
- Modify: `bridge/src/common/motionstudio_bridge_commands.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Consumes: `MakeGroupLayersCommand`、`MakeUngroupLayersCommand`、`Execute`、`DocumentLock`
- Produces:

```c
uint64_t ms_command_group_layers(MSDocument *document, uint64_t compositionId,
                                 const uint64_t *layerIds, size_t count);
bool ms_command_ungroup_layers(MSDocument *document, uint64_t compositionId,
                               const uint64_t *layerIds, size_t count,
                               int64_t frame);
```

- [x] **Step 1: Write the failing bridge tests**

用 `ms_command_add_rect_layer` 建两层，调 `ms_command_group_layers`，断言返回非 0、`ms_layer_parent_id` 指向 Group、`ms_layer_type` 为 `MS_LAYER_GROUP`。再 `ms_command_ungroup_layers` 为 true、parent 清零。`document==nullptr` 或 `layerIds==nullptr && count>0` 返回 0 / false。

- [x] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target bridge_test && ./build/bridge/bridge_test --gtest_filter='*GroupLayer*'
```

（测试名自定，filter 对齐。）Expected: 链接失败。

- [x] **Step 3: Implement**

`DocumentLock` 后 `std::vector<EntityId>` 从 C 数组填。`Make*` 为 nullptr 则不 `Execute`。Group 成功 `Execute` 后返回 `outGroupId.value`。

- [x] **Step 4: Run tests** — PASS。

- [x] **Step 5: Update this plan** — Task 4 `[x]`，`Status: ✅ Done`。

- [x] **Step 6: Commit**

```bash
git commit --only bridge/include/motionstudio_bridge.h bridge/src/common/motionstudio_bridge_commands.cpp bridge/tests/BridgeTest.cpp docs/superpowers/plans/2026-08-19-layer-group-ungroup.md -m "Expose group and ungroup layer commands on the bridge."
```

---

### Task 5: Swift 树辅助 + 测试

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineReorder.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift`
- Modify: `apps/MotionStudioApp/MotionStudioAppTests/TimelineReorderTests.swift`

**Interfaces:**
- Consumes: `parentId` 字典（`0` = 无父）
- Produces:

```swift
enum TimelineLayerTree {
    static let layerLeading: CGFloat = 8
    static let propertyLeading: CGFloat = 28
    static let indentPerDepth: CGFloat = 12

    nonisolated static func parentDepth(layerID: UInt64, parentOf: [UInt64: UInt64]) -> Int
    nonisolated static func leadingInset(depth: Int, isProperty: Bool) -> CGFloat
    nonisolated static func movingIDsIncludingDescendants(
        order: [UInt64], parentOf: [UInt64: UInt64], moving: Set<UInt64>
    ) -> Set<UInt64>
    nonisolated static func groupingIDsStrippingNested(
        ids: [UInt64], parentOf: [UInt64: UInt64]
    ) -> [UInt64]
    nonisolated static func canGroup(ids: [UInt64], parentOf: [UInt64: UInt64]) -> Bool
    nonisolated static func canUngroup(ids: [UInt64], types: [UInt64: MS_LAYER]) -> Bool
}
```

`canUngroup`：任一 id 的 type 为 `.GROUP`。`canGroup`：strip 后非空且这些 id 的 `parentOf[id] ?? 0` 全相同。

- [x] **Step 1: Write failing Swift tests**（`TimelineReorderTests.swift` 或新建 `TimelineLayerTreeTests.swift`）

- depth：`A←B←C` 则 C=2。
- inset：depth 1 图层 `20`，属性 `40`。
- 拖 Group 1（子 2,3）时 moving `{1}` → `{1,2,3}`；只拖 `{2}` 仍 `{2}`。
- strip：选 `{group, child}` → `{group}`。
- 不同父 `canGroup` false。

- [x] **Step 2: Run via Xcode MCP `RunSomeTests`**（scheme MotionStudioApp）确认失败。

- [x] **Step 3: Implement `TimelineLayerTree`**。深度循环要有 visiting set 防环。不要 lambda。

- [x] **Step 4: 测试 PASS。**

- [x] **Step 5: Update this plan** — Task 5 `[x]`，`Status: ✅ Done`。

- [x] **Step 6: Commit**

```bash
git commit --only apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineReorder.swift apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift apps/MotionStudioApp/MotionStudioAppTests/TimelineReorderTests.swift docs/superpowers/plans/2026-08-19-layer-group-ungroup.md -m "Indent the layer tree by parent depth and move group subtrees together."
```

若测试文件路径不同，按实际 `--only`。

---

### Task 6: 左侧 Layer 树缩进 UI + 拖拽/Arrange 扩子孙

**Status:** ✅ Done

**Files:**
- Modify: `TimelineSidebarView.swift`（`TimelineLayerCell` / `TimelinePropertyCell` leading constraint 改为可更新；`configure` 传入 depth）
- Modify: drag `itemsForBeginning` 与 `arrangeFromContextMenu` / `EditorViewController.canArrangeSelection` / `arrangeSelection`：`moving` 先经 `movingIDsIncludingDescendants`

**Interfaces:**
- Consumes: `core.layerParentID` 建 `parentOf`；`TimelineLayerTree.leadingInset`
- Produces: 左侧 Layer 树缩进可见；右侧轨道不缩进；拖 Group 子树整块移动

- [x] **Step 1: 给 cell 加 `NSLayoutConstraint` 成员**（layer stack leading、property name leading），`configure(..., depth: Int)` 里设 `constant`。

`configure(cell:)` 里：

```swift
var parentOf: [UInt64: UInt64] = [:]
for layerID in document.core.layerIDs(compositionID: document.core.firstCompositionID) {
    parentOf[layerID] = core.layerParentID(layerID)
}
let depth = TimelineLayerTree.parentDepth(layerID: row.layerID, parentOf: parentOf)
```

（可把 `parentOf` 提到 `reloadRows` 缓存，避免每行重建；v1 每行算也可以，层数不多。）

- [x] **Step 2: drag beginning**

```swift
var parentOf: [UInt64: UInt64] = [:]
for id in startOrder {
    parentOf[id] = document.core.layerParentID(id)
}
let moving = TimelineLayerTree.movingIDsIncludingDescendants(
    order: startOrder, parentOf: parentOf, moving: Set(editorState.selectedLayerIDs))
```

`EditorViewController+Commands.arrangeSelection` / `canArrangeSelection` 同样扩展 moving。

- [x] **Step 3: 无新单测则手动对照 spec：SVG 树或编组后子层缩进。** 本任务不写 UI 截图测试。

- [x] **Step 4: Update this plan** — Task 6 `[x]`，`Status: ✅ Done`。

- [x] **Step 5: Commit**

```bash
git commit --only apps/MotionStudioApp/MotionStudioApp/Timeline/Sidebar/TimelineSidebarView.swift apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift docs/superpowers/plans/2026-08-19-layer-group-ungroup.md -m "Indent the layer tree and drag a group with its children."
```

---

### Task 7: 菜单、快捷键、Core 门面

**Status:** ✅ Done

**Files:**
- Modify: `MotionDocumentCore.swift`（`groupLayers` / `ungroupLayers`）
- Modify: `AppDelegate.swift` Arrange 菜单加 Group / Ungroup
- Modify: `EditorViewController.swift` `keyCommands` + `canPerformAction`
- Modify: `EditorViewController+Commands.swift` `@objc groupSelectedLayers` / `ungroupSelectedLayers`
- Modify: `TimelineSidebarView.layerContextMenu`

**Interfaces:**
- Consumes: `ms_command_group_layers` / `ms_command_ungroup_layers`、`TimelineLayerTree.canGroup` / `canUngroup`、`playheadClock.frame`
- Produces: `⌘G` / `⇧⌘G`；成功后 Group 选中新 Group，Ungroup 选中放出的子层

- [x] **Step 1: Core 包装**

```swift
@discardableResult
func groupLayers(compositionID: UInt64, layerIDs: [UInt64]) -> UInt64 {
    var ids = layerIDs
    let newID = ids.withUnsafeBufferPointer { buffer in
        ms_command_group_layers(handle, compositionID, buffer.baseAddress, ids.count)
    }
    if newID != 0 {
        changed()
    }
    return newID
}

func ungroupLayers(compositionID: UInt64, layerIDs: [UInt64], frame: Int64) -> Bool {
    var ids = layerIDs
    let ok = ids.withUnsafeBufferPointer { buffer in
        ms_command_ungroup_layers(handle, compositionID, buffer.baseAddress, ids.count, frame)
    }
    if ok {
        changed()
    }
    return ok
}
```

`count==0` 时不要传悬挂指针：`count==0` 直接 return 0 / false。

- [x] **Step 2: Editor 动作**

`canGroupSelection`：`!isExportInProgress` 且 `TimelineLayerTree.canGroup`。  
`groupSelectedLayers`：`perform("Group") { let id = core.groupLayers(...); if id != 0 { editorState.selectLayer(id) } }`。

Ungroup：先在 Swift 记下选中 Group 的直接子 id（`layerParentID(child)==group`），`perform("Ungroup")` 成功后 `selectedLayerIDs = children`。

- [x] **Step 3: Arrange 菜单与右键** 加 Group / Ungroup；`canPerformAction` 接上。`keyCommands` 加 `"g"` command 与 command+shift。

- [x] **Step 4: Update spec 状态为已实现（已验收前可写「已实现」）并勾选本 plan。**

- [x] **Step 5: Commit**

```bash
git commit --only apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift apps/MotionStudioApp/MotionStudioApp/App/AppDelegate.swift apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift apps/MotionStudioApp/MotionStudioApp/Timeline/Sidebar/TimelineSidebarView.swift docs/superpowers/specs/2026-08-19-layer-group-ungroup-design.md docs/superpowers/plans/2026-08-19-layer-group-ungroup.md -m "Add Group and Ungroup commands to the arrange menu."
```

---

## 验收对照

1. 选两层 `⌘G`：左侧 Layer 树 Group 在上、子层缩进；画布不动；Undo 还原。
2. 选 Group `⇧⌘G`：子层回到原父；Group 被平移过则静态子层世界位置保留。
3. SVG 导入树自动缩进。
4. 拖 Group 整棵子树一起走；只拖子层不改 parent。
