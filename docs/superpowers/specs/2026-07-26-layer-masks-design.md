# Layer Masks + Track Matte — 设计说明

日期：2026-07-26  
状态：实现中（Core / Bridge 已合入分支；SwiftUI 待人工验证）
分支：`feature/0x1306a94_layer_masks`

## 实现进度


| 里程碑                             | 状态      | 说明                                                             |
| ------------------------------- | ------- | -------------------------------------------------------------- |
| 1. 模型 + 序列化 + PropertyPath      | **已完成** | `Animatable` path、feather/expansion、TrackMatte 字段；不升 schema    |
| 2. SceneEvaluator / SceneState  | **已完成** | `EvaluatedMask`、matte 解析、`usedAsMatteOnly`                     |
| 3. DrawCommand + CommandBuilder | **已完成** | `BeginLayer/EndLayer/BeginMask/EndMask/DrawMaskPath`           |
| 4. tgfx 离屏 PathCoverage         | **已完成** | Picture 隔离 + MaskFilter；Add / feather 快照测试通过                   |
| 5. Track Matte 端到端（渲染）          | **已完成** | Alpha matte 快照测试通过；Luma/反相走同一 coverage 路径                      |
| 6. Bridge + Undo                | **已完成** | 增删/移动 mask、mode/inverted、set track matte；undo + bridge_test    |
| 7. SwiftUI 编辑 UI                | **已完成** | Inspector Masks + Track Matte + 时间轴；Add Mask 形状烘焙 |
| 8. 选中层 mask path 描边          | **已完成** | 通用 `PathOverlay`；钢笔编辑后续 |


提交策略：Core / 适配器 / 测试 / 文档可自动提交；含 UI 交互的改动等人工验证后再提交。

## 目标

完整支持 After Effects 常用遮罩能力：

1. **Path Masks**：每层多条路径遮罩，模式 Add / Subtract / Intersect，inverted、opacity、feather、expansion；path 可动画。
2. **Track Matte**：Alpha / Alpha Inverted / Luma / Luma Inverted；可指定同 composition 内任意层为 matte 源。
3. **UI**：创建/编辑 mask、设置 track matte（与 core 同里程碑交付）。

采用 **统一离屏 coverage 管线**：内容进离屏 → path masks / track matte 合成 coverage → 调制后画回父级。

## 现状


| 层级                                                      | 能力                                               |
| ------------------------------------------------------- | ------------------------------------------------ |
| `Layer::masks` + `MaskMode`                             | 模型字段已有；path 为静态 `BezierPath`；无 feather/expansion |
| 序列化 masks                                               | 已有（静态 path）                                      |
| `DrawCommandType::ClipPath` / `RenderAdapter::clipPath` | 已有；CommandBuilder **未**对 masks 发指令               |
| Track Matte                                             | 无                                                |
| 编辑 UI                                                   | 无                                                |




## 非目标

- Mask 模式 Lighten / Darken / Difference
- Set Matte 效果、跨 composition 引用 matte
- schema 版本升级 / 旧文档迁移（开发阶段，直接改当前格式）
- Lottie 导出 masks/matte（另里程碑；导出仍走模型直转）

---



## §1 数据模型



### Path Masks

```cpp
struct Mask {
    Animatable<BezierPath> path;     // 原静态 BezierPath → 可动画
    MaskMode mode = MaskMode::Add;   // Add / Subtract / Intersect
    Animatable<float> opacity{1};
    bool inverted = false;
    Animatable<float> feather{0};    // 羽化半径 px（layer 局部）
    Animatable<float> expansion{0};  // 扩张/收缩 px（可负）
};
```

`Layer::masks`：按数组顺序合成 coverage。

### Track Matte

```cpp
enum class TrackMatteType {
    None,
    Alpha,
    AlphaInverted,
    Luma,
    LumaInverted,
};

// on Layer:
EntityId trackMatteLayerId;   // 无效 = 无
TrackMatteType trackMatteType = TrackMatteType::None;
```

约定：

- Matte 源层仍在树中，但作为 matte 时 **不单独绘制到最终画面**。
- 可指定任意同 composition 层；无效 / 自引用 / 找不到 → 视为无 matte。
- Path masks 与 Track Matte 可同时存在：先 path masks，再与 matte coverage **Intersect**。



