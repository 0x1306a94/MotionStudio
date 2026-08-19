# Image / Group Corner Radius Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **每完成一个 Step/Task 必须立刻把本文件对应勾选改为 `[x]` 并更新 Task Status，随 commit 提交（见 AGENTS.md「按 plan 实现」）。**

**Goal:** Image 按容器圆角裁剪；Group 按子层 AABB 圆角裁剪，有圆角 / mask / track matte 时整组 `BeginLayer` 合成。

**Architecture:** `ImageContent` / `NullContent` 增加 `Animatable<float> cornerRadius`。求值写入 `EvaluatedImageItem` / `EvaluatedLayer`。Image 在 `DrawImage` 前 `ClipPath`。Group 由 `CommandBuilder` 按树包 isolation。删除 `InheritAncestorTrackMattes`。PAG：Image 圆角 mask；Group 用 Precomp 包一层。

**Tech Stack:** C++17 core / GoogleTest、tgfx `ClipPath`、SwiftUI Inspector、UIKit Timeline。

**Spec:** `docs/superpowers/specs/2026-08-19-image-group-corner-radius-design.md`

## Global Constraints

- 圆角：`Animatable<float>`，默认 0；求值 `≥0` 且不超过裁剪框短边一半。
- Group 裁剪框：`BoundsOfDescendantUnionLocal`；空 AABB 且只有圆角 → 不 isolation。
- Group isolation：`(radius>0 && 有 AABB) || !masks.empty() || trackMatte != None`。
- 无圆角/mask/matte 的 Group 不发 `BeginLayer`，子层直画。
- 不新建 `GroupContent`、不新开 undo 命令、不升 schema、不新开 Bridge API。
- 禁止 `dynamic_cast`、异常、lambda；`if`/`switch`/`while` 分支必须 `{}`。
- 提交：每任务结束 commit（不推送）；英语一句、句号结尾、无其它标点。
- 测试走现有文件 glob，不必改 CMake。

## File Map

| 区域 | 文件 |
|---|---|
| 模型 | Modify: `include/MotionStudio/model/ImageContent.h`、`include/MotionStudio/model/NullContent.h`、`include/MotionStudio/model/PropertyPath.h`、`src/model/PropertyPath.cpp` |
| 序列化 | Modify: `src/serialization/Serializer.cpp` |
| 求值 | Modify: `include/MotionStudio/render/EvaluatedImageItem.h`、`include/MotionStudio/render/EvaluatedLayer.h`、`src/render/SceneEvaluator.cpp`、`src/render/ShapeGeometry.cpp`、`include/MotionStudio/render/ShapeGeometry.h` |
| 绘制 | Modify: `src/render/CommandBuilder.cpp` |
| 点选 | Modify: `src/render/HitTest.cpp`、`include/MotionStudio/render/HitTest.h` |
| PAG | Modify: `src/export/pag/PagFileBuilder.cpp`、`src/export/pag/PagFileBuilder.h` |
| 测试 | Modify: `tests/model/PropertyPathTest.cpp`、`tests/serialization/SerializerTest.cpp`、`tests/render/SceneEvaluatorTest.cpp`、`tests/render/CommandBuilderTest.cpp`、`tests/render/HitTestTest.cpp`、`tests/export/pag/PagExporterTest.cpp` |
| App | Modify: `PropertyPath.swift`、`ImageLayerInspector.swift`、`InspectorView.swift`、`TimelineSupport.swift` |

---

### Task 1: 模型 + PropertyPath

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/model/ImageContent.h`
- Modify: `include/MotionStudio/model/NullContent.h`
- Modify: `include/MotionStudio/model/PropertyPath.h`（注释补路径）
- Modify: `src/model/PropertyPath.cpp`
- Test: `tests/model/PropertyPathTest.cpp`

**Interfaces:**
- Consumes: `ResolveAnimatable`、`Animatable<float>`
- Produces: `ImageContent::cornerRadius`；`NullContent::cornerRadius`；路径 `image.cornerRadius`、`content.cornerRadius`

- [x] **Step 1: Write the failing tests**

在 `PropertyPathTest.cpp` 的 `ResolvesImageSize` 后追加：

```cpp
TEST(ResolveAnimatableTest, ResolvesImageCornerRadius) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *imageLayer =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Image));
    auto *imageContent = static_cast<motion::ImageContent *>(imageLayer->content.get());

    AnimatableBase *resolved =
        ResolveAnimatable(document, {imageLayer->id, "image.cornerRadius"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase *>(&imageContent->cornerRadius));
    EXPECT_EQ(ResolveAnimatable(document, {imageLayer->id, "content.cornerRadius"}), nullptr);
}

TEST(ResolveAnimatableTest, ResolvesGroupCornerRadius) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *group =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    auto *nullContent = static_cast<motion::NullContent *>(group->content.get());

    AnimatableBase *resolved =
        ResolveAnimatable(document, {group->id, "content.cornerRadius"});
    EXPECT_EQ(resolved, static_cast<AnimatableBase *>(&nullContent->cornerRadius));
    EXPECT_EQ(ResolveAnimatable(document, {group->id, "image.cornerRadius"}), nullptr);
}
```

`#include "MotionStudio/model/NullContent.h"`。

- [x] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='ResolveAnimatableTest.ResolvesImageCornerRadius:ResolveAnimatableTest.ResolvesGroupCornerRadius'
```

Expected: 编译失败（无 `cornerRadius` 成员）或 `resolved == nullptr`。

- [x] **Step 3: Write minimal implementation**

`ImageContent.h` 在 `size` 后加：

```cpp
    // Container clip radius in layer-local pixels. 0 = square corners.
    Animatable<float> cornerRadius{0.0f};
