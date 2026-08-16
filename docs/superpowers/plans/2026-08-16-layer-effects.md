# Layer Effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**Goal:** 叶子图层（Shape / Image / Text）支持多 effect 后处理链：整层离屏合成后按序 `Image → Image`，再以图层 opacity / blend 叠回。

**Architecture:** `Layer::effects[]` 与 `styles[]` 并列。求值用 `LayerEffect::snapshot(time)` bake 成静态值，`EvaluatedLayer` / `EndLayer` 持 `shared_ptr<const LayerEffect>`。有 effect 时复用 `BeginLayer` / `EndLayer`。adapter 在 `endLayer` 把 Picture 栅格成 Image，mask 之后链式 Apply。首版 BrightnessContrast + GaussianBlur。不引入 `EvaluatedEffect` / `variant`。

**Tech Stack:** C++17、GoogleTest、tgfx Metal、`RuntimeFilter` / `ImageFilter::Blur`

**Spec:** `docs/superpowers/specs/2026-08-16-layer-effects-design.md`

## 全局约束

- 仅 Shape / Image / Text；Precomp / Group 上的 effect 存盘但不求值、不渲染
- 不引入 `EvaluatedEffect` / `std::variant` / 拍平参数 struct
- 分发用 `type()` + `static_cast`；禁止 `dynamic_cast` 与 C++ 异常
- 不升 `schemaVersion`；`effects` 缺省 = `[]`；未知 JSON `type` 跳过该项
- 不做 Inspector / 时间轴 UI、Lottie/PAG 导出 Effect、AE Layer Style、其余 PAG Effect
- 不改 `ColorSourceEffect` 生产代码
- 命中 / 选中框不随 blur 扩张
- `if` / `switch` 分支必须 `{}`；成员 `= {}` 初始化
- CMake 已 glob `src/`、`include/`、`tests/`、`adapter/tgfx/src` 与 `tests`，新文件自动进库
- Commit：英语一句、句号结尾、120 字符内、无其它标点；不 push；在 master 则先建 `feature/{username}_layer_effects`
- 每完成一步立刻把本 plan 对应 `- [ ]` 改为 `- [x]` 并更新 Task `**Status:**`，与代码一并 commit

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/model/LayerEffect.h` | `LayerEffect` 基类 + BC / GaussianBlur |
| `src/model/LayerEffect.cpp` | `type()` / `snapshot()` |
| `include/MotionStudio/model/Layer.h` | `effects` 向量 |
| `src/serialization/Serializer.cpp` | 读写 `effects` |
| `src/model/PropertyPath.cpp` | `effects[i].brightness\|contrast\|blurriness` |
| `include/MotionStudio/undo/CommandKind.h` + 五个命令 | 增删移 / enabled / repeatEdge |
| `include/MotionStudio/render/EvaluatedLayer.h` | `vector<shared_ptr<const LayerEffect>>` |
| `src/render/SceneEvaluator.cpp` | `FillCommonLayerFields` 调 `snapshot` |
| `include/MotionStudio/render/DrawCommand.h` + `RenderAdapter.h` | `EndLayer` 带 effects |
| `src/render/CommandBuilder.cpp` + `RenderAdapter.cpp` | isolation 条件 + `PlayCommands` |
| `adapter/tgfx/src/TgfxIsolation.h` | `compositeOpacity` / `compositeBlend` |
| `adapter/tgfx/src/TgfxCanvasAdapter.cpp` | `beginLayer` 快照；`endLayer` 滤镜链 |
| `adapter/tgfx/src/effects/GaussianBlurFilter.{h,cpp}` | `ImageFilter::Blur` |
| `bridge/include/motionstudio_bridge.h` + layer/commands | `MS_EFFECT` 与命令 |
| `apps/.../Bridge/MotionStudioBridgingExtension.swift` | `MS_EFFECT` CaseIterable |
| 测试 / `docs/data-model.md` / `docs/rendering.md` / `docs/pag-runtime-filter.md` | 验收与文档 |

---

### Task 1: LayerEffect 模型 + snapshot

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/model/LayerEffect.h`
- Create: `src/model/LayerEffect.cpp`
- Create: `tests/model/LayerEffectTest.cpp`
- Modify: `include/MotionStudio/model/Layer.h`（加 `#include` 与 `effects`）

**Interfaces:**
- Produces: `enum class LayerEffectType { BrightnessContrast, GaussianBlur }`
- Produces: `LayerEffect::snapshot(PreviewTime) const → shared_ptr<const LayerEffect>`（`!enabled` 或恒等 → `nullptr`）
- Produces: `Layer::effects` 为 `vector<unique_ptr<LayerEffect>>`
- Consumes: `Animatable<float>::evaluatePreview` / `setStaticValue`

- [x] **Step 1: 写失败测试**

`tests/model/LayerEffectTest.cpp`：