### 序列化 / PropertyPath

- **不升 schemaVersion**；直接改 JSON 字段。
- Mask：`path` 改为与其它 Animatable 相同的 `{static|keyframes}` 形态；新增 `feather` / `expansion`。
- Layer：新增 `trackMatteLayerId`、`trackMatteType`。
- PropertyPath：`masks[i].path|opacity|feather|expansion`。

---



## §2 求值与 DrawCommand



### SceneState

```cpp
struct EvaluatedMask {
    BezierPath path;
    MaskMode mode;
    float opacity;
    bool inverted;
    float feather;
    float expansion;
};

enum class EvaluatedMatteKind { None, Alpha, AlphaInverted, Luma, LumaInverted };

struct EvaluatedLayer {
    // ...existing...
    std::vector<EvaluatedMask> masks;
    EvaluatedMatteKind matteKind = EvaluatedMatteKind::None;
    EntityId matteSourceId;
    bool usedAsMatteOnly = false;  // 被引用为 matte 且不单独上屏
};
```

`SceneEvaluator`：求值 masks；解析 track matte；标记 `usedAsMatteOnly`。

### DrawCommand

新增：

```cpp
enum class DrawCommandType {
    // existing...
    BeginLayer,
    EndLayer,
    BeginMask,   // payload: MaskApplyMode
    EndMask,
};

enum class MaskApplyMode {
    PathCoverage,
    AlphaMatte,
    AlphaMatteInverted,
    LumaMatte,
    LumaMatteInverted,
};
```

有 mask/matte 的层：

```
Save → ConcatTransform → SetOpacity → SetBlendMode
BeginLayer
  // shapeItems
BeginMask(PathCoverage) … EndMask     // 若有 masks
BeginMask(AlphaMatte/…) … EndMask     // 若有 track matte（重放源层，相对变换）
EndLayer
Restore
```

硬边且无 feather、opacity 均为 1、无 matte 时，允许降级为 `ClipPath`（优化，非正确性依赖）。

坐标：mask path 为 layer 局部；matte 源用 `matteWorld * inverse(targetWorld)` 画进当前离屏空间。

---



## §3 适配器（tgfx）

在 `TgfxCanvasAdapter` 实现离屏栈：

- `BeginLayer`：分配/压入离屏 surface（或等价 `saveLayer`）
- 内容绘制进当前离屏
- `BeginMask`/`EndMask`：另开 coverage 缓冲；PathCoverage 内绘制白色 path（expansion → stroke/offset 近似或 path 扩张；feather → blur）；按 mode 与 inverted/opacity 累积；Matte 模式按 alpha/luma/反相写入 coverage
- `EndLayer`：用 coverage 作 mask filter 合成到父级

离屏/上屏适配器共享基类实现。无 mask 的层保持现有直接绘制路径。

---



## §4 UI / Bridge

- Bridge：增删改 mask；设 `trackMatteLayerId` / `trackMatteType`；PropertyPath 读写已有通道复用
- Undo：mask 增删与属性变更走现有 Command / SetStaticValue / Keyframe；track matte 用专用小命令或 Set 层字段命令
- Inspector：Masks 列表（mode / inverted / opacity / feather / expansion）；Track Matte 选择层 + 类型
- **Add Mask 默认 path**：按当前 playhead **烘焙**层形状几何（Rect/Ellipse/Path → `BezierPath` 快照）；非 Shape 层回退 200×200 中心矩形。与形状之后**不联动**（AE 语义）。
- 画布：选中层显示 mask 路径描边（通用 `PathOverlay`；编辑 / 钢笔后续）
- 时间轴：mask 可动画属性出现在属性子行（与 transform 一致）

---



## §5 测试

- 模型 / 序列化 round-trip：animatable path、feather、expansion、track matte 字段
- CommandBuilder：有/无 mask、matte-only 源层跳过、相对变换
- 适配器快照：Add/Subtract/Intersect、inverted、opacity、feather、Alpha/Luma matte
- PropertyPath + undo 一致性

---