```

`NullContent.h`：

```cpp
class NullContent : public LayerContent {
  public:
    NullContent();
    ~NullContent() override;

    // Group clip radius in layer-local pixels. Clip rect is descendant AABB.
    Animatable<float> cornerRadius{0.0f};
};
```

`PropertyPath.cpp`：`#include "MotionStudio/model/NullContent.h"`。`image` 分支在 `size` 后：

```cpp
            if (segments[1].name == "size") {
                return &imageContent->size;
            }
            if (segments[1].name == "cornerRadius") {
                return &imageContent->cornerRadius;
            }
```

`content` 分支开头（现有 Text 判断之前）：

```cpp
        if (first.name == "content") {
            if (layer->content->type() == LayerType::Group && segments.size() == 2 &&
                segments[1].name == "cornerRadius") {
                auto *nullContent = static_cast<NullContent *>(layer->content.get());
                return &nullContent->cornerRadius;
            }
            if (layer->content->type() != LayerType::Text) {
                return nullptr;
            }
```

`PropertyPath.h` 注释 Layer 行补 `"image.cornerRadius"` / `"content.cornerRadius"`。

- [x] **Step 4: Run tests to verify they pass**

```bash
./build/tests/core_tests --gtest_filter='ResolveAnimatableTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git add include/MotionStudio/model/ImageContent.h include/MotionStudio/model/NullContent.h include/MotionStudio/model/PropertyPath.h src/model/PropertyPath.cpp tests/model/PropertyPathTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md
git commit --only include/MotionStudio/model/ImageContent.h include/MotionStudio/model/NullContent.h include/MotionStudio/model/PropertyPath.h src/model/PropertyPath.cpp tests/model/PropertyPathTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Add animatable corner radius on image and group layers.

EOF
)"
```

---

### Task 2: 序列化

**Status:** ✅ Done

**Files:**
- Modify: `src/serialization/Serializer.cpp`
- Test: `tests/serialization/SerializerTest.cpp`

**Interfaces:**
- Consumes: `ImageContent::cornerRadius`、`NullContent::cornerRadius`、`AnimatableToJson` / `AnimatableFromJson`
- Produces: Image `content.cornerRadius`；Group `content.cornerRadius`；缺字段 = 0

- [x] **Step 1: Write the failing tests**

在 `SerializerTest.cpp` 追加（API 是 `Serializer::serialize` / `deserialize`）：

```cpp
TEST(SerializerTest, ImageAndGroupCornerRadiusRoundTrip) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->duration = 30;
    Layer *image = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Image));
    static_cast<ImageContent *>(image->content.get())->cornerRadius.setStaticValue(12.0f);
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    static_cast<NullContent *>(group->content.get())->cornerRadius.setStaticValue(8.0f);

    const std::string first = Serializer::serialize(document);
    Expected<std::unique_ptr<Document>, std::string> loaded = Serializer::deserialize(first);
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(first, Serializer::serialize(**loaded));

    Layer *loadedImage = (*loaded)->compositions[0]->layers[0].get();
    Layer *loadedGroup = (*loaded)->compositions[0]->layers[1].get();
    EXPECT_FLOAT_EQ(static_cast<ImageContent *>(loadedImage->content.get())->cornerRadius.staticValue(),
                    12.0f);
    EXPECT_FLOAT_EQ(static_cast<NullContent *>(loadedGroup->content.get())->cornerRadius.staticValue(),
                    8.0f);
}

TEST(SerializerTest, MissingCornerRadiusDefaultsToZero) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    auto json = nlohmann::json::parse(Serializer::serialize(document));
    json["compositions"][0]["layers"][0]["content"].erase("cornerRadius");
    Expected<std::unique_ptr<Document>, std::string> loaded =
        Serializer::deserialize(json.dump());
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    Layer *group = (*loaded)->compositions[0]->layers[0].get();
    EXPECT_FLOAT_EQ(static_cast<NullContent *>(group->content.get())->cornerRadius.staticValue(), 0.0f);
}
```

`#include "MotionStudio/model/NullContent.h"`。

- [x] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='SerializerTest.ImageAndGroupCornerRadiusRoundTrip:SerializerTest.MissingCornerRadiusDefaultsToZero'
```

Expected: round-trip 值仍为 0，或 FromJson 后不是 12/8。

- [x] **Step 3: Write minimal implementation**

`ContentToJson` Image 分支在 `scaleMode` 旁：

```cpp
            node["cornerRadius"] = AnimatableToJson(image.cornerRadius);
```

Group 分支：

```cpp
        case LayerType::Group: {
            const auto &group = static_cast<const NullContent &>(content);
            node["cornerRadius"] = AnimatableToJson(group.cornerRadius);
            break;
        }
```

Image FromJson，`scaleMode` 之后：

```cpp
            if (const json *cornerRadiusNode = FindChild(node, "cornerRadius")) {
                Expected<void, std::string> radiusResult =
                    AnimatableFromJson(*cornerRadiusNode, content->cornerRadius);
                if (!radiusResult) {
                    return Unexpected(radiusResult.error());
                }
            }
