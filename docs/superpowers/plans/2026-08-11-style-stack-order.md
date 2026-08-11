# Style Stack Order Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Inspector 倒序展示 Fills/Strokes（列表顶 = 绘制顶），并提供上/下移按钮在同类 style 内调序。

**Architecture:** Core `styles[]` 与 paint 序不变（小 index 先画）。新增 `MoveLayerStyleCommand`（仿 `MoveMaskCommand` 的 erase+insert），仅允许 Fill↔Fill / Stroke↔Stroke。Bridge/App 暴露 `moveStyle`；Inspector 对 indices `reversed()` 后渲染，按钮把视觉上移映射为更大 core index。

**Tech Stack:** C++17 Core、GoogleTest、bridge C ABI、SwiftUI App。

**Spec:** `docs/superpowers/specs/2026-08-11-style-stack-order-design.md`

## Global Constraints

- 分支：非 `master` 时直接在当前分支提交；若在 `master` 先按 `feature/{username}_style_stack_order` 建分支。
- **自动 commit：** 每完成一个 Task 必须提交；**提交前先**把本 plan 对应 checkbox 改为 `[x]`、更新 `**Status:**`，再把代码与本 plan 一并 commit。
- Commit 信息：英语、≤120 字符、句号结尾、句中无其他标点；侧重用户可感知变化。
- 每完成一个 Step 立刻勾选；未同步 plan 视为该步未完成。
- Core / bridge 宣称完成前优先 ASan：
  `cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON && cmake --build build`
- App / Xcode：优先 Xcode MCP `BuildProject`；不可用再 `xcodebuild`（见 `AGENTS.md`）。
- 编码规范：禁异常、禁 `dynamic_cast`、错误用 `Expected`、禁 lambda 优先显式函数（测试与文件既有风格除外）。
- **不做：** 拖拽排序、翻转 Core/序列化/PAG paint 序、Fill 画在 Stroke 之上、合并 Fill+Stroke 混排列表。

---

## 文件对照

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/undo/CommandKind.h` | 增加 `MoveLayerStyle` |
| `include/MotionStudio/undo/MoveLayerStyleCommand.h` | 命令声明 |
| `src/undo/MoveLayerStyleCommand.cpp` | 同类校验 + erase+insert；`mergeWith` |
| `tests/undo/CommandsTest.cpp` | 单测（同目录已有 Add/RemoveStyle、MoveMask） |
| `bridge/include/motionstudio_bridge.h` | `ms_command_move_layer_style` |
| `bridge/src/common/motionstudio_bridge_commands.cpp` | 桥接实现 |
| `bridge/tests/BridgeTest.cpp` | 冒烟：调序 + undo |
| `apps/.../Model/MotionDocumentCore.swift` | `moveStyle(layerID:from:to:)` |
| `apps/.../Inspector/FillsInspector.swift` | 倒序 + 上/下移 |
| `apps/.../Inspector/StrokesInspector.swift` | 同上 |
| `docs/data-model.md` | 补一句 Inspector 倒序与 MoveLayerStyle |
| `docs/superpowers/specs/2026-08-11-style-stack-order-design.md` | 状态改为已实现（最后 Task） |

`src/CMakeLists.txt` / `tests/CMakeLists.txt` 已 glob `undo/`，新 `.cpp` / 测试追加到现有 `CommandsTest.cpp` 即可，无需改 CMake。

---

### Task 1: `MoveLayerStyleCommand` + 单测

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/undo/MoveLayerStyleCommand.h`
- Create: `src/undo/MoveLayerStyleCommand.cpp`
- Modify: `include/MotionStudio/undo/CommandKind.h`
- Modify: `tests/undo/CommandsTest.cpp`

**Interfaces:**
- Consumes: `Document`、`Layer::styles`、`LayerStyle::type()`、`Command` / `CommandKind`（同 `MoveMaskCommand`）
- Produces:
```cpp
enum class CommandKind { /* ... */ MoveLayerStyle, /* ... */ };

class MoveLayerStyleCommand : public Command {
  public:
    MoveLayerStyleCommand(EntityId layerId, int fromIndex, int toIndex);
    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;
  private:
    EntityId layerId_ = {};
    int fromIndex_ = 0;
    int toIndex_ = 0;
};
```

校验（匿名命名空间辅助函数）：

