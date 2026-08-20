# SVG Mask Track Matte Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** SVG `mask=` 导入为同级 Shape track matte（均匀圆角仍走 `cornerRadius`）；PAG 导出时把带 path mask 或自身是 track matte 目标的 Group 包进 Precomp，裁切作用在合成结果上。

**Architecture:** 只改 `ApplyMask` 与 `applyGroupCornerRadiusClip` 的触发条件。不改 Core 模型 / schema / bridge。matte 源 `parentId = target.parentId`；`usedAsMatteOnly` 求值已有。Group → NullLayer 裁不到子层，isolation wrap 把已有 masks / trackMatte 挪到宿主 `PreComposeLayer`。

**Tech Stack:** C++17、GoogleTest、`svg_import`、tgfx `SVGDOM`、现有 `PagFileBuilder`。

**Spec:** [`docs/superpowers/specs/2026-08-20-svg-mask-track-matte-design.md`](../specs/2026-08-20-svg-mask-track-matte-design.md)

## Global Constraints

- 不改 Core 模型、schema、bridge、App。
- 禁止 `dynamic_cast` 与 C++ 异常；SVG 子类用 `tag()` + `static_cast`。
- 禁止 lambda；`if`/`switch` 分支必须 `{}`。
- Expected 用 `hasValue()` / `error()`。
- `clip-path` 映射不变（圆角 → `cornerRadius`，其余 → `Layer.masks`）。
- 复杂 mask（多子 / `use` / `g` / `objectBoundingBox`）仍 `mask.skipped`。
- 仅半径触发时报 `GroupCornerRadiusApproximated`；纯 mask / matte wrap 不新 warning。

## File Map

| 文件 | 职责 |
|---|---|
| `src/import/svg/SvgWalk.cpp` | `ApplyMask`：圆角仍 `cornerRadius`；否则建同级 Shape + track matte |
| `tests/import/svg/SvgImporterTest.cpp` | 导入断言 |
| `src/export/pag/PagFileBuilder.cpp` | Group isolation wrap 覆盖 path mask 与 track matte 目标 |
| `src/export/pag/PagFileBuilder.h` | 注释与现有 `applyGroupCornerRadiusClip` 对齐（可不改名） |
| `tests/export/pag/PagExporterTest.cpp` | Group mask / matte 目标导出结构 |

---

### Task 1: SVG `mask=` → track matte

**Status:** 待开始

**Files:**
- Modify: `src/import/svg/SvgWalk.cpp`
- Test: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: `CollectSingleShape`、`TryApplyUniformRoundedRectClip`、`ShapePathFromNode`、`PathToVectorNetwork`、`NetworkHasArea`、`ResolveStyle`、`ApplyPaintStyles`、`ApplyNodeTransform`、`tgfx::SVGMask::getMaskType()`
- Produces: 目标层 `trackMatteLayerId` / `trackMatteType`；matte Shape 在 `tree.layers` 里且 `parentId == target.parentId`；成功时不写 `target.masks`、不报 `mask.skipped`

- [ ] **Step 1: 写失败测试**

`tests/import/svg/SvgImporterTest.cpp` 增加 `#include "MotionStudio/model/TrackMatteType.h"`。

把 `MaskAttributeBecomesMask` 改成断言 track matte（默认 luminance → Luma）：

```cpp
TEST(SvgImporterTest, MaskAttributeBecomesMask) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><mask id=\"m\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#fff\"/></mask></defs>"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" fill=\"#000\" mask=\"url(#m)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *target = result.value().layers.back().get();
    EXPECT_TRUE(target->masks.empty());
    EXPECT_EQ(target->trackMatteType, motion::TrackMatteType::Luma);
    EXPECT_TRUE(target->trackMatteLayerId.isValid());
    EXPECT_FALSE(HasDiagnostic(result.value(), "mask.skipped"));
    const motion::Layer *matte = nullptr;
    for (const auto &layer : result.value().layers) {
        if (layer->id == target->trackMatteLayerId) {
            matte = layer.get();
        }
    }
    ASSERT_NE(matte, nullptr);
    EXPECT_EQ(matte->type(), motion::LayerType::Shape);
    EXPECT_EQ(matte->parentId, target->parentId);
    EXPECT_NE(matte->id, target->id);
}
```

`MaskOnGroupBecomesMask`：Group `masks.empty()`，`trackMatteType != None`，matte 同级。

`ComplexMaskAttributeIsSkipped`：额外 `EXPECT_EQ(layers.back()->trackMatteType, motion::TrackMatteType::None)`。

新增：

