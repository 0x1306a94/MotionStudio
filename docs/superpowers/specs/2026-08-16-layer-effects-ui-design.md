# Layer Effects UI（Inspector + 时间轴）— 设计说明

日期：2026-08-16  
状态：草稿  
前置：[Layer Effects](2026-08-16-layer-effects-design.md)（模型 / 求值 / adapter / Bridge 命令已落地）  
相关：[bridge-swift-enums](../../../.claude/rules/bridge-swift-enums.md)

## 目标

在已有 Bridge 上补齐编辑入口：选中叶子层后能增删改 effect，打了关键帧后时间轴展开对应行。

1. Inspector 增加 `Effects` 段（加 / 删 / 上下移 / 启用 / 数值 / Repeat Edge）。
2. 时间轴对已有关键帧的 `effects[i].*` 路径展开子行。
3. 复用现有 Fill / Mask 列表心智与 `NumberPropertyRow`，不抽通用列表组件。

## 非目标

- 新 effect 类型、Group / Precomp 组级 isolation 或 Effects UI
- Lottie / PAG 导出 Effect
- 命中 / 选中框随 blur 扩张
- 改现有 add / remove / move / set 命令语义
- 升 `schemaVersion`
- Inspector / 时间轴自动化 UI 测试（项目里没有这类 harness）

## 已锁定决策

| 项 | 选择 |
|---|---|
| 范围 | Inspector + 时间轴关键帧行 |
| 出现条件 | 仅 `MS_LAYER` 为 Shape / Image / Text；Group / Precomp 不显示段、不展开时间轴行 |
| 列表顺序 | 与 Fill / Mask 相同：**倒序**。最上 = 数组最后一项 = **最后生效** |
| 添加 | 段头一个 `+`，`Menu` 选类型；调用已有两条 add 命令（append） |
| 启用 | 每行一个 `Toggle`；关了参数仍可见可改，渲染跳过 |
| Repeat Edge | Gaussian Blur 行内一个 `Toggle`；文案 `Repeat Edge` |
| 行标题 | `MS_EFFECT.label`（Brightness Contrast / Gaussian Blur），**不编号**；同类型多条靠列表位置区分 |
| Inspector 位置 | `TrackMatteInspector` 之后（内容 → mask/matte → effect） |
| 数值控件 | `NumberPropertyRow` + 菱形关键帧；路径 `effects[i].brightness\|contrast\|blurriness` |
| 默认值 | 沿用 Core 构造：BC `0/0`，Blur `0` + `repeatEdgePixels=false`。加上去先是恒等，画面不变 |
| 时间轴 | 只展开**已有关键帧**的可动画路径；`enabled` / `repeatEdgePixels` 不上时间轴 |
| 禁用的 effect | 时间轴仍显示其关键帧行 |
| 实现结构 | 新建 `EffectsInspector`，不塞进 Image/Text/Shape 各自 Inspector，不抽通用列表 |
| Bridge | 命令不改；**补一条查询** `ms_layer_effect_repeat_edge_at`（现有 API 只能写、不能读） |

---

## §1 Inspector

新建 `apps/MotionStudioApp/MotionStudioApp/Inspector/EffectsInspector.swift`。  
Xcode 工程是 `PBXFileSystemSynchronizedRootGroup`，放入 `Inspector/` 即可被编进 target。

`InspectorView` 在选中层且 `layerType` ∈ `{.SHAPE, .IMAGE, .TEXT}` 时，于 `TrackMatteInspector` 之后插入：

```
EffectsInspector(core:layerID:isEditable:perform:)
```

`isEditable` 与其它段相同：可见且未锁定。订阅 `core.panelRevision`。

### 段头

```
HStack { Text("Effects"); Spacer(); Menu { … } label: { Image(systemName: "plus") } }
```

菜单项 = `MS_EFFECT.allCases`（不含 `.INVALID`），label 用已有 `MS_EFFECT.label`：

- Brightness Contrast → `core.addBrightnessContrastEffect`
- Gaussian Blur → `core.addGaussianBlurEffect`

`perform` 文案：`Add Brightness Contrast` / `Add Gaussian Blur`。

### 列表

```
indices = Array((0 ..< core.effectCount(layerID:)).reversed())
```

每行（`index` 是数组下标，不是视觉序号）：

```
VStack {
  HStack {
    Text(type.label)
    Toggle("", isOn: enabledBinding)   // labelsHidden
    Button chevron.up / chevron.down
    Button minus
  }
  switch type {
  case .BRIGHTNESS_CONTRAST:
    floatRow("Brightness", .brightness)
    floatRow("Contrast", .contrast)
  case .GAUSSIAN_BLUR:
    floatRow("Blurriness", .blurriness)
    Toggle("Repeat Edge", isOn: repeatEdgeBinding)
  case .INVALID:
    不渲染该行
  }
}
```

上下移与 Stroke 相同：

- 视觉上移 = 与**更大**数组下标的邻居交换（更晚应用，列表位置更靠上）
- `canMove` 在倒序列表两端为 false
- `perform("Move Effect")` → `core.moveLayerEffect(from:to:)`