```cpp
#include <gtest/gtest.h>

#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerEffect.h"

using motion::BrightnessContrastEffect;
using motion::GaussianBlurEffect;
using motion::Layer;
using motion::LayerEffectType;
using motion::LayerType;

TEST(LayerEffectTest, DefaultBrightnessContrastIsIdentitySnapshot) {
    BrightnessContrastEffect effect;
    EXPECT_EQ(effect.type(), LayerEffectType::BrightnessContrast);
    EXPECT_TRUE(effect.enabled);
    EXPECT_EQ(effect.snapshot(0), nullptr);
}

TEST(LayerEffectTest, DisabledEffectSnapshotsToNull) {
    BrightnessContrastEffect effect;
    effect.enabled = false;
    effect.brightness.setStaticValue(40.0f);
    EXPECT_EQ(effect.snapshot(0), nullptr);
}

TEST(LayerEffectTest, BakesStaticValuesAndKeepsId) {
    BrightnessContrastEffect effect;
    effect.brightness.setStaticValue(40.0f);
    effect.contrast.setStaticValue(-10.0f);
    const auto snap = effect.snapshot(0);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->id, effect.id);
    EXPECT_EQ(snap->type(), LayerEffectType::BrightnessContrast);
    const auto &baked = static_cast<const BrightnessContrastEffect &>(*snap);
    EXPECT_FLOAT_EQ(baked.brightness.evaluate(0), 40.0f);
    EXPECT_FLOAT_EQ(baked.contrast.evaluate(0), -10.0f);
    EXPECT_FALSE(baked.brightness.isAnimated());
}

TEST(LayerEffectTest, GaussianBlurZeroBlurrinessIsIdentity) {
    GaussianBlurEffect effect;
    EXPECT_EQ(effect.snapshot(0), nullptr);
    effect.blurriness.setStaticValue(8.0f);
    const auto snap = effect.snapshot(0);
    ASSERT_NE(snap, nullptr);
    const auto &baked = static_cast<const GaussianBlurEffect &>(*snap);
    EXPECT_FLOAT_EQ(baked.blurriness.evaluate(0), 8.0f);
    EXPECT_FALSE(baked.repeatEdgePixels);
}

TEST(LayerEffectTest, LayerHoldsEffectsVector) {
    Layer layer(LayerType::Shape);
    layer.effects.push_back(std::make_unique<GaussianBlurEffect>());
    ASSERT_EQ(layer.effects.size(), 1u);
    EXPECT_EQ(layer.effects[0]->type(), LayerEffectType::GaussianBlur);
}
```

- [x] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='LayerEffectTest.*'
```

Expected: 编译失败（找不到 `LayerEffect.h`）。

- [x] **Step 3: 实现模型**

`include/MotionStudio/model/LayerEffect.h`：按 spec §1，含 `snapshot` 纯虚与两个子类。`#include "MotionStudio/common/Time.h"`。

`src/model/LayerEffect.cpp`：

```cpp
#include "MotionStudio/model/LayerEffect.h"

namespace motion {

LayerEffect::LayerEffect(LayerEffectType type) : type_(type) {
}

LayerEffectType LayerEffect::type() const {
    return type_;
}

BrightnessContrastEffect::BrightnessContrastEffect()
    : LayerEffect(LayerEffectType::BrightnessContrast) {
}

std::shared_ptr<const LayerEffect> BrightnessContrastEffect::snapshot(PreviewTime time) const {
    if (!enabled) {
        return nullptr;
    }
    const float bakedBrightness = brightness.evaluatePreview(time);
    const float bakedContrast = contrast.evaluatePreview(time);
    if (bakedBrightness == 0.0f && bakedContrast == 0.0f) {
        return nullptr;
    }
    auto baked = std::make_shared<BrightnessContrastEffect>();
    baked->id = id;
    baked->enabled = true;
    baked->brightness.setStaticValue(bakedBrightness);
    baked->contrast.setStaticValue(bakedContrast);
    return baked;
}

GaussianBlurEffect::GaussianBlurEffect() : LayerEffect(LayerEffectType::GaussianBlur) {
}

std::shared_ptr<const LayerEffect> GaussianBlurEffect::snapshot(PreviewTime time) const {
    if (!enabled) {
        return nullptr;
    }
    const float bakedBlurriness = blurriness.evaluatePreview(time);
    if (bakedBlurriness <= 0.0f) {
        return nullptr;
    }
    auto baked = std::make_shared<GaussianBlurEffect>();
    baked->id = id;
    baked->enabled = true;
    baked->blurriness.setStaticValue(bakedBlurriness);
    baked->repeatEdgePixels = repeatEdgePixels;
    return baked;
}

}  // namespace motion
```

`Layer.h`：`#include "MotionStudio/model/LayerEffect.h"`，在 `styles` 旁加：

```cpp
std::vector<std::unique_ptr<LayerEffect>> effects;
```

- [x] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='LayerEffectTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git add include/MotionStudio/model/LayerEffect.h src/model/LayerEffect.cpp \
  include/MotionStudio/model/Layer.h tests/model/LayerEffectTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-effects.md
git commit --only include/MotionStudio/model/LayerEffect.h src/model/LayerEffect.cpp \
  include/MotionStudio/model/Layer.h tests/model/LayerEffectTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Add layer effect model with baked snapshots."
