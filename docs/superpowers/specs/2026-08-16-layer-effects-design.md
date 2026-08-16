# Layer Effects（图层后处理链）— 设计说明

日期：2026-08-16  
状态：已实现  
前置：[PAG RuntimeFilter](2026-08-13-pag-runtime-filter-design.md)（adapter 滤镜基类 + BrightnessContrast 已落地）  
相关：[rendering.md](../../rendering.md)、[data-model.md](../../data-model.md)、[pag-runtime-filter.md](../../pag-runtime-filter.md)、[Layer Masks](2026-07-26-layer-masks-design.md)

## 目标

把 effect 做成图层的**后处理链**：先把该层全部 fill/stroke/image/text 离屏合成，再按 `effects[]` 顺序做 `Image → Image`，最后用图层 opacity / blend 叠回父画布。

1. Core 模型：`Layer::effects[]`，与 `styles[]` 并列、职责不同。
2. 求值进 `SceneState`，`CommandBuilder` 在有 effect 时复用 `BeginLayer` / `EndLayer`。
3. `TgfxCanvasAdapter::endLayer` 把 isolation Picture 转 Image，再链式 Apply。
4. 首版两个滤镜：**BrightnessContrast**（已有 GPU）+ **GaussianBlur**（证明 bounds 扩张）。

## 非目标

- Precomp / Group 的组级 isolation（预合成仍拍平；挂在它们上的 effect **静默忽略**）
- SwiftUI Inspector / 时间轴 effect 行（已另开：[Layer Effects UI](2026-08-16-layer-effects-ui-design.md)）
- AE Layer Style → [Layer Styles](2026-08-16-layer-styles-design.md)（Drop Shadow / Outer Glow / Stroke；Gradient Overlay 另开）
- 其余 PAG Effect（RadialBlur / Glow / HueSaturation / Mosaic / DisplacementMap / …）
- Lottie / PAG 导出写入 Effect 块
- 改 `ColorSourceEffect`（过程色 fill，不是后处理）
- 命中检测 / 选中框随 blur 扩张（仍用未滤几何 bounds）
- 升 `schemaVersion`

## 已锁定决策

| 项 | 选择 |
|---|---|
| 作用对象 | 仅 Shape / Image / Text **叶子层** |
| 与 fill/stroke | 不平行：先画完全部 style，再滤整张结果 |
| 多 effect | 数组序链式：`effects[0]` 输出 = `effects[1]` 输入 |
| 离屏 | 复用 `BeginLayer` / `EndLayer`，不新开 Effects 命令对 |
| 相对 mask | **先 mask / track matte，再 effect**（模糊可渗出遮罩） |
| opacity / blend | isolation 内按 opacity 1、style 自身 blend 画；**composite 时**用图层 opacity / blend |
| 无 effect 且无 mask | 直画，不离屏 |
| 恒等参数 | 求值时丢掉：BC `brightness==0 && contrast==0`；Blur `blurriness<=0`；全被丢掉则当无 effect |
| `!enabled` | 求值跳过 |
| 首版类型 | `BrightnessContrast`、`GaussianBlur`（PAG 文件枚举仍是 `FastBlur`；GPU 类与对外名是高斯模糊，我们跟后者） |
| GaussianBlur 方向 | 仅双向；不做 Horizontal / Vertical |
| GaussianBlur `repeatEdgePixels` | 静态 `bool`，默认 `false`（扩 bounds）；`true` 时 Clamp 并裁回原 bounds |
| Blur 半径 | 与 PAG 一致：`ImageFilter::Blur(blurriness/2, blurriness/2)` |
| BC 数值 | Core 存 AE 风格 float（约 ±100）；映射仍在 adapter |
| 未知 type | 反序列化**跳过该项**（向前兼容），不整篇失败 |
| 求值快照 | 复用 `LayerEffect` 多态：`snapshot(time)` bake 成静态值，`shared_ptr<const LayerEffect>`；**不**引入 `EvaluatedEffect` / `variant` / 拍平 struct |
| 分发 | `type()` + `static_cast`（与 Fill/Stroke 相同）；禁 `dynamic_cast` |
| 命中 / outline | 不随滤镜改 |
| schema | 不升版；`effects` 缺省 = `[]` |

---

## §1 数据模型

`styles[]` 仍是内容绘制（Fill / Stroke）。`effects[]` 是后处理，**不要**扩 `LayerStyleType`。

