# 文本曲线布局（Text Path）— 设计说明

日期：2026-08-04  
状态：已落地  
实现计划：`docs/superpowers/plans/2026-08-04-text-path-layout.md`
相关：`[2026-07-30-text-layer-design.md](./2026-07-30-text-layer-design.md)`、`[2026-08-01-point-vs-box-text-design.md](./2026-08-01-point-vs-box-text-design.md)`、`[2026-07-28-follow-path-design.md](./2026-07-28-follow-path-design.md)`、`[2026-07-31-pag-export-design.md](./2026-07-31-pag-export-design.md)`

## 目标

让点文本沿用户绘制的路径排布（环形、wave 等由路径形状表达，非独立布局模式）：

1. Core 只记录文案、字体、字号与路径布局必要参数；**不做**弧长排版
2. 路径来源对齐 Follow Path：引用同 composition 内另一路径图层
3. Adapter 用 **tgfx** `PathMeasure` 实现排版，带缓存，避免每帧重复重算
4. 选中 AABB 按路径布局后的 glyph 包围盒
5. PAG 导出完整映射到 `TextPathOptions`（可编辑矢量）



## 已锁定决策


| 项           | 选择                                                                                      |
| ----------- | --------------------------------------------------------------------------------------- |
| 实现路径        | Adapter 路径排版模块 + Core 只传数据（方案 1）                                                        |
| 字段          | 完整对齐 PAG：`reversed` / `perpendicular` / `forceAlignment` / `firstMargin` / `lastMargin` |
| 文本模式        | 仅点文本；路径有效时忽略 `boxTextMode`（不强制改写存储值）                                                    |
| 坐标系         | 每帧 path 层 local → world → 文本层 local                                                     |
| 动画          | `firstMargin` / `lastMargin` 可 KF；三 bool 静态                                             |
| 超长文本        | 对齐 PAG `TextPathRender`（两端延长 / clamp，不裁切、不缩字）                                           |
| 接口收拢        | `TextDrawParams`；`drawText(const TextDrawParams &)`                                     |
| 范围          | Core + Adapter 缓存 + Bridge + AABB + App Inspector + PagExporter 路径文本                    |
| reversed 导出 | 导出 path 为「未 reverse 的文本 local 几何」；`reversedPath` = 模型 `reversed`                        |
| 路径层隐藏       | 隐藏不影响文本路径布局（求值忽略路径层 `visible`）                                                      |




## 非目标

- 框文本 + 路径、垂直文本
- 路径层自动隐藏 / 专用引导层类型（用户可手动隐藏路径层；隐藏后布局仍有效，见 §1）
- Text Animator；完整 AE tracking 对齐
- 逐 glyph 精确 hit-test（MVP 用 AABB）
- 与 PAGViewer 自动化像素回归（可手工验收）
- **不链入** libpag 渲染层 `TextPathRender` / 整库 rendering（libpag 渲染层尚无 Metal 后端；MS 预览走预编译 Metal tgfx。算法可对照参考，实现放在 MS `adapter/textlayout` + tgfx `PathMeasure`）

---



## §1 数据模型

```cpp
// include/MotionStudio/model/TextPath.h
struct TextPath {
    bool enabled = false;
    EntityId pathLayerId;              // 无效 = 未绑定
    bool reversed = false;
    bool perpendicular = true;         // 对齐 PAG 默认
    bool forceAlignment = false;
    Animatable<float> firstMargin{0.f};
    Animatable<float> lastMargin{0.f};
};

// TextContent 增加：
TextPath textPath;
```


| 项                    | 行为                                                        |
| -------------------- | --------------------------------------------------------- |
| 落点                   | `TextContent.textPath`（文本专属；非 Layer 级 FollowPath）         |
| 路径来源                 | 同 composition 另一图层的 evaluated `path`（ShapePath / 几何 bake） |
| 自引用 / 无效 id / 无 path | 视为无效 → 退回普通点文本                                            |
| 与 `boxTextMode`      | 路径有效时忽略框排版；不改写存储的 `boxTextMode`                           |
| 路径层                  | 仍是普通形状层（可显示/编辑）                                           |
| 路径层隐藏                | **不影响**文本路径布局：只取路径几何与 world 变换，不检查路径层 `visible`（可隐藏作引导） |
| schema               | 开发阶段不升 `schemaVersion`；缺省用上表默认                            |