```

---

### Task 2: 序列化 + PropertyPath

**Status:** ✅ Done

**Files:**
- Modify: `src/serialization/Serializer.cpp`（`LayerToJson` / `LayerFromJson`）
- Modify: `src/model/PropertyPath.cpp`（`ResolveAnimatable`）
- Create: `tests/serialization/LayerEffectSerializerTest.cpp`
- Modify: `tests/model/PropertyPathTest.cpp`（若无则在 Serializer 测试里用 `ResolveAnimatable`）

**Interfaces:**
- Consumes: Task 1 的 `LayerEffect` 子类
- Produces: JSON `type` = `brightnessContrast` / `gaussianBlur`；未知 type 跳过
- Produces: `ResolveAnimatable` 支持 `effects[i].brightness|contrast|blurriness`

- [x] **Step 1: 写失败测试**

`tests/serialization/LayerEffectSerializerTest.cpp` 用与 `SceneEvaluatorTest` 相同的最小 Document（一个 Shape + Fill），再 `push_back` effect：

```cpp
TEST(LayerEffectSerializerTest, RoundTripKeepsBothEffectTypes) {
    // BC brightness 关键帧 + GaussianBlur blurriness=8 repeatEdgePixels=true
    const std::string first = Serializer::serialize(document);
    auto restored = Serializer::deserialize(first);
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ((*restored)->compositions[0]->layers[0]->effects.size(), 2u);
    EXPECT_EQ((*restored)->compositions[0]->layers[0]->effects[0]->type(),
              LayerEffectType::BrightnessContrast);
    const auto *blur = static_cast<const GaussianBlurEffect *>(
        (*restored)->compositions[0]->layers[0]->effects[1].get());
    EXPECT_TRUE(blur->repeatEdgePixels);
    EXPECT_EQ(Serializer::serialize(**restored), first);
}

TEST(LayerEffectSerializerTest, MissingEffectsArrayLoadsEmpty) {
    auto json = nlohmann::json::parse(Serializer::serialize(document));
    json["compositions"][0]["layers"][0].erase("effects");
    auto restored = Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    EXPECT_TRUE((*restored)->compositions[0]->layers[0]->effects.empty());
}

TEST(LayerEffectSerializerTest, UnknownTypeIsSkipped) {
    auto json = nlohmann::json::parse(Serializer::serialize(document));
    json["compositions"][0]["layers"][0]["effects"] = nlohmann::json::array({
        {{"id", "aaaaaaaaaaaaaaaa"}, {"type", "unknownEffect"}},
        json["compositions"][0]["layers"][0]["effects"][1],
    });
    auto restored = Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ((*restored)->compositions[0]->layers[0]->effects.size(), 1u);
    EXPECT_EQ((*restored)->compositions[0]->layers[0]->effects[0]->type(),
              LayerEffectType::GaussianBlur);
}

TEST(LayerEffectSerializerTest, ResolvesBrightnessPropertyPath) {
    PropertyPath path{layer->id, "effects[0].brightness"};
    AnimatableBase *base = ResolveAnimatable(document, path);
    ASSERT_NE(base, nullptr);
    static_cast<Animatable<float> *>(base)->setStaticValue(33.0f);
    EXPECT_FLOAT_EQ(static_cast<BrightnessContrastEffect *>(layer->effects[0].get())
                        ->brightness.evaluate(0),
                    33.0f);
}
```

旧文档路径：先 `serialize` 再 `erase("effects")`，不要手写残缺层 JSON。`ResolveAnimatable` 在 `MotionStudio/model/PropertyPath.h`。

- [x] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='LayerEffectSerializerTest.*:PropertyPathTest.ResolvesEffectBrightness'
```

Expected: FAIL（JSON 无 `effects` 或 path 解析不到）。

- [x] **Step 3: 实现**

`Serializer.cpp` 在 `LayerStyleToJson` 旁加 `LayerEffectToJson` / `LayerEffectFromJson`：

- 写出 `id`、`type`、`enabled`、对应 Animatable 字段；GaussianBlur 另写 `repeatEdgePixels`。
- 读入：`type` 未知 → 返回空 `unique_ptr`（不是 error）。缺 `enabled` → `true`。缺 `repeatEdgePixels` → `false`。
- `LayerToJson`：始终写 `effects` 数组（可空）。
- `LayerFromJson`：`FindChild(node, "effects")`；无或非数组 → 空；对每项 `FromJson`，空指针 skip，error 则整层失败。

`PropertyPath.cpp` 在 `styles` 分支后：

```cpp
if (first.name == "effects" && first.index >= 0 && segments.size() == 2) {
    if (first.index >= static_cast<int>(layer->effects.size())) {
        return nullptr;
    }
    LayerEffect *effect = layer->effects[static_cast<size_t>(first.index)].get();
    const std::string &name = segments[1].name;
    switch (effect->type()) {
        case LayerEffectType::BrightnessContrast: {
            auto *bc = static_cast<BrightnessContrastEffect *>(effect);
            if (name == "brightness") {
                return &bc->brightness;
            }
            if (name == "contrast") {
                return &bc->contrast;
            }
            break;
        }
        case LayerEffectType::GaussianBlur: {
            auto *blur = static_cast<GaussianBlurEffect *>(effect);
            if (name == "blurriness") {
                return &blur->blurriness;
            }
            break;
        }
    }
    return nullptr;
}
```