```cpp
enum class LayerEffectType {
    BrightnessContrast,
    GaussianBlur,
};

class LayerEffect {
  public:
    explicit LayerEffect(LayerEffectType type);
    virtual ~LayerEffect() = default;
    LayerEffectType type() const;

    EntityId id = EntityId::Generate();
    bool enabled = true;

    // Bake 可动画字段为 `time` 的静态值。`!enabled` 或恒等参数 → nullptr。
    virtual std::shared_ptr<const LayerEffect> snapshot(PreviewTime time) const = 0;

  private:
    LayerEffectType type_;
};

class BrightnessContrastEffect : public LayerEffect {
  public:
    BrightnessContrastEffect();
    Animatable<float> brightness{0};
    Animatable<float> contrast{0};
    std::shared_ptr<const LayerEffect> snapshot(PreviewTime time) const override;
};

class GaussianBlurEffect : public LayerEffect {
  public:
    GaussianBlurEffect();
    Animatable<float> blurriness{0};
    bool repeatEdgePixels = false;
    std::shared_ptr<const LayerEffect> snapshot(PreviewTime time) const override;
};

class Layer {
    // 现有 styles / masks / …
    std::vector<std::unique_ptr<LayerEffect>> effects;
};
```

不进 `EntityIndex`（与 `LayerStyle` 相同）。命令持 `layerId` + index。

添加：**append**。后加的 effect 后执行（叠在链尾）。调序任意 index，无 Fill/Stroke 那种同类区间限制。

Precomp / Group 上的 `effects[]` 可持久化，求值与绘制**不读**。

### 序列化 / PropertyPath

`document.json` 图层可选字段 `effects`（数组）。缺省或非数组 → 空。`schemaVersion` 保持 1。

```json
"effects": [
  {
    "id": "…",
    "type": "brightnessContrast",
    "enabled": true,
    "brightness": { "static": 0 },
    "contrast": { "static": 0 }
  },
  {
    "id": "…",
    "type": "gaussianBlur",
    "enabled": true,
    "blurriness": { "static": 8 },
    "repeatEdgePixels": false
  }
]
```

- `type` 字符串：`brightnessContrast` / `gaussianBlur`；未知 → 跳过该项。
- `enabled` 缺省 `true`。
- `repeatEdgePixels` 缺省 `false`。
- 可动画字段形态与现有 `Animatable` 相同。

PropertyPath（走已有 `SetStaticValue` / 关键帧命令）：

- `effects[i].brightness`
- `effects[i].contrast`
- `effects[i].blurriness`

`enabled` / `repeatEdgePixels` 用专用小命令，不进 PropertyPath。

---

## §2 求值与 DrawCommand

### SceneState

不另建 `EvaluatedEffect`。求值结果就是 bake 过的 `LayerEffect` 子类：新类型只加模型子类 + adapter 一处 `static_cast`，没有中央 variant / 拍平字段可膨胀。

`shared_ptr` 让 `SceneState` 仍可拷贝（跨线程只加引用计数）。快照与 Document 脱钩：改关键帧不影响已求值帧。

```cpp
struct EvaluatedLayer {
    // 现有字段…
    std::vector<std::shared_ptr<const LayerEffect>> effects;
};
```

`FillCommonLayerFields` 里求值（Image / Text / Shape 共用）：

```
for (effect in layer.effects)
    if (auto snap = effect->snapshot(time))
        evaluated.effects.push_back(std::move(snap));
```

`snapshot` 内部：`!enabled` 或恒等（BC 两值皆 0，Blur `blurriness<=0`）→ `nullptr`。否则 `make_shared` 子类，可动画字段写成该帧静态值，拷贝 `id` / `repeatEdgePixels` 等。

Precomp 仍在 `FillCommonLayerFields` 之前展开子层并 `return`，不会带上预合成自己的 effect。

### 指令

不新增 `ApplyEffect` 类型。`EndLayer` 携带本层快照指针（浅拷贝）。

```cpp
struct DrawCommand {
    // 现有字段…
    std::vector<std::shared_ptr<const LayerEffect>> effects;  // EndLayer
};
```

```cpp
class RenderAdapter {
    virtual void endLayer(
        const std::vector<std::shared_ptr<const LayerEffect>> &effects) = 0;
};
```

`PlayCommands`：`adapter.endLayer(command.effects)`。实现方目前只有 `TgfxCanvasAdapter`。

`CommandBuilder` 读的是 **`EvaluatedLayer`**（已丢掉 `!enabled` / 恒等项），不是 `Layer::effects`：