```cpp
TEST(SvgImporterTest, RoundedGroupMaskUsesCornerRadius) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
        "<defs><mask id=\"m\" maskUnits=\"userSpaceOnUse\">"
        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"8\" fill=\"#fff\"/>"
        "</mask></defs>"
        "<g mask=\"url(#m)\"><rect width=\"40\" height=\"40\" fill=\"#000\"/></g>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *group = nullptr;
    for (const auto &layer : result.value().layers) {
        if (layer->type() == motion::LayerType::Group && layer->name != "SVG") {
            group = layer.get();
        }
    }
    ASSERT_NE(group, nullptr);
    EXPECT_TRUE(group->masks.empty());
    EXPECT_EQ(group->trackMatteType, motion::TrackMatteType::None);
    auto *content = static_cast<motion::NullContent *>(group->content.get());
    EXPECT_NEAR(content->cornerRadius.staticValue(), 8.f, 1e-3f);
}

TEST(SvgImporterTest, MaskTypeAlphaAttribute) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><mask id=\"m\" mask-type=\"alpha\">"
        "<rect width=\"10\" height=\"10\" fill=\"#fff\"/>"
        "</mask></defs>"
        "<rect width=\"20\" height=\"20\" fill=\"#000\" mask=\"url(#m)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.back()->trackMatteType, motion::TrackMatteType::Alpha);
}

TEST(SvgImporterTest, MaskTypeAlphaStyle) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
        "<defs><mask id=\"m\" style=\"mask-type:alpha\">"
        "<rect width=\"10\" height=\"10\" fill=\"#fff\"/>"
        "</mask></defs>"
        "<rect width=\"20\" height=\"20\" fill=\"#000\" mask=\"url(#m)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.back()->trackMatteType, motion::TrackMatteType::Alpha);
}
```

`SimpleClipPathBecomesMask` 不改（仍 `masks.size()==1`）。

- [ ] **Step 2: 跑测试确认失败**

```
cmake --build build --target svg_import_tests
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.Mask*:SvgImporterTest.RoundedGroupMask*:SvgImporterTest.SimpleClipPath*'
```

Expected: `MaskAttributeBecomesMask` / `MaskOnGroupBecomesMask` FAIL（仍是 path mask）；新用例 FAIL 或未编译。

- [ ] **Step 3: 最小实现**

`src/import/svg/SvgWalk.cpp` 增加 `#include "MotionStudio/model/TrackMatteType.h"`。

在匿名 namespace 里，`ApplyMask` 成功路径改为：圆角仍 `TryApplyUniformRoundedRectClip`；否则建 Shape、设 track matte、`tree.layers.push_back`。不要对 matte 调 `ApplyNodeEffects`。`AppendShapeMask` 留给 `ApplyClipPath`。

```cpp
bool AppendMaskShapeLayer(const tgfx::SVGNode &shape, EntityId parentId, WalkContext &ctx,
                          EntityId *matteId) {
    SvgRectGeom geom = {};
    const bool uniformRect =
        ResolveSvgRectGeom(shape, ctx.lengthContext, &geom) && SvgRectHasUniformRadius(geom);
    VectorNetwork network = {};
    if (!uniformRect) {
        bool usedConic = false;
        network = PathToVectorNetwork(ShapePathFromNode(shape, ctx.lengthContext), &usedConic);
        if (!NetworkHasArea(network)) {
            return false;
        }
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = LayerName(shape);
    layer->parentId = parentId;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    Vec2 boundsMin = {};
    Vec2 boundsSize = {};
    if (uniformRect) {
        auto geometry = std::make_unique<ShapeRect>();
        geometry->position.setStaticValue(
            {geom.x + geom.width * 0.5f, geom.y + geom.height * 0.5f});
        geometry->size.setStaticValue({geom.width, geom.height});
        geometry->cornerRadius.setStaticValue(geom.rx);
        content->geometry = std::move(geometry);
        boundsMin = {geom.x, geom.y};
        boundsSize = {geom.width, geom.height};
    } else {
        auto geometry = std::make_unique<ShapePath>();
        geometry->path.setStaticValue(network);
        content->geometry = std::move(geometry);
        NetworkAabb(network, boundsMin, boundsSize);
    }
    const ComputedStyle style = ResolveStyle(shape, ComputedStyle{}, ctx.lengthContext);
    ApplyPaintStyles(*layer, style, ctx.mapper, boundsMin, boundsSize, &ctx.tree->diagnostics);
    ApplyNodeTransform(*layer, shape);
    *matteId = layer->id;
    ctx.tree->layers.push_back(std::move(layer));
    return true;
}

void ApplyMask(Layer &layer, const tgfx::SVGNode &node, WalkContext &ctx) {
    const auto &mask = node.getMask();
    if (!mask.isValue() || mask->type() != tgfx::SVGFuncIRI::Type::IRI) {
        return;
    }
    if (ctx.mapper == nullptr) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const auto it = ctx.mapper->find(LocalIriId(mask->iri()));
    if (it == ctx.mapper->end() || !it->second || it->second->tag() != tgfx::SVGTag::Mask) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const auto &svgMask = static_cast<const tgfx::SVGMask &>(*it->second);
    if (svgMask.getMaskContentUnits().type() !=
        tgfx::SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const tgfx::SVGNode *shape = CollectSingleShape(svgMask);
    if (shape == nullptr) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    if (TryApplyUniformRoundedRectClip(layer, *shape, ctx)) {
        return;
    }
    EntityId matteId{};
    if (!AppendMaskShapeLayer(*shape, layer.parentId, ctx, &matteId)) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const std::string maskName = LayerName(svgMask);
    if (!maskName.empty() && maskName != "Path") {
        ctx.tree->layers.back()->name = maskName;
    }
    layer.trackMatteLayerId = matteId;
    if (svgMask.getMaskType().type() == tgfx::SVGMaskType::Type::Alpha) {
        layer.trackMatteType = TrackMatteType::Alpha;
    } else {
        layer.trackMatteType = TrackMatteType::Luma;
    }
}
```