- [x] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='LayerEffectSerializerTest.*:PropertyPathTest.*Effect*'
```

Expected: PASS。再跑一遍现有 Serializer 回归：`./build/tests/core_tests --gtest_filter='SerializerTest.*'`。

- [x] **Step 5: Commit**

```bash
git commit --only src/serialization/Serializer.cpp src/model/PropertyPath.cpp \
  tests/serialization/LayerEffectSerializerTest.cpp tests/model/PropertyPathTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Serialize layer effects and resolve effect property paths."
```

---

### Task 3: Undo 命令

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/undo/CommandKind.h`
- Create: `include/MotionStudio/undo/AddLayerEffectCommand.h` + `src/undo/AddLayerEffectCommand.cpp`
- Create: `include/MotionStudio/undo/RemoveLayerEffectCommand.h` + `src/undo/RemoveLayerEffectCommand.cpp`
- Create: `include/MotionStudio/undo/MoveLayerEffectCommand.h` + `src/undo/MoveLayerEffectCommand.cpp`
- Create: `include/MotionStudio/undo/SetLayerEffectEnabledCommand.h` + `src/undo/SetLayerEffectEnabledCommand.cpp`
- Create: `include/MotionStudio/undo/SetGaussianBlurRepeatEdgeCommand.h` + `src/undo/SetGaussianBlurRepeatEdgeCommand.cpp`
- Modify: `tests/undo/CommandsTest.cpp`

**Interfaces:**
- Produces: `AddLayerEffectCommand(EntityId layerId, unique_ptr<LayerEffect>)` — append；undo 按 `id` 移除
- Produces: `RemoveLayerEffectCommand(EntityId, int index)` — 与 `RemoveStyleCommand` 同形
- Produces: `MoveLayerEffectCommand(EntityId, int from, int to)` — 任意 index，越界 no-op；`mergeWith` 同 `MoveLayerStyleCommand`
- Produces: `SetLayerEffectEnabledCommand(EntityId, int index, bool)` — merge 同层同 index
- Produces: `SetGaussianBlurRepeatEdgeCommand(EntityId, int index, bool)` — 非 GaussianBlur 则 no-op
- Produces: `CommandKind::{AddLayerEffect,RemoveLayerEffect,MoveLayerEffect,SetLayerEffectEnabled,SetGaussianBlurRepeatEdge}`

- [x] **Step 1: 写失败测试**

在 `tests/undo/CommandsTest.cpp` 用现有 `Scene` fixture：

- `AddLayerEffectCommand`：append BC，undo 清空，redo 同 `id`
- 缺层：`EntityId{999}` execute + undo 不崩
- `RemoveLayerEffectCommand`：两 effect 删 index 0，undo 插回原位
- 越界 / 缺层：no-op
- `MoveLayerEffectCommand`：`[BC, Blur]` 0→1 后序为 Blur, BC；undo 恢复；`mergeWith` 连续 0→1 再 1→0 合成一次 undo
- `SetLayerEffectEnabledCommand`：false → undo true
- `SetGaussianBlurRepeatEdgeCommand`：true → undo false；对 BC 调用保持 `repeatEdgePixels` 不变
- `SetStaticValueCommand` + `PropertyPath{id, "effects[0].blurriness"}`：改值 + undo 指纹回到操作前（用现有 `documentFingerprint` 若测试里已有）

- [x] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='*LayerEffectCommand*'
```

Expected: 编译失败。

- [x] **Step 3: 实现五个命令**

`AddLayerEffectCommand::execute`：找到 layer 后 `effects.push_back(std::move(effect_))`。`undo`：按 `effectId_` 找到并 `move` 回 `effect_`。缺层 return。

`RemoveLayerEffectCommand`：`execute` 越界 return；否则 `effect_ = std::move(effects[index])` 再 erase。`undo` 插回 `index_`。

`MoveLayerEffectCommand`：越界或 `from==to` return；`move` + erase + insert。`mergeWith`：`kind` 相同且 `layerId` 相同且 `other.fromIndex_ == toIndex_` 则 `toIndex_ = other.toIndex_`。

`SetLayerEffectEnabledCommand`：`execute` 记下 `oldEnabled_` 再赋值。`undo` 写回。`mergeWith` 同层同 index。

`SetGaussianBlurRepeatEdgeCommand`：`type() != GaussianBlur` return；否则改 `repeatEdgePixels`。`mergeWith` 同层同 index。

`describe()` 分别为 `"Add Effect"` / `"Remove Effect"` / `"Move Effect"` / `"Set Effect Enabled"` / `"Set Blur Repeat Edge"`。

- [x] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='*LayerEffectCommand*:*GaussianBlurRepeat*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/undo/CommandKind.h \
  include/MotionStudio/undo/AddLayerEffectCommand.h src/undo/AddLayerEffectCommand.cpp \
  include/MotionStudio/undo/RemoveLayerEffectCommand.h src/undo/RemoveLayerEffectCommand.cpp \
  include/MotionStudio/undo/MoveLayerEffectCommand.h src/undo/MoveLayerEffectCommand.cpp \
  include/MotionStudio/undo/SetLayerEffectEnabledCommand.h src/undo/SetLayerEffectEnabledCommand.cpp \
  include/MotionStudio/undo/SetGaussianBlurRepeatEdgeCommand.h src/undo/SetGaussianBlurRepeatEdgeCommand.cpp \
  tests/undo/CommandsTest.cpp docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Add undo commands for layer effects."
```

