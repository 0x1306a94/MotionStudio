# Fill-before-Stroke Paint Order Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shape/Text 绘制始终先 Fill 后 Stroke，修复 `[Stroke, Fill]`（尤其 Inside Stroke）被盖住的问题。

**Architecture:** 不改 `Layer::styles` schema。`ApplyLayerStyles` 与 Text 求值改为两遍扫描（Fill 块 → Stroke 块，同类保序）。`AddLayerStyleCommand` 添加 Fill 时插入到首个 Stroke 之前，使新文档磁盘顺序更直观；旧文档仅靠求值修正。

**Tech Stack:** C++17 core、GoogleTest、`SceneEvaluator` / `AddLayerStyleCommand`

**Spec:** `docs/superpowers/specs/2026-08-10-fill-before-stroke-paint-order-design.md`

## Global Constraints

- 不拆 `fills[]` / `strokes[]`，不改 PropertyPath `styles[i]` 语义（索引仍指磁盘数组）
- 不迁移已有 `document.json` 的 styles 顺序
- 不改 PAG export（Text 已 `strokeOverFill=true`；Shape Inside 为平行层）
- 开放路径 Inside 几何为空属既有限制，本次不处理
- 提交仅在用户明确要求时执行（plan 中的 Commit step 可跳过或攒到用户要求时）

---

## File Structure

| 文件 | 职责 |
|---|---|
| `src/render/SceneEvaluator.cpp` | `ApplyLayerStyles` 两遍；Text `styles` 两遍 |
| `src/undo/AddLayerStyleCommand.cpp` | Fill 插到首个 Stroke 前；Stroke 仍 append |
| `include/MotionStudio/undo/AddLayerStyleCommand.h` | 更新注释 |
| `tests/render/SceneEvaluatorTest.cpp` | `[Stroke,Fill]` → shapeItems / textItem 序 |
| `tests/undo/CommandsTest.cpp` | Add Fill 插在 Stroke 前 |
| `docs/data-model.md` / `docs/rendering.md` | 文档同步 |
| Spec 状态 | 草案 → 已批准/已实现 |

---

### Task 1: SceneEvaluator Fill→Stroke 求值顺序

**Status:** ✅ Done

**Files:**
- Modify: `src/render/SceneEvaluator.cpp`（`ApplyLayerStyles`；Text 分支收集 styles）
- Test: `tests/render/SceneEvaluatorTest.cpp`
- Docs: `docs/data-model.md`、`docs/rendering.md`；spec 状态

**Interfaces:**
- Consumes: `Layer::styles`、`LayerStyleType::{Fill,Stroke}`、`EvaluatedShapeItem` / `TextDrawStyle`
- Produces: `shapeItems` / `textItem.styles` 恒为 Fill 项在前、Stroke 项在后（同类保序）

- [x] **Step 1: 写失败测试（Shape）**

在 `tests/render/SceneEvaluatorTest.cpp` 追加：

```cpp
TEST(SceneEvaluatorTest, StrokeBeforeFillStillPaintsFillThenStroke) {
    RectScene scene;
    // RectScene already has one Fill at styles[0]. Prepend a Stroke so disk
    // order is [Stroke, Fill] — the pen-then-add-fill case.
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->position = motion::StrokePosition::Inside;
    stroke->width.setStaticValue(9.0f);
    stroke->color.setStaticValue(Color{1, 1, 1, 1});
    scene.layer->styles.insert(scene.layer->styles.begin(), std::move(stroke));
    ASSERT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Stroke);
    ASSERT_EQ(scene.layer->styles[1]->type(), motion::LayerStyleType::Fill);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);
    EXPECT_FALSE(result->layers[0].shapeItems[0].isStroke);
    EXPECT_TRUE(result->layers[0].shapeItems[1].isStroke);
    EXPECT_EQ(result->layers[0].shapeItems[1].stroke.position,
              motion::StrokePosition::Inside);
}

TEST(SceneEvaluatorTest, InterleavedFillsKeepRelativeOrderBeforeStrokes) {
    RectScene scene;
    // Disk: [Fill0, Stroke, Fill1] → paint Fill0, Fill1, Stroke
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->width.setStaticValue(2.0f);
    scene.layer->styles.push_back(std::move(stroke));
    auto fill1 = std::make_unique<FillStyle>();
    fill1->color.setStaticValue(Color{0, 1, 0, 1});
    scene.layer->styles.push_back(std::move(fill1));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 3u);
    EXPECT_FALSE(result->layers[0].shapeItems[0].isStroke);
    EXPECT_FLOAT_EQ(result->layers[0].shapeItems[0].paint.color.r, 1.0f);  // Fill0 red
    EXPECT_FALSE(result->layers[0].shapeItems[1].isStroke);
    EXPECT_FLOAT_EQ(result->layers[0].shapeItems[1].paint.color.g, 1.0f);  // Fill1 green
    EXPECT_TRUE(result->layers[0].shapeItems[2].isStroke);
}
```