```

Group FromJson：

```cpp
        case LayerType::Group: {
            auto content = std::make_unique<NullContent>();
            if (const json *cornerRadiusNode = FindChild(node, "cornerRadius")) {
                Expected<void, std::string> radiusResult =
                    AnimatableFromJson(*cornerRadiusNode, content->cornerRadius);
                if (!radiusResult) {
                    return Unexpected(radiusResult.error());
                }
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
```

- [x] **Step 4: Run tests to verify they pass**

```bash
./build/tests/core_tests --gtest_filter='SerializerTest.*'
```

Expected: PASS。已有 kitchen-sink round-trip 仍绿。

- [x] **Step 5: Commit**

```bash
git commit --only src/serialization/Serializer.cpp tests/serialization/SerializerTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Serialize image and group corner radius with default zero.

EOF
)"
```

---

### Task 3: SceneEvaluator 写出 clamp 后的 radius

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/render/ShapeGeometry.h`、`src/render/ShapeGeometry.cpp`
- Modify: `include/MotionStudio/render/EvaluatedImageItem.h`、`include/MotionStudio/render/EvaluatedLayer.h`
- Modify: `src/render/SceneEvaluator.cpp`
- Test: `tests/render/SceneEvaluatorTest.cpp`

**Interfaces:**
- Consumes: `ImageContent::cornerRadius`、`NullContent::cornerRadius`
- Produces: `float ClampCornerRadius(float radius, Vec2 size)`；`EvaluatedImageItem::cornerRadius`；`EvaluatedLayer::cornerRadius`（已 clamp）

本任务**不**删除 `InheritAncestorTrackMattes`。

- [x] **Step 1: Write the failing tests**

```cpp
TEST(SceneEvaluatorTest, ImageCornerRadiusIsClamped) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->duration = 100;
    Layer *image =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Image));
    image->outPoint = 100;
    auto *content = static_cast<ImageContent *>(image->content.get());
    content->size.setStaticValue(Vec2{100, 40});
    content->cornerRadius.setStaticValue(100.0f);
    Expected<SceneState, std::string> result =
        SceneEvaluator::Evaluate(document, composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_FALSE(result->layers.empty());
    ASSERT_TRUE(result->layers[0].imageItem.has_value());
    EXPECT_FLOAT_EQ(result->layers[0].imageItem->cornerRadius, 20.0f);
    EXPECT_FLOAT_EQ(result->layers[0].cornerRadius, 20.0f);
}

TEST(SceneEvaluatorTest, GroupCornerRadiusEvaluates) {
    RectScene scene;
    Layer *group =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
    group->outPoint = 100;
    static_cast<NullContent *>(group->content.get())->cornerRadius.setStaticValue(6.0f);
    scene.layer->parentId = group->id;
    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const EvaluatedLayer *groupEval = nullptr;
    for (const EvaluatedLayer &layer : result->layers) {
        if (layer.id == group->id) {
            groupEval = &layer;
        }
    }
    ASSERT_NE(groupEval, nullptr);
    EXPECT_FLOAT_EQ(groupEval->cornerRadius, 6.0f);
}
```

`#include "MotionStudio/model/NullContent.h"`。`RectScene` 已有矩形子层，AABB 短边足够容纳 6。

- [x] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.ImageCornerRadiusIsClamped:SceneEvaluatorTest.GroupCornerRadiusEvaluates'
```

Expected: 编译失败或 radius 仍为 0。

- [x] **Step 3: Write minimal implementation**

`ShapeGeometry.h` 在 `MakeRectGeometry` 旁：

```cpp
// Clamps radius to [0, half of the shorter side]. Non-positive size → 0.
float ClampCornerRadius(float radius, Vec2 size);
```

`ShapeGeometry.cpp`：

```cpp
float ClampCornerRadius(float radius, Vec2 size) {
    const float half = std::min(std::max(size.x, 0.0f), std::max(size.y, 0.0f)) * 0.5f;
    return std::clamp(radius, 0.0f, half);
}
```

`EvaluatedImageItem` 加 `float cornerRadius = 0.0f;`。`EvaluatedLayer` 加 `float cornerRadius = 0.0f;`。

`SceneEvaluator.cpp` Image 分支，写出 `imageItem` 后：

```cpp
        const float radius = ClampCornerRadius(imageContent.cornerRadius.evaluatePreview(time),
                                               imageItem.containerSize);
        imageItem.cornerRadius = radius;
        evaluated.cornerRadius = radius;
        evaluated.imageItem = std::move(imageItem);
```

Group 分支 `FillCommonLayerFields` 后：

```cpp
        const auto &nullContent = static_cast<const NullContent &>(*layer.content);
        evaluated.cornerRadius = std::max(nullContent.cornerRadius.evaluatePreview(time), 0.0f);
```

Group 的短边一半 clamp 放到 CommandBuilder（那时才有 AABB）。求值阶段 Group 只保证 `≥0`。

`#include "MotionStudio/model/NullContent.h"`。

- [x] **Step 4: Run tests to verify they pass**

```bash
./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.*'
```

Expected: PASS。`GroupTargetTrackMatteInheritsToChildren` 本任务仍应通过。

- [x] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/render/ShapeGeometry.h src/render/ShapeGeometry.cpp include/MotionStudio/render/EvaluatedImageItem.h include/MotionStudio/render/EvaluatedLayer.h src/render/SceneEvaluator.cpp tests/render/SceneEvaluatorTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Evaluate clamped corner radius on image and group layers.

EOF
)"
```

---

### Task 4: Image `ClipPath` before `DrawImage`

**Status:** ✅ Done

**Files:**
- Modify: `src/render/CommandBuilder.cpp`
- Test: `tests/render/CommandBuilderTest.cpp`

**Interfaces:**
- Consumes: `EvaluatedImageItem::cornerRadius`、`MakeRectGeometry`、`ClampCornerRadius`
- Produces: `radius>0` 时 `ClipPath` 紧挨在 `DrawImage` 前；不因此多发 `BeginLayer`

- [x] **Step 1: Write the failing tests**

```cpp
TEST(CommandBuilderTest, ImageCornerRadiusEmitsClipPathBeforeDrawImage) {
    SceneState state;
    EvaluatedLayer layer;
    layer.opacity = 1.0f;
    EvaluatedImageItem image;
    image.absolutePath = "/tmp/project/assets/a.png";
    image.containerSize = {200, 100};
    image.intrinsicSize = {200, 100};
    image.cornerRadius = 12.0f;
    layer.imageItem = std::move(image);
    layer.cornerRadius = 12.0f;
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    int clipIndex = -1;
    int drawIndex = -1;
    int beginLayerCount = 0;
    for (size_t index = 0; index < commands.size(); ++index) {
        if (commands[index].type == DrawCommandType::ClipPath) {
            clipIndex = static_cast<int>(index);
        }
        if (commands[index].type == DrawCommandType::DrawImage) {
            drawIndex = static_cast<int>(index);
        }
        if (commands[index].type == DrawCommandType::BeginLayer) {
            ++beginLayerCount;
        }
    }
    EXPECT_GE(clipIndex, 0);
    EXPECT_GE(drawIndex, 0);
    EXPECT_LT(clipIndex, drawIndex);
    EXPECT_EQ(beginLayerCount, 0);
    EXPECT_EQ(commands[static_cast<size_t>(clipIndex)].geometry.kind, motion::ShapeGeometryKind::Rect);
    EXPECT_FLOAT_EQ(commands[static_cast<size_t>(clipIndex)].geometry.cornerRadius, 12.0f);
}