---

### Task 4: SceneEvaluator 快照进 EvaluatedLayer

**Status:** 待开始

**Files:**
- Modify: `include/MotionStudio/render/EvaluatedLayer.h`
- Modify: `src/render/SceneEvaluator.cpp`（`FillCommonLayerFields`）
- Modify: `tests/render/SceneEvaluatorTest.cpp`

**Interfaces:**
- Produces: `EvaluatedLayer::effects` 为 `vector<shared_ptr<const LayerEffect>>`
- Consumes: `LayerEffect::snapshot`
- Precomp 仍在 `FillCommonLayerFields` 前 `return`，不带预合成自己的 effect

- [ ] **Step 1: 写失败测试**

用现有 `RectScene`：

```cpp
TEST(SceneEvaluatorTest, SkipsIdentityAndDisabledEffects) {
    RectScene scene;
    auto identity = std::make_unique<BrightnessContrastEffect>();
    auto disabled = std::make_unique<GaussianBlurEffect>();
    disabled->blurriness.setStaticValue(8.0f);
    disabled->enabled = false;
    scene.layer->effects.push_back(std::move(identity));
    scene.layer->effects.push_back(std::move(disabled));
    auto result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers[0].effects.empty());
}

TEST(SceneEvaluatorTest, KeepsNonIdentityEffectsInOrder) {
    RectScene scene;
    auto bc = std::make_unique<BrightnessContrastEffect>();
    bc->brightness.setStaticValue(20.0f);
    auto blur = std::make_unique<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(6.0f);
    scene.layer->effects.push_back(std::move(bc));
    scene.layer->effects.push_back(std::move(blur));
    auto result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].effects.size(), 2u);
    EXPECT_EQ(result->layers[0].effects[0]->type(), LayerEffectType::BrightnessContrast);
    EXPECT_EQ(result->layers[0].effects[1]->type(), LayerEffectType::GaussianBlur);
}

TEST(SceneEvaluatorTest, PrecompEffectsAreIgnored) {
    // 外层 Precomp 带 GaussianBlur(8)，内层一个红色 Rect。
    // Evaluate 后只有内层叶子，没有任何 EvaluatedLayer.effects。
}
```

Precomp 用例：`Document` 两个 composition；外层 `LayerType::Precomp` 的 `PrecompContent::compositionId` 指向内层；外层 `effects` 推一个非恒等 GaussianBlur。断言 `state.layers.size()==1` 且 `effects.empty()`。

- [ ] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.*Effect*'
```

Expected: 编译失败（无 `effects` 字段）。

- [ ] **Step 3: 实现**

`EvaluatedLayer.h`：`#include <memory>` + `LayerEffect.h`，加 `std::vector<std::shared_ptr<const LayerEffect>> effects;`

`FillCommonLayerFields` 末尾：

```cpp
for (const auto &effect : layer.effects) {
    if (std::shared_ptr<const LayerEffect> snap = effect->snapshot(time)) {
        evaluated.effects.push_back(std::move(snap));
    }
}
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='SceneEvaluatorTest.*'
```

Expected: 全绿（含旧用例）。

- [ ] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/render/EvaluatedLayer.h \
  src/render/SceneEvaluator.cpp tests/render/SceneEvaluatorTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Evaluate layer effect snapshots into the scene state."
```

---

### Task 5: CommandBuilder + EndLayer 签名

**Status:** 待开始

**Files:**
- Modify: `include/MotionStudio/render/DrawCommand.h`
- Modify: `include/MotionStudio/render/RenderAdapter.h`
- Modify: `src/render/RenderAdapter.cpp`
- Modify: `src/render/CommandBuilder.cpp`
- Modify: `adapter/tgfx/include/TgfxCanvasAdapter.h`
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`（只改签名，滤镜逻辑在 Task 6）
- Modify: `tests/render/CommandBuilderTest.cpp`

**Interfaces:**
- Produces: `DrawCommand::effects` 为 `vector<shared_ptr<const LayerEffect>>`（仅 `EndLayer` 有意义）
- Produces: `RenderAdapter::endLayer(const vector<shared_ptr<const LayerEffect>> &effects)`
- Produces: `needsIsolation |= !evaluated.effects.empty()`；`EndLayer` 拷贝 `evaluated.effects`
- Task 6 之前 adapter **忽略** effects，仍 `drawPicture`（保证现有 mask 测试可编过）

- [ ] **Step 1: 写失败测试**

`tests/render/CommandBuilderTest.cpp`：