```cpp
bool CanMoveLayerStyle(const Layer &layer, int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex) {
        return false;
    }
    const auto &styles = layer.styles;
    if (static_cast<size_t>(fromIndex) >= styles.size() ||
        static_cast<size_t>(toIndex) >= styles.size()) {
        return false;
    }
    const LayerStyleType type = styles[static_cast<size_t>(fromIndex)]->type();
    if (styles[static_cast<size_t>(toIndex)]->type() != type) {
        return false;
    }
    const int lo = std::min(fromIndex, toIndex);
    const int hi = std::max(fromIndex, toIndex);
    for (int i = lo; i <= hi; ++i) {
        if (styles[static_cast<size_t>(i)]->type() != type) {
            return false;
        }
    }
    return true;
}

void MoveLayerStyleInLayer(Layer &layer, int fromIndex, int toIndex) {
    if (!CanMoveLayerStyle(layer, fromIndex, toIndex)) {
        return;
    }
    std::unique_ptr<LayerStyle> style =
        std::move(layer.styles[static_cast<size_t>(fromIndex)]);
    layer.styles.erase(layer.styles.begin() + static_cast<ptrdiff_t>(fromIndex));
    layer.styles.insert(layer.styles.begin() + static_cast<ptrdiff_t>(toIndex),
                        std::move(style));
}
```

`execute` / `undo` / `mergeWith` 镜像 `MoveMaskCommand`（undo 交换 from/to；merge 要求 `other.fromIndex_ == toIndex_` 后吸收 `toIndex_`）。

- [x] **Step 1: 写失败测试**

在 `tests/undo/CommandsTest.cpp` 增加 include / `using`，并追加：

```cpp
TEST(MoveLayerStyleCommandTest, ReordersFillsWithinBlock) {
    Scene scene;
    auto a = std::make_unique<FillStyle>();
    auto b = std::make_unique<FillStyle>();
    auto c = std::make_unique<FillStyle>();
    const EntityId idA = a->id;
    const EntityId idB = b->id;
    const EntityId idC = c->id;
    scene.layer->styles.push_back(std::move(a));
    scene.layer->styles.push_back(std::move(b));
    scene.layer->styles.push_back(std::move(c));

    scene.execute<MoveLayerStyleCommand>(scene.layer->id, 0, 2);
    EXPECT_EQ(scene.layer->styles[0]->id, idB);
    EXPECT_EQ(scene.layer->styles[1]->id, idC);
    EXPECT_EQ(scene.layer->styles[2]->id, idA);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->styles[0]->id, idA);
    EXPECT_EQ(scene.layer->styles[1]->id, idB);
    EXPECT_EQ(scene.layer->styles[2]->id, idC);

    scene.undo.redo(scene.document);
    EXPECT_EQ(scene.layer->styles[2]->id, idA);
}

TEST(MoveLayerStyleCommandTest, CrossFillStrokeIsNoOp) {
    Scene scene;
    auto fill = std::make_unique<FillStyle>();
    auto stroke = std::make_unique<StrokeStyle>();
    const EntityId fillId = fill->id;
    const EntityId strokeId = stroke->id;
    scene.layer->styles.push_back(std::move(fill));
    scene.layer->styles.push_back(std::move(stroke));

    scene.execute<MoveLayerStyleCommand>(scene.layer->id, 0, 1);
    EXPECT_EQ(scene.layer->styles[0]->id, fillId);
    EXPECT_EQ(scene.layer->styles[1]->id, strokeId);
    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->styles[0]->id, fillId);
}

TEST(MoveLayerStyleCommandTest, ReordersStrokesPreservingFillPrefix) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());
    auto s0 = std::make_unique<StrokeStyle>();
    auto s1 = std::make_unique<StrokeStyle>();
    const EntityId stroke0 = s0->id;
    const EntityId stroke1 = s1->id;
    scene.layer->styles.push_back(std::move(s0));
    scene.layer->styles.push_back(std::move(s1));

    scene.execute<MoveLayerStyleCommand>(scene.layer->id, 1, 2);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Fill);
    EXPECT_EQ(scene.layer->styles[1]->id, stroke1);
    EXPECT_EQ(scene.layer->styles[2]->id, stroke0);
}

TEST(MoveLayerStyleCommandTest, ExecuteSkipsMissingLayer) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());
    scene.execute<MoveLayerStyleCommand>(EntityId{999}, 0, 0);
    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->styles.size(), 1u);
}

TEST(MoveLayerStyleCommandTest, MergesChainedMoves) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());
    scene.layer->styles.push_back(std::make_unique<FillStyle>());
    scene.layer->styles.push_back(std::make_unique<FillStyle>());
    const EntityId id0 = scene.layer->styles[0]->id;

    scene.execute<MoveLayerStyleCommand>(scene.layer->id, 0, 1);
    scene.execute<MoveLayerStyleCommand>(scene.layer->id, 1, 2);
    EXPECT_EQ(scene.layer->styles[2]->id, id0);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->styles[0]->id, id0);
}
```

（`StrokeStyle` 已由 `LayerStyle.h` 提供；若编译缺符号，加 `using motion::StrokeStyle;`。）