- [x] **Step 2: 写失败测试（Text）**

同文件追加（需 `#include "MotionStudio/model/TextContent.h"`）：

```cpp
TEST(SceneEvaluatorTest, TextStrokeBeforeFillStillPaintsFillThenStroke) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->duration = 100;
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    layer->outPoint = 100;
    auto *text = static_cast<motion::TextContent *>(layer->content.get());
    text->text.setStaticValue("Hi");

    auto stroke = std::make_unique<StrokeStyle>();
    stroke->width.setStaticValue(2.0f);
    stroke->color.setStaticValue(Color{1, 1, 1, 1});
    layer->styles.push_back(std::move(stroke));
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0, 0, 0, 1});
    layer->styles.push_back(std::move(fill));

    Expected<SceneState, std::string> result =
        SceneEvaluator::Evaluate(document, composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result->layers[0].textItem.has_value());
    const auto &styles = result->layers[0].textItem->styles;
    ASSERT_EQ(styles.size(), 2u);
    EXPECT_FALSE(styles[0].isStroke);
    EXPECT_TRUE(styles[1].isStroke);
}
```

- [x] **Step 3: 跑测试确认失败**

```bash
cmake --build build -j8 --target core_tests
./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.StrokeBeforeFill*|SceneEvaluatorTest.InterleavedFills*|SceneEvaluatorTest.TextStrokeBeforeFill*'
```

Expected: `StrokeBeforeFillStillPaintsFillThenStroke` 与 `TextStrokeBeforeFillStillPaintsFillThenStroke` FAIL（shapeItems/text styles 仍为 Stroke 在前）。`InterleavedFills*` 在旧实现下也会 FAIL（当前序为 Fill0, Stroke, Fill1）。

- [x] **Step 4: 实现 `ApplyLayerStyles` 两遍扫描**

将 `ApplyLayerStyles` 中单遍 `for (const auto &style : layer.styles)` 改为两遍：先只处理 `LayerStyleType::Fill`，再只处理 `LayerStyleType::Stroke`。每遍内逻辑与现有 case 分支相同（可抽小 lambda，避免复制；保持简洁即可）。

- [x] **Step 5: 实现 Text 两遍收集**

`SceneEvaluator` Text 分支中收集 `textItem.styles` 的循环改为两遍（先 Fill 再 Stroke），跳过 Shader / `width<=0` 的规则不变；空 styles 时补默认黑 Fill 的逻辑不变。

- [x] **Step 6: 跑测试确认通过**

```bash
./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.*'
```

Expected: PASS（含既有 `StrokeItemCarriesPositionAndTrim` 等）

- [x] **Step 7: 更新文档与 spec 状态**

`docs/data-model.md` 新建 Text 层段（约 L271）将「按 `Layer.styles` 顺序全部参与绘制」改为：

> 绘制合成固定为先全部 Fill、再全部 Stroke（同类内部按 `styles[]` 出现顺序）；`styles[i]` 仍索引磁盘数组。

`docs/rendering.md` L52 注释改为：

> Fill/Stroke 求值为 Fill 块→Stroke 块（同类保序）；缺省黑 Fill。

Spec 头：`状态：草案` → `状态：实现中`（本 Task 完成后）。

- [x] **Step 8: Commit**

```bash
git add src/render/SceneEvaluator.cpp tests/render/SceneEvaluatorTest.cpp \
  docs/data-model.md docs/rendering.md \
  docs/superpowers/specs/2026-08-10-fill-before-stroke-paint-order-design.md \
  docs/superpowers/plans/2026-08-10-fill-before-stroke-paint-order.md
git commit -m "$(cat <<'EOF'
fix: paint fills before strokes regardless of styles[] order

Inside strokes were covered when Fill was appended after Stroke.
EOF
)"
```