```cpp
TEST(CommandBuilderTest, EffectsForceIsolationAndRideOnEndLayer) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    auto blur = std::make_shared<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(8.0f);
    layer.effects.push_back(std::move(blur));
    state.layers.push_back(std::move(layer));

    const auto commands = BuildCommands(state);
    bool sawBegin = false;
    const DrawCommand *endLayer = nullptr;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::BeginLayer) {
            sawBegin = true;
        }
        if (command.type == DrawCommandType::EndLayer) {
            endLayer = &command;
        }
    }
    EXPECT_TRUE(sawBegin);
    ASSERT_NE(endLayer, nullptr);
    ASSERT_EQ(endLayer->effects.size(), 1u);
    EXPECT_EQ(endLayer->effects[0]->type(), LayerEffectType::GaussianBlur);
}

TEST(CommandBuilderTest, IdentitySkippedEffectsDoNotIsolate) {
    // EvaluatedLayer.effects 为空（求值已丢掉恒等项）→ 无 Begin/EndLayer
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    state.layers.push_back(std::move(layer));
    const auto commands = BuildCommands(state);
    for (const auto &command : commands) {
        EXPECT_NE(command.type, DrawCommandType::BeginLayer);
        EXPECT_NE(command.type, DrawCommandType::EndLayer);
    }
}

TEST(CommandBuilderTest, MaskCommandsStayBeforeEndLayerEffects) {
    // 现有 PathMasks 用例基础上给 layer.effects 推一个 bake 过的 BC
    // 断言最后一个 EndLayer.effects.size()==1，且 BeginMask 出现在该 EndLayer 之前
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='CommandBuilderTest.*Effect*'
```

Expected: FAIL。

- [ ] **Step 3: 改指令与签名**

`DrawCommand.h`：`#include <memory>` + `LayerEffect.h`，加 `effects` 字段。

`RenderAdapter.h`：`endLayer` 改为带 `const std::vector<std::shared_ptr<const LayerEffect>> &effects`。

`PlayCommands`：`adapter.endLayer(command.effects);`

`CommandBuilder.cpp`：

```cpp
const bool needsIsolation = !layer.masks.empty() ||
    layer.trackMatteType != TrackMatteType::None || !layer.effects.empty();
// ...
DrawCommand endLayer;
endLayer.type = DrawCommandType::EndLayer;
endLayer.effects = layer.effects;
commands.push_back(std::move(endLayer));
```

`TgfxCanvasAdapter`：签名跟上，函数体仍是当前 `drawPicture` 路径（先不读 `effects`）。

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target core_tests tgfx_adapter_test
./build/tests/core_tests --gtest_filter='CommandBuilderTest.*'
```

Expected: PASS。adapter 目标必须能编过。

- [ ] **Step 5: Commit**

```bash
git commit --only include/MotionStudio/render/DrawCommand.h \
  include/MotionStudio/render/RenderAdapter.h src/render/RenderAdapter.cpp \
  src/render/CommandBuilder.cpp tests/render/CommandBuilderTest.cpp \
  adapter/tgfx/include/TgfxCanvasAdapter.h adapter/tgfx/src/TgfxCanvasAdapter.cpp \
  docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Carry baked effects on EndLayer isolation commands."
```

---

### Task 6: adapter isolation 快照 + BC 接线

**Status:** 待开始

**Files:**
- Modify: `adapter/tgfx/src/TgfxIsolation.h`
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`
- Modify: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`

**Interfaces:**
- Consumes: `endLayer(effects)`、`renderCache_`、`BrightnessContrastFilter::Apply`
- Produces: `IsolationLayer::{compositeOpacity, compositeBlend}`；`beginLayer` 快照后把 `opacity_`/`blendMode_` 置为 `1`/`Normal`
- Produces: effects 非空时 Picture→Image →（可选 mask）→ 链式 Apply → `drawImage`
- Produces: 单个 `Apply` 返回 `nullptr` 则停链，用上一步 image；尚无 image 则退回 `drawPicture`

- [ ] **Step 1: 写失败测试**

`adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`（Metal 不可用则 `GTEST_SKIP`，与现有用例相同）：

```cpp
TEST(TgfxRenderAdapterTest, BrightnessContrastRaisesCenterLuma) {
    // 100x100 黑底；40x40 灰矩形 (0.5,0.5,0.5,1) 居中。
    // 无 effect 读中心 luma。同几何 brightness=100 contrast=0，中心 luma 明显更高。
}

TEST(TgfxRenderAdapterTest, LayerOpacityAppliesAfterBrightnessContrast) {
    // 不透明红矩形 + BC brightness=0 contrast=0 不能用（恒等会被求值丢掉）。
    // 用 brightness=0.001 或直接往 EvaluatedLayer.effects 塞一个 bake 过、
    // brightness=0 contrast=0 的 shared_ptr（绕过 snapshot）不合适。
    // 改用 brightness=10 + layer.opacity=0.5：中心 alpha 约 128（容差 ±20），
    // 且 RGB 仍明显高于「先把红乘 0.5 再 BC」的直觉对照——断言 alpha 即可。
}
```

构造：直接填 `EvaluatedLayer`（不必走 Document），`effects` 推 `shared_ptr<BrightnessContrastEffect>` 且已 `setStaticValue`。`PlayCommands(BuildCommands(state))`。

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxRenderAdapterTest.BrightnessContrast*'
```

Expected: 中心 luma / alpha 与无滤镜几乎相同（effects 被忽略）。

- [ ] **Step 3: 实现 endLayer 滤镜路径**

`TgfxIsolation.h` 的 `IsolationLayer` 增加：