TEST(CommandBuilderTest, ImageLayerEmitsDrawImage) {
    // 现有用例保持 6 条命令、无 ClipPath。
}
```

现有 `ImageLayerEmitsDrawImage` 不要改断言条数以外的语义；radius 0 仍无 `ClipPath`。

- [x] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='CommandBuilderTest.ImageCornerRadiusEmitsClipPathBeforeDrawImage'
```

Expected: `clipIndex == -1`。

- [x] **Step 3: Write minimal implementation**

`AppendImageItem` 改为：

```cpp
void AppendImageItem(const std::optional<EvaluatedImageItem> &imageItem, DrawCommandList &commands) {
    if (!imageItem.has_value() || imageItem->absolutePath.empty()) {
        return;
    }
    const float radius = ClampCornerRadius(imageItem->cornerRadius, imageItem->containerSize);
    if (radius > 0.0f) {
        DrawCommand clip;
        clip.type = DrawCommandType::ClipPath;
        clip.geometry = MakeRectGeometry(
            Vec2{imageItem->containerSize.x * 0.5f, imageItem->containerSize.y * 0.5f},
            imageItem->containerSize, radius);
        clip.fillRule = FillRule::NonZero;
        commands.push_back(std::move(clip));
    }
    DrawCommand drawImage;
    drawImage.type = DrawCommandType::DrawImage;
    drawImage.imagePath = imageItem->absolutePath;
    drawImage.imageContainerSize = imageItem->containerSize;
    drawImage.imageIntrinsicSize = imageItem->intrinsicSize;
    drawImage.imageScaleMode = imageItem->scaleMode;
    commands.push_back(std::move(drawImage));
}
```

`#include` 已有 `ShapeGeometry.h`。加 `FillRule.h` 若尚未包含。

- [x] **Step 4: Run tests to verify they pass**

```bash
./build/tests/core_tests --gtest_filter='CommandBuilderTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git commit --only src/render/CommandBuilder.cpp tests/render/CommandBuilderTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Clip image layers to a rounded container rectangle.

EOF
)"
```

---

### Task 5: Group isolation + 去掉 matte 继承

**Status:** ✅ Done

**Files:**
- Modify: `src/render/CommandBuilder.cpp`
- Modify: `src/render/SceneEvaluator.cpp`（删除 `InheritAncestorTrackMattes` 及其调用）
- Test: `tests/render/CommandBuilderTest.cpp`、`tests/render/SceneEvaluatorTest.cpp`

**Interfaces:**
- Consumes: `EvaluatedLayer::cornerRadius`、`BoundsOfDescendantUnionLocal`、`HasAncestor`
- Produces: isolation Group 在自身 `layers[]` 位置发 `BeginLayer`；子孙相对变换；`ClipPath` 在子绘制前；子层不再继承 Group matte

- [x] **Step 1: Write the failing tests**

改写 `GroupTargetTrackMatteInheritsToChildren` 为不继承，并断言命令里 Group isolation 带 AlphaMatte、子层命令不再单独带 matte：

```cpp
TEST(SceneEvaluatorTest, GroupTargetTrackMatteStaysOnGroup) {
    // 同现有 fixture：group.trackMatte = matteLayer，rect 为子层。
    // EXPECT_EQ(groupEval->trackMatteType, Alpha);
    // EXPECT_EQ(childEval->trackMatteType, None);  // 不再继承
    // BuildCommands：BeginLayer 出现；AlphaMatte 出现在 Group 包裹内。
}
```

新增：