`DefaultName(SVGTag::Mask)` 若走到会返回 `"Path"`；有 id 的 mask（`m` / `mask0_129_143`）用 `LayerName(svgMask)`。

- [ ] **Step 4: 跑测试确认通过**

```
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。

- [ ] **Step 5: 勾选本 Task 并 commit**

```
git commit --only src/import/svg/SvgWalk.cpp tests/import/svg/SvgImporterTest.cpp docs/superpowers/plans/2026-08-20-svg-mask-track-matte.md -m "Import SVG masks as sibling track mattes."
```

---

### Task 2: PAG Group isolation wrap（path mask + track matte 目标）

**Status:** 待开始

**Files:**
- Modify: `src/export/pag/PagFileBuilder.cpp`（`applyGroupCornerRadiusClip`、`wrapGroupWithCornerClip`）
- Modify: `src/export/pag/PagFileBuilder.h`（注释：除圆角外，非空 masks / track matte 目标也 wrap）
- Test: `tests/export/pag/PagExporterTest.cpp`

**Interfaces:**
- Consumes: 现有 `wrapGroupWithCornerClip`、`collectDescendants`、`appendMasks`、`buildComposition` 里已排好的 track matte 邻接
- Produces: 满足 `cornerRadius>0 || !masks.empty() || trackMatteType != None` 的 Group 在 host 上变成 `PreComposeLayer`；已有 masks / trackMatte 在宿主上；仅 `radius>0` 时追加 AABB 圆角 mask 并 warning `GroupCornerRadiusApproximated`

- [ ] **Step 1: 写失败测试**

`tests/export/pag/PagExporterTest.cpp` 在 `GroupCornerRadiusApproximatedWarning` 后追加：

```cpp
TEST(PagExporterTest, GroupPathMaskWrappedInPrecomp) {
    Document document = MakeEmptyDoc(200, 200, 30);
    Composition *composition = Primary(document);
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    group->name = "MaskedGroup";
    group->inPoint = 0;
    group->outPoint = composition->duration;
    Mask mask;
    BezierPath path =
        MakeSingleContour({{Vec2{0, 0}, {}, {}}, {Vec2{40, 0}, {}, {}}, {Vec2{40, 40}, {}, {}}}, true);
    mask.path.setStaticValue(BezierPathToVectorNetwork(path));
    mask.mode = MaskMode::Add;
    group->masks.push_back(mask);
    Layer *child = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{80, 80});
    child->name = "Child";
    child->parentId = group->id;

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    for (const auto &warning : result.value().warnings) {
        EXPECT_NE(warning.code, "GroupCornerRadiusApproximated");
    }
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *host = nullptr;
    for (pag::Layer *layer : vector->layers) {
        if (layer->name == "MaskedGroup") {
            host = layer;
        }
        EXPECT_NE(layer->name, "Child");
    }
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(host->type(), pag::LayerType::PreCompose);
    EXPECT_FALSE(host->masks.empty());
    auto *precomp = static_cast<pag::PreComposeLayer *>(host);
    auto *inner = static_cast<pag::VectorComposition *>(precomp->composition);
    ASSERT_NE(inner, nullptr);
    bool foundChild = false;
    for (pag::Layer *innerLayer : inner->layers) {
        if (innerLayer->name == "Child") {
            foundChild = true;
        }
        EXPECT_TRUE(innerLayer->masks.empty());
    }
    EXPECT_TRUE(foundChild);
}