```cpp
float compositeOpacity = 1.0f;
BlendMode compositeBlend = BlendMode::Normal;
```

`#include "MotionStudio/model/BlendMode.h"`。

`beginLayer`：emplace 后写入 `compositeOpacity = opacity_`、`compositeBlend = blendMode_`，再 `opacity_ = 1`、`blendMode_ = BlendMode::Normal`，然后 `beginRecording`。

匿名命名空间加 `PictureToImage`：用现有 mask 同样的 `Surface::Make(context, w, h, true)` 回退不带 alpha；`translate(-rounded.left, -rounded.top)`；`drawPicture`；`makeImageSnapshot()`。空 `contentBounds` → `nullptr`。

`ApplyLayerEffect`：

```cpp
switch (effect.type()) {
    case LayerEffectType::BrightnessContrast: {
        const auto &bc = static_cast<const BrightnessContrastEffect &>(effect);
        return BrightnessContrastFilter::Apply(input, cache, bc.brightness.evaluate(0),
                                               bc.contrast.evaluate(0), offset);
    }
    case LayerEffectType::GaussianBlur:
        return input;  // Task 7 再接
    default:
        return input;
}
```

`endLayer`：

1. finish picture；取出 `compositeOpacity` / `compositeBlend` / coverages / contentBounds。
2. pop isolation（与现在一样先 pop 再画到 parent）。
3. 若 `effects.empty()`：现 `drawPicture` + MaskFilter；`paint.setAlpha(compositeOpacity)`、`setBlendMode(ToTgfxBlendMode(compositeBlend))`。
4. 否则：`PictureToImage`；失败 → 走步骤 3。有 coverages 则把现有 MaskFilter 画到同尺寸中间 Surface 再 snapshot（mask 在滤镜前）。`offset=0`，对每个 effect `ApplyLayerEffect`，`nullptr` 则 break 保留上一张。`parent->drawImage(image, Matrix::MakeTrans(bounds.left+offset.x, bounds.top+offset.y), &paint)`，paint 用 composite opacity/blend。

`#include "effects/BrightnessContrastFilter.h"` 与 `LayerEffect.h`。

仅 mask、无 effect 的层也走步骤 3，opacity 改为 composite 时乘。现有 mask 测试默认 opacity=1，不应挂。

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target tgfx_adapter_test
# 跑新用例 + 全部 TgfxRenderAdapterTest 防回归
```

Expected: 新用例 PASS；旧 mask / opacity 用例仍 PASS。

- [ ] **Step 5: Commit**

```bash
git commit --only adapter/tgfx/src/TgfxIsolation.h adapter/tgfx/src/TgfxCanvasAdapter.cpp \
  adapter/tgfx/tests/TgfxRenderAdapterTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Apply brightness contrast after isolated layer content."
```

---

### Task 7: GaussianBlur + 链序 / mask 渗出

**Status:** 待开始

**Files:**
- Create: `adapter/tgfx/src/effects/GaussianBlurFilter.h`
- Create: `adapter/tgfx/src/effects/GaussianBlurFilter.cpp`
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`（`ApplyLayerEffect` 接 GaussianBlur）
- Modify: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`

**Interfaces:**
- Produces: `GaussianBlurFilter::Apply(input, cache, blurriness, repeatEdgePixels, offset)`
- 半径：`ImageFilter::Blur(blurriness/2, blurriness/2)`；`repeatEdgePixels==true` 时 `TileMode::Clamp` 且 clip 回输入 WH（对齐 PAG `GaussianBlurFilter`）
- `cache` 本滤镜不用，签名保留

- [ ] **Step 1: 写失败测试**

```cpp
TEST(TgfxRenderAdapterTest, GaussianBlurSoftensRectEdge) {
    // 黑底；中心 20x20 白矩形。blurriness=16。
    // 矩形外数像素（如中心+14,0）alpha > 0；无 blur 时该点应为背景。
}

TEST(TgfxRenderAdapterTest, EffectOrderChangesHalfBlackHalfWhite) {
    // 64x64 层：左黑右白（两个相邻 rect 或一个 path）。
    // 链 A：BC contrast=80 再 Blur 12；链 B：Blur 12 再 BC contrast=80。
    // readPixels 两缓冲 memcmp 不全等。
}