同步 plan：本 Task 所有 checkbox → `[x]`，`**Status:** ✅ Done`。

---

### Task 2: AddLayerStyle 插入策略

**Status:** ✅ Done

**Files:**
- Modify: `src/undo/AddLayerStyleCommand.cpp`
- Modify: `include/MotionStudio/undo/AddLayerStyleCommand.h`（注释）
- Test: `tests/undo/CommandsTest.cpp`

**Interfaces:**
- Consumes: `LayerStyle::type()`、`layer->styles`
- Produces: Fill 插入到第一个 Stroke 之前；Stroke `push_back`；undo 仍按 `styleId_` 移除

- [x] **Step 1: 写失败测试**

在 `tests/undo/CommandsTest.cpp` 的 `AddLayerStyleCommandTest` 附近追加：

```cpp
TEST(AddLayerStyleCommandTest, AddFillInsertsBeforeFirstStroke) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<motion::StrokeStyle>());
    ASSERT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Stroke);

    scene.execute<AddLayerStyleCommand>(scene.layer->id, std::make_unique<FillStyle>());
    ASSERT_EQ(scene.layer->styles.size(), 2u);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Fill);
    EXPECT_EQ(scene.layer->styles[1]->type(), motion::LayerStyleType::Stroke);
    const EntityId fillId = scene.layer->styles[0]->id;

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Stroke);

    scene.undo.redo(scene.document);
    ASSERT_EQ(scene.layer->styles.size(), 2u);
    EXPECT_EQ(scene.layer->styles[0]->id, fillId);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Fill);
}

TEST(AddLayerStyleCommandTest, AddFillAppendsWhenNoStroke) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());
    scene.execute<AddLayerStyleCommand>(scene.layer->id, std::make_unique<FillStyle>());
    ASSERT_EQ(scene.layer->styles.size(), 2u);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Fill);
    EXPECT_EQ(scene.layer->styles[1]->type(), motion::LayerStyleType::Fill);
}
```

- [x] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='AddLayerStyleCommandTest.AddFillInsertsBeforeFirstStroke'
```

Expected: FAIL（Fill 被 append 到 Stroke 后）

- [x] **Step 3: 实现插入逻辑**

`AddLayerStyleCommand::execute`：

```cpp
void AddLayerStyleCommand::execute(Document &document) {
    if (!style_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (style_->type() == LayerStyleType::Fill) {
        size_t insertAt = layer->styles.size();
        for (size_t i = 0; i < layer->styles.size(); ++i) {
            if (layer->styles[i]->type() == LayerStyleType::Stroke) {
                insertAt = i;
                break;
            }
        }
        layer->styles.insert(layer->styles.begin() + static_cast<ptrdiff_t>(insertAt),
                             std::move(style_));
        return;
    }
    layer->styles.push_back(std::move(style_));
}
```

`undo` 保持按 `styleId_` 查找并 erase（已有）。

头文件注释改为说明：Fill 插到首个 Stroke 前，Stroke 追加。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/tests/core_tests --gtest_filter='AddLayerStyleCommandTest.*'
```

Expected: PASS

- [x] **Step 5: Commit**

```bash
git add src/undo/AddLayerStyleCommand.cpp include/MotionStudio/undo/AddLayerStyleCommand.h \
  tests/undo/CommandsTest.cpp \
  docs/superpowers/plans/2026-08-10-fill-before-stroke-paint-order.md
git commit -m "$(cat <<'EOF'
fix: insert new fills before strokes in styles[]

Keeps disk order closer to paint order for pen-then-fill workflows.
EOF
)"
```

同步 plan：本 Task checkbox → `[x]`，`**Status:** ✅ Done`；spec `状态：已实现`。

---

## Spec Coverage Check

| Spec 要求 | Task |
|---|---|
| Fill→Stroke 求值 | Task 1 |
| 同类保序 | Task 1（Interleaved 测试） |
| Text 同规则 | Task 1 |
| AddLayerStyle 插入 | Task 2 |
| 文档 | Task 1 Step 7 |
| 不改 schema / PAG | 全局约束，无代码任务 |
