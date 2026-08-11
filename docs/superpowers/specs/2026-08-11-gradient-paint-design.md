# Gradient Paint — 设计说明

日期：2026-08-11  
状态：已实现（见 `docs/superpowers/plans/2026-08-11-gradient-paint.md`）  
关联：[数据模型](../../data-model.md)、[渲染](../../rendering.md)、[Color Source Core 存储](2026-08-07-color-source-core-storage-design.md)

## 目标

为 Fill / Stroke 增加 tgfx 原生渐变绘制：Linear / Radial / Conic / Diamond；Inspector 以第三种 paint mode 接入；支持几何与色标关键帧；画布可拖手柄；PAG 导出 Linear / Radial，其余由用户自行选 BMP。

## 已锁定决策

| 项 | 选择 |
|---|---|
| Paint 切换 | 三段：`Color \| Gradient \| Shader`（`StylePaintMode` / kind） |
| kind 切换语义 | **只改 kind，不自动清空** `color` / `gradient` / `shaderId+uniformValues`；切回仍保留上次配置 |
| 动画 | 几何（start/end/angles）+ 每个 stop 的 color / position 均可 `Animatable` |
| Stops | 可变 N（≥2），可增删 |
| 坐标空间 | 图层局部 px（与 shape path 同空间） |
| 几何编码 | 对齐 PAG：`start` / `end`；Radial/Diamond 的 `radius = \|end - start\|`；Conic 另加角度 |
| 编辑 | Inspector 数值 + 画布拖拽手柄 |
| 导出 | PAG：Linear / Radial → `GradientFill` / `GradientStroke`；Conic / Diamond / Shader **跳过该 paint**（不静默栅格）；用户可自行选 BMP |
| Core 与 tgfx | Core 只存求值快照；`Make*Gradient` 仅在 adapter |
| 求值 / 绘制 | **只按当前 kind 求值**；当前 kind 数据不合法 → **跳过该 paint 绘制**（不回退到其它 kind、不画占位色） |

## 非目标

- Lottie 渐变导出
- PAG `Angle` / `Reflected`；PAG `Diamond`（file.h 标注 not supported）
- stop midpoint（AE 风格）
- 画布上直接拖 stop 色标
- 渐变缩略图 / 库资源化（渐变内嵌在 Fill/Stroke，不进 `Document.shaders`）

## 方案取舍

选用 **独立 `StylePaintMode::Gradient` + 内嵌 `GradientPaint`**：与现有 Color/Shader 互斥一致；几何对齐 PAG start/end；渲染走 tgfx 原生 API。

不采用：用过程色 Shader 模拟渐变（导出与手柄都别扭）；按类型拆完全不同的字段集（PropertyPath / 序列化分支过多）。

---

## 1. 数据模型

```cpp
enum class StylePaintMode : uint8_t {
    Color = 0,
    Shader = 1,
    Gradient = 2,
};

enum class GradientType : uint8_t {
    Linear = 0,
    Radial = 1,
    Conic = 2,     // tgfx Conic；PAG 不导出
    Diamond = 3,   // tgfx Diamond；PAG 不导出
};

struct GradientStop {
    Animatable<Color> color{Color{0, 0, 0, 1}};
    Animatable<float> position{0.f};  // 0…1
};

struct GradientPaint {
    GradientType type = GradientType::Linear;
    // start/end：图层局部 AABB 左上角空间（(0,0)=bounds.min）；求值时 + aabb.min → shape 路径空间
    Animatable<Vec2> start{Vec2{0, 0}};    // Linear 起点；其它类型中心
    Animatable<Vec2> end{Vec2{100, 0}};    // Linear 终点；Radial/Diamond 半径点
    Animatable<float> startAngle{0.f};     // 仅 Conic，度
    Animatable<float> endAngle{360.f};     // 仅 Conic，度
    std::vector<GradientStop> stops;      // N ≥ 2
};

// FillStyle / StrokeStyle 增加成员：
GradientPaint gradient;
```