TEST(TgfxRenderAdapterTest, BlurBleedsOutsideAddMask) {
    // 大白矩形 + 小于内容的 Add mask（中心小圆/小矩形）。
    // GaussianBlur 16。mask 外、原内容内的一点：无 blur 时 alpha≈0，有 blur 时 alpha>0。
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxRenderAdapterTest.GaussianBlur*:TgfxRenderAdapterTest.EffectOrder*:TgfxRenderAdapterTest.BlurBleed*'
```

Expected: FAIL（`ApplyLayerEffect` 对 GaussianBlur 原样返回）。

- [ ] **Step 3: 实现 GaussianBlur**

`GaussianBlurFilter.h` / `.cpp`：

```cpp
std::shared_ptr<tgfx::Image> GaussianBlurFilter::Apply(std::shared_ptr<tgfx::Image> input,
                                                   RenderCache * /*cache*/, float blurriness,
                                                   bool repeatEdgePixels, tgfx::Point *offset) {
    if (input == nullptr || blurriness <= 0.0f) {
        return input;
    }
    const float radius = blurriness * 0.5f;
    if (repeatEdgePixels) {
        auto filter = tgfx::ImageFilter::Blur(radius, radius, tgfx::TileMode::Clamp);
        tgfx::Rect clip = tgfx::Rect::MakeWH(static_cast<float>(input->width()),
                                             static_cast<float>(input->height()));
        return input->makeWithFilter(filter, offset, &clip);
    }
    auto filter = tgfx::ImageFilter::Blur(radius, radius);
    return input->makeWithFilter(filter, offset);
}
```

`ApplyLayerEffect` 的 `GaussianBlur` 分支：`static_cast<const GaussianBlurEffect &>`，调 `Apply`。

- [ ] **Step 4: 跑测试确认通过**

跑 Task 6+7 全部新用例 + `TgfxRenderAdapterTest.*`。Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git commit --only adapter/tgfx/src/effects/GaussianBlurFilter.h \
  adapter/tgfx/src/effects/GaussianBlurFilter.cpp \
  adapter/tgfx/src/TgfxCanvasAdapter.cpp \
  adapter/tgfx/tests/TgfxRenderAdapterTest.cpp \
  docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Add gaussian blur to the layer effect chain."
```

---

### Task 8: Bridge + 文档

**Status:** 待开始

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_layer.cpp`
- Modify: `bridge/src/common/motionstudio_bridge_commands.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Bridge/MotionStudioBridgingExtension.swift`
- Modify: `docs/data-model.md`、`docs/rendering.md`、`docs/pag-runtime-filter.md`、`docs/README.md`
- Modify: `docs/superpowers/specs/2026-08-16-layer-effects-design.md`（状态改为已实现）

**Interfaces:**
- Produces: `MS_EFFECT`（`INVALID=-1`、`BRIGHTNESS_CONTRAST=0`、`GAUSSIAN_BLUR=1`）
- Produces: `ms_layer_effect_count` / `type_at` / `enabled_at`
- Produces: `ms_command_add_brightness_contrast_effect` / `add_gaussian_blur_effect` / `remove_layer_effect` / `move_layer_effect` / `set_layer_effect_enabled` / `set_gaussian_blur_repeat_edge`

- [ ] **Step 1: 写失败测试**

`bridge/tests/BridgeTest.cpp`：建 shape 层，`add_gaussian_blur` 后 count==1、type==`GAUSSIAN_BLUR`、enabled true；`set_enabled(false)` 后 false；`add_brightness_contrast` count==2；`move 0→1`；`remove 0`；undo 各一步回到前态（走 document revision / 再查 count）。缺层返回 0 / `INVALID`。

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='*Effect*'
```

Expected: 编译失败。

- [ ] **Step 3: 实现 Bridge + Swift 扩展 + 文档**

查询放 `motionstudio_bridge_layer.cpp`，对标 `ms_layer_style_*`。命令放 `motionstudio_bridge_commands.cpp`，对标 `ms_command_add_fill_style`：`Execute(document, make_unique<AddLayerEffectCommand>(...))`。

`MotionStudioBridgingExtension.swift`：

```swift
extension MS_EFFECT: @retroactive CaseIterable, @retroactive Identifiable {
    public static var allCases: [MS_EFFECT] { [.BRIGHTNESS_CONTRAST, .GAUSSIAN_BLUR] }
    public var id: Int32 { rawValue }
}
```

文档按 spec §6 补 `Layer::effects[]`、isolation 条件、`endLayer` 链、GaussianBlur `Apply` 约定。spec 状态改为「已实现」。

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target bridge_test core_tests tgfx_adapter_test
ctest --test-dir build --output-on-failure -E benchmark
```

Expected: 全绿。

- [ ] **Step 5: Commit**

```bash
git commit --only bridge/include/motionstudio_bridge.h \
  bridge/src/common/motionstudio_bridge_layer.cpp \
  bridge/src/common/motionstudio_bridge_commands.cpp \
  bridge/tests/BridgeTest.cpp \
  apps/MotionStudioApp/MotionStudioApp/Bridge/MotionStudioBridgingExtension.swift \
  docs/data-model.md docs/rendering.md docs/pag-runtime-filter.md docs/README.md \
  docs/superpowers/specs/2026-08-16-layer-effects-design.md \
  docs/superpowers/plans/2026-08-16-layer-effects.md \
  -m "Expose layer effects on the bridge and document the pipeline."
```

---

## Spec 覆盖对照

| Spec | Task |
|---|---|
| `LayerEffect` 多态 + `snapshot` | 1 |
| 序列化 / 未知 type / PropertyPath | 2 |
| Undo 五命令 | 3 |
| `EvaluatedLayer.effects` / Precomp 忽略 | 4 |
| `EndLayer` 带 `shared_ptr` / isolation 条件 | 5 |
| opacity/blend composite、Picture→Image、BC | 6 |
| GaussianBlur、链序、mask 后渗出 | 7 |
| Bridge / `MS_EFFECT` / 文档 | 8 |
| 不做 Inspector、导出、组级 isolation | 全局约束（无 Task） |