**PropertyPath / Undo**

- 可动画：`content.textPath.firstMargin`、`content.textPath.lastMargin`
- 非动画：`SetTextPathCommand`（enabled、pathLayerId、三 bool），可 merge

**序列化（文本 content JSON）**

```json
"textPath": {
  "enabled": true,
  "pathLayerId": 42,
  "reversed": false,
  "perpendicular": true,
  "forceAlignment": false,
  "firstMargin": 0,
  "lastMargin": 0
}
```

margin 有关键帧时走现有 Animatable JSON 写法。

---



## §2 求值与 DrawCommand

Core **只解析路径并变换坐标系**，不做字距/弧长排版。

### 求值

```
EvaluateTextPath(document, textLayer, time) -> optional<EvaluatedTextPath>
  if !textPath.enabled || pathLayerId 无效/自引用 → nullopt
  pathLayer = find；取 evaluated BezierPath（无 → nullopt）
  // 不检查 pathLayer.visible：隐藏路径层仍参与布局
  M = inv(textLayer.world) * pathLayer.world
  localPath = TransformBezierPath(path, M)   // 顶点与 tangent
  // 注意：reversed 不在此处 bake 进几何；交给 Adapter/导出按标志处理
  // （与「导出未 reverse 几何 + reversedPath 标志」一致；
  //   若 Adapter 内部 reverse 仅用于 PathMeasure，不写回模型）
  return {
    path: localPath,          // 未 reverse 的文本 local 几何
    reversed, perpendicular, forceAlignment,
    firstMargin.evaluate(t), lastMargin.evaluate(t)
  }
```

```cpp
struct EvaluatedTextPath {
    BezierPath path;          // 文本 local；未应用 reversed
    bool reversed = false;
    bool perpendicular = true;
    bool forceAlignment = false;
    float firstMargin = 0;
    float lastMargin = 0;
};

struct EvaluatedTextItem {
    // ...existing fields...
    std::optional<EvaluatedTextPath> textPath;
};
```

路径有效时，Adapter 将 `boxTextMode` 视为 false（直线框排版关闭）。

### TextDrawParams / DrawCommand

```cpp
struct TextDrawParams {
    std::string text;
    float fontSize = 48;
    Vec2 containerSize;
    bool boxTextMode = false;
    TextAlign align = TextAlign::Left;
    std::string fontFamily;
    std::string fontStyle;
    std::vector<TextDrawStyle> styles;

    bool textPathEnabled = false;
    BezierPath textPath;
    bool textPathReversed = false;
    bool textPathPerpendicular = true;
    bool textPathForceAlignment = false;
    float textPathFirstMargin = 0;
    float textPathLastMargin = 0;
};
```

- `RenderAdapter::drawText(const TextDrawParams &)`
- `DrawCommand`：`DrawText` 携带同等字段（或内嵌 `TextDrawParams`）；`PlayCommands` 组装后调用
- 不新增 `DrawCommandType`

路径 morph / 路径层 transform / margin 关键帧 → 每帧重求值 path 快照；排版缓存在 Adapter（§3）。

---



## §3 Adapter 布局 + 缓存



### 模块

放在 `adapter/textlayout/`（或紧邻），绘制与 AABB 测量共用：

```cpp
struct TextPathLayoutInput {
    std::string text;
    float fontSize;
    TextAlign align;
    // GlyphMetrics / Typeface
    BezierPath path;          // 文本 local，未 reverse
    bool reversed;
    bool perpendicular;
    bool forceAlignment;
    float firstMargin;
    float lastMargin;
};

struct TextPathGlyph {
    Mat3 matrix;              // 文本 local（对齐 PAG applyToGlyphs）
    float advance;
    // glyph id 或 UTF-8 子串，实现时定
};

struct TextPathLayoutResult {
    std::vector<TextPathGlyph> glyphs;
    // 全部 glyph bounds 并集（文本 local min/max）
    Vec2 boundsMin;
    Vec2 boundsMax;
};
```