TEST(PagExporterTest, GroupTrackMatteTargetWrappedInPrecomp) {
    Document document = MakeEmptyDoc(200, 200, 30);
    Composition *composition = Primary(document);
    Layer *matte = AddShapeRect(document, composition, Vec2{0, 0}, Vec2{80, 80});
    matte->name = "Matte";
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    group->name = "TargetGroup";
    group->inPoint = 0;
    group->outPoint = composition->duration;
    group->trackMatteType = TrackMatteType::Alpha;
    group->trackMatteLayerId = matte->id;
    Layer *child = AddShapeRect(document, composition, Vec2{10, 10}, Vec2{80, 80});
    child->name = "Child";
    child->parentId = group->id;

    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue()) << static_cast<int>(result.error().kind);
    auto file = DecodeBytes(result.value().bytes);
    ASSERT_NE(file, nullptr);
    auto *vector = static_cast<pag::VectorComposition *>(file->compositions.back());
    pag::Layer *host = nullptr;
    pag::Layer *matteLayer = nullptr;
    for (pag::Layer *layer : vector->layers) {
        if (layer->name == "TargetGroup") {
            host = layer;
        }
        if (layer->name == "Matte") {
            matteLayer = layer;
        }
        EXPECT_NE(layer->name, "Child");
    }
    ASSERT_NE(host, nullptr);
    ASSERT_NE(matteLayer, nullptr);
    ASSERT_EQ(host->type(), pag::LayerType::PreCompose);
    EXPECT_EQ(host->trackMatteType, pag::TrackMatteType::Alpha);
    EXPECT_EQ(host->trackMatteLayer, matteLayer);
    EXPECT_FALSE(matteLayer->isActive);
    size_t hostIndex = vector->layers.size();
    size_t matteIndex = vector->layers.size();
    for (size_t i = 0; i < vector->layers.size(); ++i) {
        if (vector->layers[i] == host) {
            hostIndex = i;
        }
        if (vector->layers[i] == matteLayer) {
            matteIndex = i;
        }
    }
    EXPECT_EQ(matteIndex + 1, hostIndex);
}
```

`GroupCornerRadiusApproximatedWarning` 不改，必须继续 PASS。

- [ ] **Step 2: 跑测试确认失败**

```
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='PagExporterTest.GroupPathMask*:PagExporterTest.GroupTrackMatteTarget*:PagExporterTest.GroupCornerRadius*'
```

Expected: 两个新用例 FAIL（Group 仍是 NullLayer，Child 还在 host `layers` 里）。

- [ ] **Step 3: 最小实现**

`applyGroupCornerRadiusClip`：不要在半径 ≤ 0 时 `continue`。改为：

```cpp
const auto &content = static_cast<const NullContent &>(*layerPtr->content);
const float radius = std::max(content.cornerRadius.evaluate(layerPtr->inPoint), 0.0f);
const bool hasRadius = radius > 0.0f;
const bool hasMasks = !layerPtr->masks.empty();
const bool hasTrackMatte = layerPtr->trackMatteType != TrackMatteType::None;
if (!hasRadius && !hasMasks && !hasTrackMatte) {
    continue;
}
auto pagIt = layerByEntity_.find(layerPtr->id.value);
if (pagIt == layerByEntity_.end() || !ContainsPagLayer(current->layers, pagIt->second)) {
    continue;
}
Expected<SceneState, std::string> state =
    SceneEvaluator::Evaluate(document_, composition.id, layerPtr->inPoint);
Vec2 minPoint = {};
Vec2 maxPoint = {};
const bool hasAabb = state.hasValue() &&
    BoundsOfDescendantUnionLocal(*state, layerPtr->id, minPoint, maxPoint);
if (hasRadius && !hasAabb) {
    continue;
}
if (hasRadius) {
    Warn(&warnings_, layerPtr->id, "GroupCornerRadiusApproximated",
         "Group corner radius exported as precomp clip mask");
}
pag::VectorComposition *nested =
    wrapGroupWithCornerClip(current, *layerPtr, minPoint, maxPoint, hasRadius ? radius : 0.0f);
```

`wrapGroupWithCornerClip`：仅当 `radius > 0` 时追加 AABB 圆角 `MaskData`。`radius == 0` 时只把子树搬进 nested、把已有 `masks` / `trackMatte` 留在宿主（函数里已经 `wrap->masks = groupPag->masks` 且 `wrap->trackMatteLayer = groupPag->trackMatteLayer`）。

`PagFileBuilder.h` 给 `applyGroupCornerRadiusClip` 补一句：非空 path mask 或 Group 作为 track matte 目标时同样 wrap。

- [ ] **Step 4: 跑测试确认通过**

```
./build/tests/core_tests --gtest_filter='PagExporterTest.*'
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。

- [ ] **Step 5: 勾选本 Task 并 commit**

```
git commit --only src/export/pag/PagFileBuilder.cpp src/export/pag/PagFileBuilder.h tests/export/pag/PagExporterTest.cpp docs/superpowers/plans/2026-08-20-svg-mask-track-matte.md -m "Wrap isolated groups so PAG masks and track mattes clip children."
```