```cpp
TEST(CommandBuilderTest, GroupCornerRadiusIsolatesDescendants) {
    SceneState state;
    EvaluatedLayer child;
    child.id = EntityId{2};
    child.parentId = EntityId{1};
    child.opacity = 1.0f;
    child.worldTransform = Mat3::Translate(10, 20);
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry({50, 50}, {100, 100});
    item.paint = Paint{{1, 1, 1, 1}};
    child.shapeItems.push_back(item);

    EvaluatedLayer group;
    group.id = EntityId{1};
    group.opacity = 1.0f;
    group.cornerRadius = 8.0f;
    group.worldTransform = Mat3::Identity();

    state.layers.push_back(child);
    state.layers.push_back(group);

    auto commands = BuildCommands(state);
    int beginLayerCount = 0;
    int clipBeforeDraw = 0;
    int lastClip = -1;
    int lastDraw = -1;
    for (size_t index = 0; index < commands.size(); ++index) {
        if (commands[index].type == DrawCommandType::BeginLayer) {
            ++beginLayerCount;
        }
        if (commands[index].type == DrawCommandType::ClipPath) {
            lastClip = static_cast<int>(index);
        }
        if (commands[index].type == DrawCommandType::DrawPath) {
            lastDraw = static_cast<int>(index);
        }
    }
    EXPECT_EQ(beginLayerCount, 1);
    EXPECT_GE(lastClip, 0);
    EXPECT_GE(lastDraw, 0);
    EXPECT_LT(lastClip, lastDraw);
}

TEST(CommandBuilderTest, GroupWithoutRadiusDoesNotBeginLayer) {
    SceneState state;
    EvaluatedLayer child;
    child.id = EntityId{2};
    child.parentId = EntityId{1};
    child.opacity = 1.0f;
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry({50, 50}, {100, 100});
    item.paint = Paint{{1, 1, 1, 1}};
    child.shapeItems.push_back(item);
    EvaluatedLayer group;
    group.id = EntityId{1};
    group.opacity = 1.0f;
    state.layers.push_back(child);
    state.layers.push_back(group);
    auto commands = BuildCommands(state);
    for (const DrawCommand &command : commands) {
        EXPECT_NE(command.type, DrawCommandType::BeginLayer);
    }
}
```

需要 `#include "MotionStudio/render/HitTest.h"` 仅当测试直接调 AABB；CommandBuilder 内部会调 `BoundsOfDescendantUnionLocal`。子层局部 bounds 为 `[0,100]x[0,100]`，世界平移 (10,20)，Group 为单位矩阵 → AABB 非空。

- [x] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='CommandBuilderTest.GroupCornerRadiusIsolatesDescendants:CommandBuilderTest.GroupWithoutRadiusDoesNotBeginLayer:SceneEvaluatorTest.GroupTargetTrackMatteStaysOnGroup'
```

Expected: Group 被 `LayerHasDrawableContent` 跳过，无 `BeginLayer`；matte 测试若已改名则旧继承断言不再适用。

- [x] **Step 3: Write minimal implementation**

`SceneEvaluator.cpp`：删除 `InheritAncestorTrackMattes` 函数及其在 `EvaluatePreview` 里的调用。

`CommandBuilder.cpp` 增加（均在匿名 namespace，禁止 lambda）：

```cpp
bool IsIsolatingGroup(const EvaluatedLayer &layer, const SceneState &state) {
    if (LayerHasDrawableContent(layer)) {
        return false;
    }
    if (!layer.masks.empty() || layer.trackMatteType != TrackMatteType::None) {
        return true;
    }
    if (layer.cornerRadius <= 0.0f) {
        return false;
    }
    Vec2 minPoint;
    Vec2 maxPoint;
    return BoundsOfDescendantUnionLocal(state, layer.id, minPoint, maxPoint);
}

const EvaluatedLayer *IsolatingAncestor(const SceneState &state, EntityId layerId, EntityId stopAt) {
    const EvaluatedLayer *current = FindLayer(state, layerId);
    std::unordered_set<EntityId> visiting;
    while (current != nullptr && current->parentId.isValid()) {
        if (stopAt.isValid() && current->parentId == stopAt) {
            return nullptr;
        }
        if (!visiting.insert(current->parentId).second) {
            return nullptr;
        }
        const EvaluatedLayer *parent = FindLayer(state, current->parentId);
        if (parent == nullptr) {
            return nullptr;
        }
        if (IsIsolatingGroup(*parent, state)) {
            return parent;
        }
        current = parent;
    }
    return nullptr;
}

float OpacityRelativeToGroup(float childOpacity, float groupOpacity) {
    if (groupOpacity <= 1e-6f) {
        return 0.0f;
    }
    return childOpacity / groupOpacity;
}

void AppendRoundedClip(Vec2 minPoint, Vec2 maxPoint, float radius, DrawCommandList &commands) {
    const Vec2 size{maxPoint.x - minPoint.x, maxPoint.y - minPoint.y};
    const float clamped = ClampCornerRadius(radius, size);
    if (clamped <= 0.0f || size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }
    DrawCommand clip;
    clip.type = DrawCommandType::ClipPath;
    clip.geometry = MakeRectGeometry((minPoint + maxPoint) * 0.5f, size, clamped);
    clip.fillRule = FillRule::NonZero;
    commands.push_back(std::move(clip));
}
```

把现有单层绘制提成 `AppendLeafLayer(const SceneState &state, const EvaluatedLayer &layer, const Mat3 &parentWorld, float parentOpacity, DrawCommandList &commands)`：

- `relative = inverse(parentWorld) * layer.worldTransform`（`parentWorld` 为单位时就是 `layer.worldTransform`）
- `opacity = OpacityRelativeToGroup(layer.opacity, parentOpacity)`（`parentOpacity==1` 且外层循环时用 `layer.opacity`）
- 叶子 isolation（masks/effects/styles/自己的 track matte）逻辑保持现有 `needsIsolation`
- Image `ClipPath` 已在 `AppendImageItem`

再写 `AppendIsolatingGroup(...)`：

```
Save
Concat(group.world)
SetOpacity(OpacityRelativeToGroup(group.opacity, parentOpacity))
SetBlend(group.blendMode)
BeginLayer
  Vec2 min/max from BoundsOfDescendantUnionLocal
  AppendRoundedClip(min, max, group.cornerRadius)
  for layer in state.layers:
      if layer.id == group.id: continue
      if !HasAncestor(state, layer.id, group.id): continue
      if IsolatingAncestor(state, layer.id, group.id) != nullptr: continue
      if layer.usedAsMatteOnly: continue
      if IsIsolatingGroup(layer, state):
          AppendIsolatingGroup(state, layer, group.world, group.opacity, commands)
      else if LayerHasDrawableContent(layer):
          AppendLeafLayer(state, layer, group.world, group.opacity, commands)
  masks / track matte（现有 AppendPathMasks / AppendTrackMatte）