### 1.1 kind 与三态共存

`paintMode`（kind）只决定**当前求值 / 绘制 / Inspector 主面板**用哪一路；三套数据**始终共存**于 Fill/Stroke：

| kind | 求值使用 | 未使用字段 |
|---|---|---|
| Color | `color` | `gradient`、`shaderId`/`uniformValues` 保留在模型里，忽略 |
| Gradient | `gradient` | `color`、shader 字段保留，忽略 |
| Shader | `shaderId` + `uniformValues` | `color`、`gradient` 保留，忽略 |

**切换 kind 不自动清空**任何一路（含 keyframes）。这样 Color ↔ Gradient ↔ Shader 来回切，用户不必重配。

**仅在目标路「尚未可用」时补默认（懒初始化，不算清空）：**

| 切到 | 条件 | 动作 |
|---|---|---|
| Gradient | `stops.size() < 2` | 填默认：`type=Linear`；`start`/`end` = AABB 左上角空间水平中线内缩 15% `(0.15w,h/2)→(0.85w,h/2)`（无 bounds 则 `(0,0)→(100,0)`）；stops = 黑@0 / 白@1 |
| Shader | `shaderId` 无效 | 绑定库中第一个 shader 并 `MakeDefaultUniformValues`（若库空则拒绝切换） |
| Color | （无） | 只改 kind；`color` 沿用已有值 |

Gradient 有效时额外约束：stops≥2；首 position=0、末=1、中间严格递增（命令 / 反序列化强制）。

与旧行为差异：现有 `ClearShaderPaint` / 切回 Color 会清 `shaderId`+`uniformValues`——本设计改为**不再清**；`SetStylePaintModeCommand` 只写 kind（+ 上表懒初始化）。显式「解绑 shader / 重置渐变」若需要，另做命令，不挂在 kind 切换上。

### 1.2 PropertyPath

```
styles[i].gradient.start
styles[i].gradient.end
styles[i].gradient.startAngle
styles[i].gradient.endAngle
styles[i].gradient.stops[j].color
styles[i].gradient.stops[j].position
```

`GradientType` 与 stops 增删走专用 undo 命令（非 Animatable 路径）。

### 1.3 序列化

`document.json` 的 fill/stroke 节点：

```json
{
  "paintMode": "gradient",
  "gradient": {
    "type": "linear",
    "start": { "...animatable Vec2..." },
    "end": { "...animatable Vec2..." },
    "startAngle": { "...animatable float..." },
    "endAngle": { "...animatable float..." },
    "stops": [
      { "color": { "...animatable Color..." }, "position": { "...animatable float..." } }
    ]
  }
}
```

`schemaVersion` 按现有 migrator 惯例处理（缺字段 = Color mode；未知 `paintMode` 报错或按 migrator 定）。

---

## 2. 渲染与 Adapter

### 2.1 求值快照

```cpp
struct EvaluatedGradientStop {
    Color color;
    float position;
};

struct EvaluatedGradient {
    GradientType type = GradientType::Linear;
    Vec2 start;
    Vec2 end;
    float startAngle = 0.f;
    float endAngle = 360.f;
    std::vector<EvaluatedGradientStop> stops;
};

// Paint 增加：
EvaluatedGradient gradient;  // 仅 Gradient mode 有效
```

### 2.1.1 只求值当前 kind；不合法则跳过

`SceneEvaluator` **只读取当前 `paintMode` 对应字段**，不求值、不回退其它 kind：

| kind | 合法条件（不满足则本 style **不产生** DrawPath/StrokePath） |
|---|---|
| Color | 始终可求值（沿用现有） |
| Gradient | `stops.size() ≥ 2`；positions 首 0、末 1、中间严格递增；Radial/Diamond 还要求 `Distance(start,end) > 0` |
| Shader | `shaderId` 能在 `Document.shaders` 找到，且 `MakeShaderPaint` 成功（与现有一致：失败则该 paint 不画） |