```
needsIsolation =
    !evaluated.masks.empty()
    || evaluated.trackMatteType != None
    || !evaluated.effects.empty()

Save
ConcatTransform
SetOpacity(evaluated.opacity)
SetBlendMode(evaluated.blendMode)
[BeginLayer]                    // needsIsolation
  AppendShapeItems / Image / Text
  [masks / track matte]
[EndLayer(evaluated.effects)]
Restore
```

图层 `SetOpacity` / `SetBlendMode` 仍发在 `BeginLayer` **之前**。adapter 在 `beginLayer` 里快照并清掉，避免烧进内容图。模型里只有恒等 effect 时，`evaluated.effects` 为空 → 不离屏。

---

## §3 适配器

### isolation 状态

`IsolationLayer` 增加 `savedBlendMode`。`beginLayer`：

1. 快照 `opacity_`、`blendMode_`。
2. 将二者重置为 `1` / `Normal`，再 `beginRecording`。

`beginMask` 现有的 opacity 快照保持：mask 录制仍用 1。

任意 isolation（含仅 mask）都在 **composite** 时用快照的图层 opacity / blend。仅 mask 层的透明度从「画内容时预乘」改为「叠回时再乘」，与 AE 一致。

### `endLayer(effects)`

```
content = finishRecordingAsPicture()
parent = drawingCanvas()

若 effects 为空：
    现路径：drawPicture(content, maskFilter from coverages)
    paint.alpha / blendMode = 快照值
    pop isolation；return

否则：
    image = PictureToImage(content, contentBounds)   // 空 bounds 或失败 → 退回现路径（不滤）
    若有 coverages：把 coverage 打成 MaskFilter，画到同尺寸中间图（mask 在滤镜前）
    offset = 0
    for e in effects:
        image = ApplyLayerEffect(*e, image, cache, &delta)
        offset += delta
    parent->drawImage(image) 于 contentBounds.origin + offset
    paint 用快照 opacity / blend
    pop isolation
```

`PictureToImage`：对齐 PAG `CreatePictureImage`——`contentBounds.roundOut()`，`Image::MakeFrom(picture, w, h, Translate(-x,-y))`。

`ApplyLayerEffect`：`switch (effect.type())` + `static_cast`（与 `ApplyLayerStyles` 相同）。快照已 bake，读静态值用 `evaluate(0)`（或等价的 static getter）。

| `type()` | 实现 |
|---|---|
| `BrightnessContrast` | `BrightnessContrastFilter::Apply`（`brightness` / `contrast`） |
| `GaussianBlur` | 新建 `GaussianBlurFilter::Apply`：`ImageFilter::Blur(b/2, b/2)`；`repeatEdgePixels` 时 `TileMode::Clamp` 且 clip 回输入 WH（与 PAG `GaussianBlurFilter` 相同） |
| 未识别 | 跳过该 effect，继续链 |

`ColorSourceEffect` 不参与此链。

### GaussianBlur 文件

```
adapter/tgfx/src/effects/GaussianBlurFilter.{h,cpp}
```

不是 `RuntimeFilter` 子类（走 tgfx 内置 Blur）。`Apply` 签名与 BC 同构：`(input, cache, blurriness, repeatEdgePixels, offset)`；`cache` 本滤镜可不用，为调用方统一仍传入。

---

## §4 Undo / Bridge

### Undo

| 命令 | 行为 |
|---|---|
| `AddLayerEffectCommand` | append；undo 按 `id` 移除 |
| `RemoveLayerEffectCommand` | 按下标移除并持有对象；undo 插回原下标 |
| `MoveLayerEffectCommand` | `fromIndex` → `toIndex`；可 merge（与 `MoveLayerStyle` 相同规则） |
| `SetLayerEffectEnabledCommand` | 改 `enabled` |
| `SetGaussianBlurRepeatEdgeCommand` | 改 `repeatEdgePixels` |

可动画参数走现有 `SetStaticValue` / 关键帧。`CommandKind` 增加对应项。

### Bridge

对标 style：

```c
typedef CF_CLOSED_ENUM(int32_t, MS_EFFECT) {
    MS_EFFECT_BRIGHTNESS_CONTRAST = 0,
    MS_EFFECT_GAUSSIAN_BLUR = 1,
};

int ms_layer_effect_count(MSDocument *, uint64_t layerId);
MS_EFFECT ms_layer_effect_type_at(MSDocument *, uint64_t layerId, int index);
bool ms_layer_effect_enabled_at(MSDocument *, uint64_t layerId, int index);

void ms_command_add_brightness_contrast_effect(MSDocument *, uint64_t layerId);
void ms_command_add_gaussian_blur_effect(MSDocument *, uint64_t layerId);
void ms_command_remove_layer_effect(MSDocument *, uint64_t layerId, int index);
void ms_command_move_layer_effect(MSDocument *, uint64_t layerId, int fromIndex, int toIndex);
void ms_command_set_layer_effect_enabled(MSDocument *, uint64_t layerId, int index, bool enabled);
void ms_command_set_gaussian_blur_repeat_edge(MSDocument *, uint64_t layerId, int index, bool repeat);
```