## 里程碑顺序

1. 模型 + 序列化 + PropertyPath
2. SceneState / Evaluator / CommandBuilder（指令形态）
3. RenderAdapter + tgfx 离屏 coverage
4. Track Matte 端到端
5. Bridge + Undo + UI
6. 快照测试补齐与硬边 ClipPath 快路径（可选）

---



## §6 手动验证（UI）

示例图层树（下文步骤均按此）：

```
Composition
  Ellipse
  Rectangle
```

两种能力不要混验：验 Path Mask 前先把 Track Matte 设为 None；验 Track Matte 前可先删掉本层 Masks。

### 6.1 Track Matte（用另一层裁切）

目标：用 **Rectangle** 裁切 **Ellipse**。

1. 选中 **Ellipse**（被裁切层）。
2. Inspector → **Track Matte**：
  - Type → **Alpha**
  - Source → **Rectangle**
3. 预期：
  - Ellipse 只在 Rectangle 覆盖区域内显示。
  - Rectangle 作为 matte 源后**不再单独绘制**（`usedAsMatteOnly`）。
4. 可选：Type 改为 **Alpha Inverted** → 显示区域对调（挖空矩形区域）。
5. **Luma** 说明（可选）：
  - Luma 看 matte 源的**颜色亮度**，不是 alpha。
  - 默认形状填充是彩色（非纯白），亮度常约 0.5，Ellipse 会像半透明——**符合预期**。
  - 实色形状层硬裁切请用 **Alpha**；或把 Rectangle 填成纯白再用 Luma。
6. Type → **None** 清除；Undo 可恢复。


| 目标                  | 选中谁     | 设置                                     |
| ------------------- | ------- | -------------------------------------- |
| Rectangle 裁 Ellipse | Ellipse | Track Matte → Alpha，Source = Rectangle |
| Ellipse 自己裁自己       | Ellipse | Masks → `+`（见下节）                       |




### 6.2 Path Masks（本层路径遮罩）

目标：在 **Ellipse** 上加路径遮罩并看到裁切。  
默认 `Masks +` 会把**当前 playhead 下的层外形**烘焙成 mask path（椭圆→椭圆 path，矩形→矩形 path）。Mode=Add 且未改 Expansion 时，画面可能几乎不变——属正常；用 Expansion / Feather / Inv / Subtract 验证。

1. 选中 **Ellipse**；Track Matte → **None**。
2. Inspector → **Masks** → `+`（path 应为椭圆外形快照，非固定方框）。
3. **Expansion** → **-40**（或更负）→ Ellipse 边缘被明显裁小。
  - 正数 = 扩张 mask；若 mask 已完全盖住图层内容，正数**看不出变化**（先缩再扩可验证）。  
  - Inv 按钮选中时应有高亮描边/底色。
4. 勾选 **Inv** → 方框外/挖空类效果；再点取消 → 恢复，按钮高亮同步消失。
5. **Feather** → **20~40** → 边缘变软；再改回 **0** → 硬边恢复。
  - 若该属性已有关键帧，改值必须打在当前时间关键帧上（菱形应立即高亮）。
6. Mask **Opacity** → **0.3** → 裁切区半透（不是整层 opacity）。
7. 可选：再 `+` 第二条；Mode → **Subtract** / **Intersect**，配合不同 Expansion，确认挖洞或取交。
8. 可选：给 Mask Opacity / Feather 打关键帧 → 时间轴出现 `Mask 1 Opacity` 等子行。
9. 点 mask 行 `-` 删除 → 裁切消失；Undo 可恢复。

**Rectangle 层**：Path Mask 与它无关；若也要自裁切，选中 Rectangle 后同样 Masks → `+` → 调 Expansion。

### 6.3 验收清单

- [x] Track Matte Alpha：Rectangle 裁 Ellipse，源层不上屏
- [x] Track Matte 清除 / Undo

- [x] 可选Luma：彩色源变淡；纯白源接近不透明

- [x] Path Mask：Expansion 负值可见裁小
- [x] Path Mask：Inv / Feather / Opacity
- [x] Path Mask：删除与 Undo
- [x] 可选 时间轴 mask 属性关键帧子行