算法对照参考 PAG `TextPathRender`（只读源码，不链接该目标：`third_party/libpag/src/rendering/renderers/TextPathRender.cpp`）：

1. 点文本直线排版得每字 baseline x（`\n` 多行；行间 y 作法向偏移）
2. 若 `reversed`：对 path 做 reverse 再 `PathMeasure`
3. `tgfx::PathMeasure::MakeFrom`
4. `forceAlignment` 时重算字间距铺满有效弧长
5. 两端按需延长（超长 / 负 margin，同 PAG `CreatePath`）
6. `getPosTan` 映射中心；`perpendicular` 时按切线旋转



### 绘制

```
drawText(TextDrawParams p):
  if p.textPathEnabled && path 非空:
    result = cache.getOrCompute(key(p))
    for style in p.styles:
      for glyph in result.glyphs:
        save; concat(glyph.matrix); draw glyph; restore
  else:
    现有 LayoutText + TextBlob
```



### 缓存


| 项     | 策略                                                                                               |
| ----- | ------------------------------------------------------------------------------------------------ |
| Key   | text、fontFamily/Style、fontSize、align、path 顶点哈希、reversed、perpendicular、forceAlignment、margins（量化） |
| Value | `TextPathLayoutResult`                                                                           |
| 范围    | adapter 实例内；单槽「上一帧」或小型 LRU                                                                       |
| 线程    | 与 canvas adapter 同线程                                                                             |


---



## §4 选中 AABB + Bridge / App



### AABB

现有：`ms_layer_local_bounds` → `ResolvePointTextContainerSizes` → `MeasurePointTextSize` → `BoundsOfLayerLocal`。

路径文本：

```
ResolvePointTextContainerSizes / Bounds 路径:
  if textPath 有效:
    result = MeasureTextPathBounds（与绘制同一 TextPathLayout + 缓存）
    BoundsOfLayerLocal 使用 result.boundsMin/Max（非轴线假设原点为 0）
  elif !boxTextMode:
    MeasurePointTextSize（现状）
```

- 选中框 = 路径布局后 glyph 的轴对齐盒（文本 local）
- 按点文本对待：无 `content.size` 缩放手柄
- Hit-test：MVP 仍用该 AABB



### Bridge


| API                             | 作用                                                        |
| ------------------------------- | --------------------------------------------------------- |
| `ms_command_set_text_path(...)` | enabled、pathLayerId、reversed、perpendicular、forceAlignment |
| query getters                   | 读上述静态字段                                                   |
| margin                          | 现有 float 路径 `content.textPath.firstMargin` / `lastMargin` |




### App Inspector

- 文本层 **Text Path** 区块（模式对齐 Follow Path）
- 开关、选路径层、三 bool、first/last margin
- 路径有效时：隐藏/灰显框文本与 size 手柄
- 时间轴：两 margin 可打 KF

### 用法：文字绕圆循环

沿路径「转圈」靠动画 **`firstMargin`**（弧长像素），不是单独的 0–1 offset。闭合路径上采样会按弧长取模，margin 超过一周仍会绕回。

**搭建**

1. 画闭合圆/椭圆 Shape 层（路径层可随后隐藏）
2. 文本层开启 Text Path，绑定该路径；`perpendicular` 默认开
3. 需要字距拉开铺满整圈时开 `forceAlignment`；只平移不拉距则保持关闭
4. 转圈方向见下「顺/逆时针」；字朝向整体反了再考虑 `reversed`

**周长（椭圆 `size` = 宽×高包围盒）**

- 圆（`width == height`）：半径 \(r = \text{size}/2\)，一周 \(L = 2\pi r = \pi\cdot\text{size}\)
- 非圆椭圆：用实际路径弧长（近似或实测），不要用 \(\pi\cdot\text{width}\)