Swift `CaseIterable` / `Identifiable` 扩展按 [bridge-swift-enums](../../../.claude/rules/bridge-swift-enums.md)。Inspector / 时间轴见 [Layer Effects UI](2026-08-16-layer-effects-ui-design.md)。

---

## §5 测试

### Core（`core_tests`）

| 用例 | 断言 |
|---|---|
| 求值跳过 | `!enabled`、BC 0/0、Blur `<=0` 不进 `EvaluatedLayer.effects` |
| 求值保序 | 两个非恒等 effect 按数组序出现 |
| Precomp 忽略 | 预合成层带 effect 时，子层列表无组级 isolation、无该 effect |
| CommandBuilder 直画 | 无 effect 无 mask → 无 Begin/EndLayer |
| CommandBuilder isolation | 非空 effects → BeginLayer，内容，EndLayer.effects 条数与求值一致 |
| mask + effect | 先 mask 指令，再 EndLayer 带 effects |
| 序列化 round-trip | 两类型 + 关键帧 + `enabled` + `repeatEdgePixels`；缺 `effects` 的旧 JSON 仍能加载 |
| 未知 type | 该项跳过，其余 effect 保留 |
| undo | 增删移、enabled、repeatEdge；PropertyPath 改 brightness / blurriness |

### Adapter（`tgfx_adapter_test`）

走 `Evaluate → BuildCommands → PlayCommands`，不要只调 `Filter::Apply`。

| 用例 | 断言 |
|---|---|
| BC 改色 | 不透明色块，`brightness=100`，中心 luma 高于无 effect |
| GaussianBlur 软化 | 小色块居中，`blurriness=16`，邻域出现非零 alpha / 边缘变软 |
| 链序 | 高对比色块（半黑半白）；BC→Blur 与 Blur→BC 的 `readPixels` 不全等 |
| mask 后 blur | Add mask 小于内容；mask 外出现模糊渗出 |
| 图层 opacity | opacity 0.5 + BC，叠回后 alpha 约为内容的一半（滤镜在乘透明之前） |

不比黄金 PNG。ASan 无泄漏。

---

## §6 文档

实现时改：

- `docs/data-model.md`：`Layer::effects[]`、与 `styles[]` 的职责表
- `docs/rendering.md`：isolation 条件、EndLayer 带 effects、opacity 在 composite
- `docs/pag-runtime-filter.md`：补 `endLayer` 调用链 + GaussianBlur
- `docs/README.md`：本 spec 一行索引

---

## §7 错误与边界

- `endLayer` 时无 isolation 栈 / 无 surface：空操作（与现逻辑相同）。
- `PictureToImage` 失败：退回无滤镜的 `drawPicture`。
- 单个 `Apply` 返回 `nullptr`：中止后续 effect，退回该步之前的 image；若尚无 image 则退回 `drawPicture`。
- 禁止 `dynamic_cast` / 异常；type 分发用 `LayerEffectType` + `static_cast`。
- Group / Precomp 上的 effect：存盘保留，不渲染、不报错。

---

## 里程碑

1. 模型 + 序列化 + PropertyPath + undo
2. SceneEvaluator / CommandBuilder / `endLayer` 签名
3. adapter：isolation 快照 + Picture→Image + BC 接线
4. GaussianBlur + bounds / mask 渗出测试
5. Bridge + `MS_EFFECT`

提交：Core / adapter / 测试 / 文档可按里程碑提交；无 SwiftUI，无需等人工点 UI。

---

## 后续（明确不做）

1. Precomp / Group 组级 isolation（拍平改为「子层先离屏再滤」）
2. Inspector / 时间轴 → [Layer Effects UI](2026-08-16-layer-effects-ui-design.md)
3. RadialBlur、Glow、HueSaturation、…
4. AE Layer Style → [Layer Styles](2026-08-16-layer-styles-design.md)（Drop Shadow / Outer Glow / Stroke）
5. 导出 Effect
6. hit-test 随 blur 扩 bounds