不合法时：**静默跳过该 Fill/Stroke 的绘制**——不改用 Color/Gradient/Shader 另一路，不画品红/黑占位。其它合法 style 照常画。

Gradient 合法时 evaluate 几何与各 stop，写入 `Paint.gradient`。局部坐标；world 变换仍由现有 DrawPath/StrokePath 的 layer matrix 负责。

### 2.2 Adapter 映射

| `GradientType` | tgfx |
|---|---|
| Linear | `Shader::MakeLinearGradient(start, end, colors, positions)` |
| Radial | `MakeRadialGradient(start, Distance(start,end), …)` |
| Conic | `MakeConicGradient(start, startAngle, endAngle, …)` |
| Diamond | `MakeDiamondGradient(start, Distance(start,end), …)` |

Adapter 若仍收到非法 gradient（防御），同样跳过该次 draw，**不**退化 solid。不新增 `DrawCommand` 种类。

---

## 3. Undo / Bridge

- 扩展 `SetStylePaintModeCommand`：**只改 kind**；切到 Gradient/Shader 时按 §1.1 懒初始化；**禁止**再走「ClearShaderPaint 清字段」路径
- 旧 `ClearShaderPaint` / `BindShaderPaint`：绑定/解绑仍可用于显式操作；kind 切换不再调用 Clear
- `SetGradientTypeCommand`
- `AddGradientStopCommand` / `RemoveGradientStopCommand`（保持 N≥2；删除后必要时重归一化首尾 position）
- Bridge：读 type / 几何 / stops；动画读写走现有 property/keyframe API + 新路径
- Timeline：Gradient mode 下列出已打关键帧的 gradient 属性轨（对标 shader uniform tracks）；非当前 kind 的已有关键帧仍留在文档里，timeline 可只展示当前 kind 的轨（避免噪音）

---

## 4. App UI

### 4.1 Inspector

`StyleShaderPaintControls` 三段 Picker：`Color | Gradient | Shader`。

Gradient 面板：

1. Type：Linear / Radial / Conic / Diamond  
2. 几何：Start/End（可关键帧）；Conic 另显示 Start/End Angle  
3. Stops：Color + Position + 关键帧；`+` / `−`（最少 2）

Color 色板仅 Color mode 显示。

### 4.2 画布手柄

选中图层且存在 Gradient 的 Fill/Stroke（优先 primary selection 上第一个 Gradient style）时显示：

| Type | Chrome |
|---|---|
| Linear | Start、End + 连线 |
| Radial / Diamond | Center、RadiusPoint + 圆/菱形示意 |
| Conic | Center + 起/终角射线 |

交互仿 PathEdit：hit → drag → merge group 写对应 PropertyPath。手柄点：local → × layer world matrix → scene。命中渐变手柄时不启动 free-transform。

---

## 5. 导出

| Paint | PAG | Lottie | BMP |
|---|---|---|---|
| Linear / Radial Gradient | → `GradientFill` / `GradientStroke`（start/end + colors） | 本轮不做 | 用户自选 |
| Conic / Diamond | **跳过该 paint** | 本轮不做 | 用户自选 |
| Shader | 仍跳过 | 本轮不做 | 用户自选 |

导出 UI：遇到不可导出 paint 时提示可用 BMP；**不**自动改文档、**不**静默栅格进 PAG。

---

## 6. 测试要点

- 模型：kind 切换不清空；懒初始化；stops 约束  
- PropertyPath 解析与 keyframe round-trip（几何 + stop）  
- 序列化 round-trip（四种 type）  
- SceneEvaluator 快照正确；非法 Gradient/Shader **不产生**对应 draw（不回退 Color）  
- Adapter：四种渐变各至少一帧可画  
- PAG 导出：Linear/Radial 有元素；Conic/Diamond 被跳过且不崩  

---

## 7. 实现分层

见实现计划 Tasks：Core 模型/undo/序列化 → 求值/adapter → Bridge/Inspector → 画布手柄 → PAG 导出。