EndLayer（effects / layerStyles 若非空照带）
Restore
```

`BuildCommands` 外层循环：

```
for layer in state.layers:
  if layer.usedAsMatteOnly: continue
  if IsolatingAncestor(state, layer.id, {}): continue  // 由祖先 Group 发出
  if IsIsolatingGroup(layer, state):
      AppendIsolatingGroup(state, layer, Identity, 1.0f, commands)
  else if LayerHasDrawableContent(layer):
      AppendLeafLayer(state, layer, Identity, 1.0f, commands)
```

`#include "MotionStudio/render/HitTest.h"`。`HasAncestor` 已在 cpp 内。

递归 `AppendIsolatingGroup` 必须是具名函数，不要 lambda。

- [x] **Step 4: Run tests to verify they pass**

```bash
./build/tests/core_tests --gtest_filter='CommandBuilderTest.*:SceneEvaluatorTest.*'
```

Expected: PASS。无 isolation 的 Group 仍不出现在命令里；子层仍直画。

- [x] **Step 5: Commit**

```bash
git commit --only src/render/CommandBuilder.cpp src/render/SceneEvaluator.cpp tests/render/CommandBuilderTest.cpp tests/render/SceneEvaluatorTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Isolate group layers for corner radius mask and track matte.

EOF
)"
```

---

### Task 6: 点选尊重圆角

**Status:** 未开始

**Files:**
- Modify: `src/render/HitTest.cpp`、`include/MotionStudio/render/HitTest.h`（若要导出辅助函数）
- Test: `tests/render/HitTestTest.cpp`

**Interfaces:**
- Consumes: `EvaluatedImageItem::cornerRadius`、`EvaluatedLayer::cornerRadius`、`BoundsOfDescendantUnionLocal`、`MakeRectGeometry` / `ShapeGeometryToBezierPath`
- Produces: Image 圆角外不命中；有圆角 isolation 祖先时点还要在祖先 AABB 圆角内

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(HitTestTest, ImageCornerRadiusRejectsOutsideRound) {
    EvaluatedLayer layer;
    layer.opacity = 1.0f;
    EvaluatedImageItem image;
    image.containerSize = {100, 100};
    image.cornerRadius = 50.0f;
    layer.imageItem = image;
    EXPECT_TRUE(HitTestLayer(layer, {50, 50}, 0));
    EXPECT_FALSE(HitTestLayer(layer, {1, 1}, 0));
}

TEST(HitTestTest, GroupCornerRadiusClipsChildHit) {
    SceneState state;
    EvaluatedLayer child;
    child.id = EntityId{2};
    child.parentId = EntityId{1};
    child.opacity = 1.0f;
    child.worldTransform = Mat3::Identity();
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry({50, 50}, {100, 100});
    item.paint = Paint{{1, 1, 1, 1}};
    child.shapeItems.push_back(item);
    EvaluatedLayer group;
    group.id = EntityId{1};
    group.opacity = 1.0f;
    group.cornerRadius = 50.0f;
    group.worldTransform = Mat3::Identity();
    state.layers.push_back(child);
    state.layers.push_back(group);
    EXPECT_EQ(HitTestLayerAtPoint(state, {50, 50}, 0).value, 2);
    EXPECT_EQ(HitTestLayerAtPoint(state, {1, 1}, 0).value, 0);
}
```

`#include "MotionStudio/render/EvaluatedImageItem.h"`。

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='HitTestTest.ImageCornerRadiusRejectsOutsideRound:HitTestTest.GroupCornerRadiusClipsChildHit'
```

Expected: 角上 (1,1) 仍命中。

- [ ] **Step 3: Write minimal implementation**

`HitTestShapeItem` / `FlattenPathInWorld` 已在 `HitTest.cpp` 匿名 namespace，同文件直接调用：

```cpp
bool PointInRoundedRect(Vec2 local, Vec2 minPoint, Vec2 maxPoint, float radius, float pad) {
    const Vec2 size{maxPoint.x - minPoint.x, maxPoint.y - minPoint.y};
    const float clamped = ClampCornerRadius(radius, size);
    const Vec2 center{(minPoint.x + maxPoint.x) * 0.5f, (minPoint.y + maxPoint.y) * 0.5f};
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry(center, size, clamped);
    item.paint = Paint{{1, 1, 1, 1}};
    const BezierPath path = ShapeGeometryToBezierPath(item.geometry);
    const std::vector<Vec2> points = FlattenPathInWorld(path, Mat3::Identity());
    return HitTestShapeItem(item, points, local, pad);
}
```

`HitTestLayer` Image 分支：radius>0 时用 `PointInRoundedRect(local, {0,0}, container, radius, pad)`，否则保留现有 AABB。

`HitTestLayerAtPoint`：命中某层后，沿 `parentId` 走；若祖先 `cornerRadius>0` 且 `BoundsOfDescendantUnionLocal` 成功，把 scene 点变到祖先局部，再 `PointInRoundedRect`；失败则视为未命中，继续往下找。只检查 `cornerRadius>0` 的祖先（与 isolation 圆角裁剪一致）。mask/matte isolation 不改点选。

- [ ] **Step 4: Run tests to verify they pass**

```bash
./build/tests/core_tests --gtest_filter='HitTestTest.*'
```

Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git commit --only src/render/HitTest.cpp include/MotionStudio/render/HitTest.h tests/render/HitTestTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Hit-test image and group content against rounded clips.

EOF
)"
```

---

### Task 7: PAG 导出

**Status:** 未开始