- [x] **Step 2: 跑测试确认失败**

Run: `cmake --build build && ./build/tests/core_tests --gtest_filter='MoveLayerStyleCommandTest.*'`  
Expected: 编译失败（缺 `MoveLayerStyleCommand`）

- [x] **Step 3: 最小实现**

1. `CommandKind.h` 在 `RemoveStyle` 后（或 `MoveMask` 旁）加 `MoveLayerStyle`
2. 按 Interfaces 写 `.h` / `.cpp`（校验 + `MoveLayerStyleInLayer`；`describe` 返回 `"Move Style"`）

- [x] **Step 4: 跑测试确认通过**

Run: 同上  
Expected: PASS（含 `MergesChainedMoves`：两次 execute 合并为一次 undo）

- [x] **Step 5: 勾选本 Task、更新 Status、commit**

```bash
git commit --only \
  include/MotionStudio/undo/CommandKind.h \
  include/MotionStudio/undo/MoveLayerStyleCommand.h \
  src/undo/MoveLayerStyleCommand.cpp \
  tests/undo/CommandsTest.cpp \
  docs/superpowers/plans/2026-08-11-style-stack-order.md \
  -m "Add MoveLayerStyleCommand for same-type style reorder."
```

---

### Task 2: Bridge + `MotionDocumentCore.moveStyle`

**Status:** Pending

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（紧挨 `ms_command_remove_style`）
- Modify: `bridge/src/common/motionstudio_bridge_commands.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（紧挨 `removeStyle`）

**Interfaces:**
- Consumes: `MoveLayerStyleCommand`（Task 1）
- Produces:
```c
// Moves a layer style within the same Fill or Stroke block. Cross-type is a no-op.
void ms_command_move_layer_style(MSDocument *document, uint64_t layerId,
                                 int fromIndex, int toIndex);
```
```swift
func moveStyle(layerID: UInt64, from fromIndex: Int, to toIndex: Int) {
    ms_command_move_layer_style(handle, layerID, Int32(fromIndex), Int32(toIndex))
    changed()
}
```

- [ ] **Step 1: 写失败 bridge 测试**

在 `BridgeTest.cpp` 的 style 生命周期测试附近追加（或扩展现有 `StyleLifecycle`）：

```cpp
TEST(BridgeCommandTest, MoveLayerStyleReorder) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    // rect 默认已有 1 fill；再加一个 fill → indices 0,1 均为 Fill
    ms_command_add_fill_style(document, layerId);
    ASSERT_EQ(ms_layer_style_count(document, layerId), 2);

    const MS_STYLE_TYPE type0Before = ms_layer_style_type_at(document, layerId, 0);
    ms_command_move_layer_style(document, layerId, 0, 1);
    EXPECT_EQ(ms_layer_style_type_at(document, layerId, 1), type0Before);

    EXPECT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_layer_style_type_at(document, layerId, 0), type0Before);

    ms_document_destroy(document);
}
```

（若默认 rect 只有 1 fill，`add_fill_style` 后 count==2；用 type 断言即可，不必比 id。）

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build build && ./build/tests/bridge_test --gtest_filter='BridgeCommandTest.MoveLayerStyleReorder'`  
Expected: 链接/编译失败（缺声明）

- [ ] **Step 3: 最小实现**

`motionstudio_bridge.h`：

```c
void ms_command_move_layer_style(MSDocument *document, uint64_t layerId,
                                 int fromIndex, int toIndex);
```

`motionstudio_bridge_commands.cpp`：`#include "MotionStudio/undo/MoveLayerStyleCommand.h"`，并：

```cpp
void ms_command_move_layer_style(MSDocument *document, uint64_t layerId, int fromIndex,
                                 int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveLayerStyleCommand>(
                          EntityId{layerId}, fromIndex, toIndex));
}
```

`MotionDocumentCore.swift` 在 `removeStyle` 旁加 `moveStyle`（见 Interfaces）。

- [ ] **Step 4: 跑测试确认通过**

Run: 同上 + `./build/tests/core_tests --gtest_filter='MoveLayerStyleCommandTest.*'`  
Expected: PASS

App 侧：Xcode MCP `BuildProject`（或回退 `xcodebuild`）确认 Swift 调用编译通过。

- [ ] **Step 5: 勾选本 Task、更新 Status、commit**

```bash
git commit --only \
  bridge/include/motionstudio_bridge.h \
  bridge/src/common/motionstudio_bridge_commands.cpp \
  bridge/tests/BridgeTest.cpp \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  docs/superpowers/plans/2026-08-11-style-stack-order.md \
  -m "Expose move layer style through bridge and app core."
```

---