**\(T\) 秒转 \(N\) 圈**

\[
\Delta\text{firstMargin} = N \times L
\]

关键帧用**线性**缓动：

| 时间 | 帧（\(\mathrm{fps}\)） | `firstMargin` |
|---|---|---|
| \(0\) | \(0\) | \(0\) |
| \(T\) | \(T\times\mathrm{fps}\) | \(N\times L\) |

例：直径 `size = 200` 的圆，**3s 转 10 圈**（30 fps）：

- \(L \approx \pi\times 200 \approx 628.3\)
- \(\Delta = 10\times L \approx 6283\)
- 帧 0 → `0`；帧 90 → `6283`；Composition ≥ 3s，循环播放即可连续转

**顺/逆时针（转圈方向）**

路径「正向」由路径顶点绘制顺序决定（圆/椭圆创建时的绕向）；`firstMargin` 增大 = 沿当前测量方向前进。

| 目标 | 做法 | 字朝向 |
|---|---|---|
| 沿路径正向转 \(N\) 圈 | `firstMargin`: \(0\) → \(+N\times L\) | 不变 |
| **反向转**（常见「逆时针」若当前正向是顺时针） | `firstMargin`: \(0\) → \(-N\times L\) | **不变**（推荐） |
| 同上，但接受字随切线翻转 | 开 `reversed`，仍用 \(0\) → \(+N\times L\) | 会翻转 |

`reversed` 语义（对齐 AE / PAG）：反转路径再 `PathMeasure`。切线约转 180°，`perpendicular=true` 时字跟着倒；同时 margin 沿原几何的行走方向也会对调。因此：

- **只想改转圈方向、字保持正立** → 用 **负 `firstMargin`**，不要开 `reversed`
- **字整体朝向反了**（例如贴在圆内侧/外侧读反）→ 再开 `reversed`；之后若转圈方向又不对，再用正/负 margin 微调

---



## §5 PAG 导出

PAG 路径文本使用 text 层 **自身** `pathOption`（mask 引用），非跨层 id。导出时物化：

```
若 textPath 有效:
  localPath = 与预览相同的 world→text-local 几何（未 reverse）
  路径/相对变换有动画 → maskPath 按帧 bake 关键帧
  textLayer.pathOption = {
    path: MaskData { maskPath = localPath }   // 不进入普通 masks 列表
    reversedPath = textPath.reversed          // 静态 Property<bool>
    perpendicularToPath / forceAlignment
    firstMargin / lastMargin                  // 保留 Animatable KF
  }
  TextDocument: 点文本（boxText=false）；baseline/position 按现有点文本导出规则
```


| 失败       | 行为                                                   |
| -------- | ---------------------------------------------------- |
| 无效 / 空路径 | 不写 `pathOption`，普通点文本 + warning `TextPathUnresolved` |
| 无法保留矢量   | 优先逐帧 bake maskPath；仍失败再走既有 Fallback 策略               |


实现时同步修订 `[2026-07-31-pag-export-design.md](./2026-07-31-pag-export-design.md)` §3.4 Text，增加 Text Path 映射行。

---



## §6 测试


| 层       | 用例                                                                |
| ------- | ----------------------------------------------------------------- |
| Core    | 序列化 round-trip；`SetTextPathCommand` undo；无效/自引用 → 无 `textPath` 求值 |
| 求值      | 直线 path + margin → local 端点；路径层平移 → 文本 local 点变化；路径层 `visible=false` 仍有 `textPath` |
| Adapter | 开放/闭合路径布局；缓存命中（同输入二次调用可计数）                                        |
| Bridge  | 路径文本 local bounds ≠ 直线点文本盒                                        |
| PAG     | `File::Load` 后 `pathOption` 与 margin KF 结构断言                      |


---



## 进度

- [x] 设计对话锁定决策  
- [x] Spec 人工审阅  
- [x] Implementation plan  
- [x] 实现