**Files:**
- Modify: `src/export/pag/PagFileBuilder.cpp`、`src/export/pag/PagFileBuilder.h`（若要加成员）
- Test: `tests/export/pag/PagExporterTest.cpp`

**Interfaces:**
- Consumes: `ImageContent::cornerRadius`、`NullContent::cornerRadius`、`applyImageContainerFit`、`wrapCompositionWithCornerClip` 同款 mask
- Produces: Image 圆角 mask；动画半径 warning `ImageCornerRadiusAnimationBaked`；Group warning `GroupCornerRadiusApproximated` + Precomp 圆角 mask

- [ ] **Step 1: Write the failing tests**

复用 `PagExporterTest.ImageLayerExports` 的 1x1 PNG fixture。追加：

```cpp
TEST(PagExporterTest, ImageCornerRadiusAddsMask) {
    // content->cornerRadius = 12；Export 后 ImageLayer->masks 非空。
}

TEST(PagExporterTest, ImageCornerRadiusAnimationBakedWarning) {
    // cornerRadius 两个关键帧；warnings 含 ImageCornerRadiusAnimationBaked。
}

TEST(PagExporterTest, GroupCornerRadiusApproximatedWarning) {
    Document document = MakeEmptyDoc(200, 200, 30);
    Composition *composition = Primary(document);
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    static_cast<NullContent *>(group->content.get())->cornerRadius.setStaticValue(10.0f);
    AddShapeRect(document, composition, Vec2{0, 0}, Vec2{80, 80});
    // 把矩形 parent 设为 group
    auto result = PagExporter::Export(document, {});
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &warning : result.value().warnings) {
        if (warning.code == "GroupCornerRadiusApproximated") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
```

`AddShapeRect` / `MakeEmptyDoc` 以该测试文件已有 helper 为准。

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target pag_export_tests
./build/tests/pag_export_tests --gtest_filter='PagExporterTest.ImageCornerRadiusAddsMask:PagExporterTest.ImageCornerRadiusAnimationBakedWarning:PagExporterTest.GroupCornerRadiusApproximatedWarning'
```

二进制名若不同，用 `ctest --test-dir build -R 'ImageCornerRadiusAddsMask' --output-on-failure`。Expected: 无 mask / 无 warning。

- [ ] **Step 3: Write minimal implementation**

**Image：** `applyImageContainerFit` 在现有 overflow clip 处改用半径；即使没有 overflow，`radius>0` 也要加圆角 mask。半径取 `content.cornerRadius.evaluate(layer.inPoint)`，`ClampCornerRadius(..., container)`。若 `content.cornerRadius.isAnimated()`，`Warn(..., "ImageCornerRadiusAnimationBaked", "Animated image corner radius baked using in-point value")`。

overflow clip 现为 `MakeRectGeometry(..., 0)`。改为同一 rect + radius，避免叠两个 mask：有 overflow 或 radius>0 时发**一条**圆角（或直角）容器 mask。

**Group：** PAG `NullLayer` 裁不到子层。在 composition 层列表建完后（`buildLayers` 已把 Group 变成 NullLayer、子层 `parent` 已挂上）做后处理：

```cpp
void PagFileBuilder::applyGroupCornerRadiusClip(pag::VectorComposition *host,
                                                const Composition &composition) {
    for (const auto &layerPtr : composition.layers) {
        if (layerPtr->type() != LayerType::Group) {
            continue;
        }
        const auto &content = static_cast<const NullContent &>(*layerPtr->content);
        if (!content.cornerRadius.isAnimated() && content.cornerRadius.staticValue() <= 0.0f) {
            continue;
        }
        const float radius = std::max(content.cornerRadius.evaluate(layerPtr->inPoint), 0.0f);
        if (radius <= 0.0f) {
            continue;
        }
        Expected<SceneState, std::string> state =
            SceneEvaluator::Evaluate(document_, composition.id, layerPtr->inPoint);
        Vec2 minPoint;
        Vec2 maxPoint;
        if (!state.hasValue() ||
            !BoundsOfDescendantUnionLocal(*state, layerPtr->id, minPoint, maxPoint)) {
            continue;
        }
        Warn(&warnings_, layerPtr->id, "GroupCornerRadiusApproximated",
             "Group corner radius exported as precomp clip mask");
        // 找到 host 里对应 NullLayer，把它与 parent==该层的子孙挪进 nested VectorComposition，
        // 用 PreComposeLayer + rounded rect mask 替换原 NullLayer 位置。
        // mask 几何：MakeRectGeometry((min+max)/2, max-min, ClampCornerRadius(radius, size))
        // 变换：Precomp 用 Group 的 transform；子孙在 nested 里相对 Group。
    }
}
```

相对变换：nested 内 NullLayer 为单位 transform，子孙保持相对 parent 的 PAG parent 指针。更简单且与 composition clip 一致的做法：nested 放入原 NullLayer + 其子树（保持 PAG parent），PreComposeLayer 放在 host 中 NullLayer 的位置，mask 在 Precomp 本地（AABB 已是 Group 局部）。NullLayer 的 transform 挪到 PreComposeLayer 上，nested 内 Group Null 改单位矩阵，避免双重 transform。

动画半径：同样 bake in-point；可与 `GroupCornerRadiusApproximated` 共用一条 warning，不必另加 code。

`#include "MotionStudio/render/SceneEvaluator.h"`、`HitTest.h`、`NullContent.h`。

- [ ] **Step 4: Run tests to verify they pass**

```bash
ctest --test-dir build -R 'PagExporterTest' --output-on-failure
```

Expected: 新用例 PASS；`ImageLayerExports` / `CompositionCornerRadiusAddsBackdrop` 仍绿。

- [ ] **Step 5: Commit**