删除：`perform("Remove Effect")` → `core.removeLayerEffect`。

启用：`perform("Set Effect Enabled")` → `core.setLayerEffectEnabled`。  
Repeat Edge：仅当 `type == .GAUSSIAN_BLUR`；`perform("Set Repeat Edge")` → `core.setGaussianBlurRepeatEdge`。非 Blur 或越界查询视为 `false`。

`floatRow` 照抄 `MasksInspector.floatPropertyRow`：有 playhead 关键帧则 `addKeyframeFloat`，否则 `setStaticFloat`，提交后 `endMergeGroup`。

---

## §2 时间轴

在 `TimelineSupport.swift`：

1. 新增 `timelineEffectTracks(core:layerID:)`，形态对齐 `timelineMaskTracks`。
2. `timelineAnimatedPropertyPaths` 与 `buildTimelineRows` 都挂上这些 track。

仅当层类型为 Shape / Image / Text 时收集。对每个 `index`：

| 类型 | 路径 | 行 label |
|---|---|---|
| BC | `effects[i].brightness` | `Brightness Contrast Brightness` |
| BC | `effects[i].contrast` | `Brightness Contrast Contrast` |
| Blur | `effects[i].blurriness` | `Gaussian Blur Blurriness` |

只追加 `keyframeFrames` 非空的路径。`TimelineRow.id` 仍是 `(layerID, path)`，同类型多条不靠 label 区分。

不改时间轴单元格绘制；现有 `keyframeTrack` / `propertySpan` 分支按路径读写即可。

---

## §3 App 封装

### `EffectProperty`（`PropertyPath.swift`）

```swift
enum EffectProperty: String, CaseIterable {
    case brightness, contrast, blurriness

    func path(at index: Int) -> String { "effects[\(index)].\(rawValue)" }

    var actionLabel: String { /* Brightness / Contrast / Blurriness */ }
}
```

### `MotionDocumentCore`

薄包装，一律 `changed()`：

| 方法 | Bridge |
|---|---|
| `effectCount` | `ms_layer_effect_count` |
| `effectType(layerID:index:)` | `ms_layer_effect_type_at` |
| `effectEnabled(layerID:index:)` | `ms_layer_effect_enabled_at` |
| `gaussianBlurRepeatEdge(layerID:index:)` | **新** `ms_layer_effect_repeat_edge_at` |
| `addBrightnessContrastEffect` | `ms_command_add_brightness_contrast_effect` |
| `addGaussianBlurEffect` | `ms_command_add_gaussian_blur_effect` |
| `removeLayerEffect` | `ms_command_remove_layer_effect` |
| `moveLayerEffect` | `ms_command_move_layer_effect` |
| `setLayerEffectEnabled` | `ms_command_set_layer_effect_enabled` |
| `setGaussianBlurRepeatEdge` | `ms_command_set_gaussian_blur_repeat_edge` |

数值读写走现有 `evaluateFloat` / `setStaticFloat` / `addKeyframeFloat` / `removeKeyframe`。

---

## §4 Bridge 补丁（仅查询）

现有命令集不变。增加：

```c
// 非 Gaussian Blur 或越界 → false
bool ms_layer_effect_repeat_edge_at(MSDocument *, uint64_t layerId, int index);
```

实现：解析 layer → `effects[index]` → `type() == GaussianBlur` 后 `static_cast` 读 `repeatEdgePixels`。禁 `dynamic_cast`。

`BridgeTest` 补：默认 `false`；`set_gaussian_blur_repeat_edge(true)` 后为 `true`；对 BC 下标返回 `false`。

---

## §5 测试与验证

- Bridge：上节查询用例（ASan 构建下的 `bridge_test`）。
- App：无新 XCTest。实现后用 Xcode 跑 MotionStudioApp，手测：
  1. Shape / Image / Text 能加两种 effect；Group/Precomp 不可达则无需测。
  2. 改 Brightness / Blurriness，画布跟着变；`enabled` 关掉则复原。
  3. 倒序：后加的在最上；上移后更晚生效（BC→Blur 与 Blur→BC 观感不同）。
  4. 菱形打关键帧后时间轴出现对应行；undo 能撤。
  5. Repeat Edge 开关能写回（边缘渗出 vs 钳制）。

---

## §6 错误与边界

- `type_at` 为 `.INVALID`：跳过该行，不崩溃。
- 锁定 / 隐藏层：整段 `disabled`，与 Fill/Mask 相同。
- 恒等默认值不是 bug：用户改数值后才有画面变化。
- 不在 Swift 侧再镜像一套 effect 枚举。

---

## 里程碑

1. Bridge 查询 + `BridgeTest` + `MotionDocumentCore` / `EffectProperty`
2. `EffectsInspector` + 挂进 `InspectorView`
3. `timelineEffectTracks` 挂进时间轴
4. 手测清单

提交：每里程碑一次；Swift 改动需人工点 UI，不阻塞 Core/Bridge commit。