### Task 3: Inspector 倒序 + 上/下移 + 文档

**Status:** Pending

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/FillsInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/StrokesInspector.swift`
- Modify: `docs/data-model.md`（约 L279 Shape/Text 绘制说明附近）
- Modify: `docs/superpowers/specs/2026-08-11-style-stack-order-design.md`（状态 → 已实现）

**Interfaces:**
- Consumes: `MotionDocumentCore.moveStyle`（Task 2）、既有 `fillIndices` / `strokeIndices`
- Produces: 视觉列表顶 = 最大同类 index；「Fill N」/「Stroke N」用视觉序（`position + 1`，在 `reversed` 后 `enumerated` 已满足）

- [ ] **Step 1: FillsInspector — 倒序列表**

将：

```swift
let fills = fillIndices()
```

改为：

```swift
let fills = fillIndices().reversed()
```

`ForEach(Array(fills.enumerated()), id: \.element)` 不变 → 顶行 `position == 0` 对应最大 core index，「Fill 1」即视觉顶。

- [ ] **Step 2: FillsInspector — 上/下移按钮与 helper**

在每行 `HStack`（blend picker 与 minus 之间）加入：

```swift
Button {
    moveFill(styleIndex: styleIndex, visuallyUp: true)
} label: {
    Image(systemName: "chevron.up")
}
.disabled(!isEditable || !canMoveFill(styleIndex: styleIndex, visuallyUp: true))
.help("Bring fill forward")

Button {
    moveFill(styleIndex: styleIndex, visuallyUp: false)
} label: {
    Image(systemName: "chevron.down")
}
.disabled(!isEditable || !canMoveFill(styleIndex: styleIndex, visuallyUp: false))
.help("Send fill backward")
```

Helpers（文件底部 private）：

```swift
private func canMoveFill(styleIndex: Int, visuallyUp: Bool) -> Bool {
    let ascending = fillIndices()
    guard let pos = ascending.firstIndex(of: styleIndex) else { return false }
    if visuallyUp {
        return pos + 1 < ascending.count
    }
    return pos > 0
}

private func moveFill(styleIndex: Int, visuallyUp: Bool) {
    guard isEditable else { return }
    let ascending = fillIndices()
    guard let pos = ascending.firstIndex(of: styleIndex) else { return }
    let neighborPos = visuallyUp ? pos + 1 : pos - 1
    guard ascending.indices.contains(neighborPos) else { return }
    let toIndex = ascending[neighborPos]
    perform("Move Fill") {
        core.moveStyle(layerID: layerID, from: styleIndex, to: toIndex)
    }
}
```

说明：相邻交换用 `from=styleIndex, to=neighborCoreIndex`（erase+insert 与 `MoveMask` 一致）；视觉上移 → 更大 core index。

- [ ] **Step 3: StrokesInspector — 同样改动**

- `let strokes = strokeIndices().reversed()`
- 复制上/下移按钮与 `canMoveStroke` / `moveStroke`（文案 `"Move Stroke"` / help Bring/Send stroke）

- [ ] **Step 4: 文档**

`docs/data-model.md` 在绘制序说明（约「先全部 Fill、再全部 Stroke」）后补一句：

> Inspector 的 Fills/Strokes 列表按同类 index **倒序**显示（最上 = 绘制最顶）；可用 `MoveLayerStyleCommand` 在同类连续块内调序，禁止跨 Fill/Stroke。

Spec 顶部状态改为：`已实现`。

- [ ] **Step 5: 构建 App + 手动冒烟**

Xcode MCP `BuildProject`（或 `xcodebuild`）。手动：两 Fill → 新在顶；上移/下移改变叠盖；Undo 还原。

- [ ] **Step 6: 勾选本 Task、更新 Status、commit**

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Inspector/FillsInspector.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/StrokesInspector.swift \
  docs/data-model.md \
  docs/superpowers/specs/2026-08-11-style-stack-order-design.md \
  docs/superpowers/plans/2026-08-11-style-stack-order.md \
  -m "Reverse fill and stroke lists and add reorder buttons."
```

---

## Spec coverage（自检）

| Spec 要求 | Task |
|---|---|
| UI 倒序展示 | Task 3 |
| 上/下移按钮（相对视觉） | Task 3 |
| Core 绘制序不变 | 无改动（显式非目标） |
| `MoveLayerStyleCommand` 同类校验 | Task 1 |
| Bridge `ms_command_move_layer_style` | Task 2 |
| App `moveStyle` | Task 2 |
| 视觉序标签 Fill N | Task 3（reversed + existing `position + 1`） |
| 单测 undo/跨类型 no-op | Task 1 |
| Bridge 冒烟 | Task 2 |
| data-model 补丁 | Task 3 |