```bash
git commit --only src/export/pag/PagFileBuilder.cpp src/export/pag/PagFileBuilder.h tests/export/pag/PagExporterTest.cpp docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Export image and group corner radius as PAG clip masks.

EOF
)"
```

---

### Task 8: Inspector + 时间轴

**Status:** 未开始

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Bridge/PropertyPath.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/ImageLayerInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift`

**Interfaces:**
- Consumes: `image.cornerRadius`、`content.cornerRadius`、`NumberPropertyRow`、`ms_command_set_static_float` / 关键帧
- Produces: Image Height 下 Radius；Group 在 Transform 前 Radius；时间轴有关键帧才出 `Corner Radius` 轨

无现成 Inspector 单测，本任务不新开 Swift 测试。

- [ ] **Step 1: Add property paths and Image Radius row**

`PropertyPath.swift`：

```swift
enum ImageProperty: String, CaseIterable {
    case size = "image.size"
    case cornerRadius = "image.cornerRadius"

    var path: String { rawValue }

    var actionLabel: String {
        switch self {
        case .size: "Size"
        case .cornerRadius: "Corner Radius"
        }
    }
}

enum GroupProperty: String, CaseIterable {
    case cornerRadius = "content.cornerRadius"

    var path: String { rawValue }

    var actionLabel: String {
        switch self {
        case .cornerRadius: "Corner Radius"
        }
    }
}
```

`ImageLayerInspector.swift` 在 Height 行后、Reset 按钮前，照 `ShapeSizeInspector` Radius 行写 `image.cornerRadius` 的 `NumberPropertyRow`。`setCornerRadius`：`< 0` 写成 `0`。`hasCornerRadiusKeyframe` / `toggleCornerRadiusKeyframe` 与 size 对称。

- [ ] **Step 2: Add Group Radius inspector**

`InspectorView.swift` 在 Transform 段之前：

```swift
if core.layerType(layerID) == .GROUP {
    Text("Group")
        .font(.subheadline)
        .foregroundStyle(.secondary)
    NumberPropertyRow(label: "Radius",
                      value: core.evaluateFloat(entityID: layerID,
                                                path: GroupProperty.cornerRadius.path,
                                                frame: playheadClock.frame),
                      hasKeyframeAtPlayhead: core.keyframes(
                          entityID: layerID,
                          path: GroupProperty.cornerRadius.path)
                          .contains { $0.frame == playheadClock.frame },
                      isEditable: isEditable)
    { newValue in
        let radius = max(newValue, 0)
        perform("Set Group Corner Radius") {
            if core.keyframes(entityID: layerID, path: GroupProperty.cornerRadius.path)
                .contains(where: { $0.frame == playheadClock.frame })
            {
                core.addKeyframeFloat(entityID: layerID,
                                      path: GroupProperty.cornerRadius.path,
                                      frame: playheadClock.frame,
                                      value: radius)
            } else {
                core.setStaticFloat(entityID: layerID,
                                    path: GroupProperty.cornerRadius.path,
                                    value: radius)
            }
        }
    } onToggleKeyframe: { value in
        // add/remove keyframe at playhead，与 ShapeSizeInspector.toggleCornerRadiusKeyframe 相同结构
    }
}
```

`playheadClock.frame` 若 InspectorView 里没有 clock 环境，与 `ImageLayerInspector` 一样用 `@Environment(PlayheadClock.self)`。优先抽小组件 `GroupLayerInspector` 放在 `Inspector/`，避免 `InspectorView` 过长。

- [ ] **Step 3: Timeline rows**

`timelineAnimatedPropertyPaths`：

```swift
    if !core.keyframes(entityID: layerID, path: ImageProperty.cornerRadius.path).isEmpty {
        paths.append(ImageProperty.cornerRadius.path)
    }
    if !core.keyframes(entityID: layerID, path: GroupProperty.cornerRadius.path).isEmpty {
        paths.append(GroupProperty.cornerRadius.path)
    }
```

`buildTimelineRows` 在 Shape 循环后加：

```swift
        for property in ImageProperty.allCases where animatedPaths.contains(property.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: property.path,
                                            label: property.actionLabel))
        }
        for property in GroupProperty.allCases where animatedPaths.contains(property.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: property.path,
                                            label: property.actionLabel))
        }
```

这也会让已有 `image.size` 关键帧出现在时间轴（先前 `timelineAnimatedPropertyPaths` 收了但 `buildTimelineRows` 没画）。

- [ ] **Step 4: Build the app**

优先 Xcode MCP `BuildProject`；不可用则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64"
```

Expected: BUILD SUCCEEDED。

- [ ] **Step 5: Commit**

```bash
git commit --only apps/MotionStudioApp/MotionStudioApp/Bridge/PropertyPath.swift apps/MotionStudioApp/MotionStudioApp/Inspector/ImageLayerInspector.swift apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift apps/MotionStudioApp/MotionStudioApp/Inspector/GroupLayerInspector.swift docs/superpowers/plans/2026-08-19-image-group-corner-radius.md -m "$(cat <<'EOF'
Add corner radius controls for image and group layers.

EOF
)"
```

若未创建 `GroupLayerInspector.swift`，从 `--only` 里去掉它。

---

## Spec coverage

| Spec | Task |
|---|---|
| Image/Group `Animatable<float> cornerRadius` + 路径 | 1 |
| 序列化 / 缺省 0 | 2 |
| 求值 clamp | 3 |
| Image ClipPath、无额外 BeginLayer | 4 |
| Group BeginLayer / AABB / 相对 opacity / 去掉 inherit | 5 |
| 点选 | 6 |
| PAG mask + warnings | 7 |
| Inspector / Timeline | 8 |
| 非目标（四角、Group 固定尺寸、羽化、升 schema） | 不实现 |
