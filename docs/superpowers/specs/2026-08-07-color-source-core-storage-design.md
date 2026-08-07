# Color Source Core 存储 — 设计说明

日期：2026-08-07  
状态：已定案（实现计划：`docs/superpowers/plans/2026-08-07-color-source-core-storage.md`）  
关联：[ColorSourceEffect 与 RenderCache](../../color-source-effect.md)、[数据模型](../../data-model.md)

## 目标

在 Core / 工程包中持久化过程色（Color Source）定义，并让 `FillStyle` / `StrokeStyle` 以 `shaderId` 引用；与现有 adapter（`ColorSourceEffect::Make(shaderId, …)` + `invalidateColorSourcePipeline`）对齐。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 定义存放 | 包内独立 `shader.json`（源码 + uniform scheme）；**不**进 `Document.assets` |
| 运行时所有权 | `Document.shaders`（`vector<ShaderDefinition>`） |
| 样式引用 | Fill / Stroke 持 `shaderId` + 本实例 `uniformValues` |
| 绘制源 | **color XOR shader**（`StylePaintMode`）；调色靠多 Fill + `blendMode`，不用 color 乘 shader |
| 用户 uniform 动画 | v1：`Float` / `Float2` / `Float3` / `Float4→Color` 走 `Animatable<>`；其它 format 预留 kind |
| 内置 Shadertoy uniform | 不进 scheme；仍由 adapter `onDraw` 注入 |
| 删 shader | 仍被引用则拒绝删除 |
| EntityIndex | v1 扫 `Document.shaders`（同 Asset）；不强制扩索引 |
| 导出 | Lottie / PAG **本阶段不支持**过程色（实现计划再定跳过或栅格） |

## 非目标

- 第一版 Int / Mat / Sampler uniform 的编辑与动画
- 把 `UniformFormat` 之外的 GLSL 类型系统做完整校验（编译错误仍在 GPU 侧暴露）
- Core 依赖 tgfx；过程色绘制仍在 adapter
- 跨文档粘贴的完整资源合并策略细节（实现计划细化）

## 方案取舍

选用 **独立 `shader.json` + 内存 `Document.shaders`**：与现有 `document.json` + `assets/` 包结构一致；一 shader 多 Fill 复用、统一改源码自然；`EntityId` 直接作为 adapter `shaderId`。

不采用：仅磁盘懒加载（undo/校验复杂）；scheme 快照打进每个 Fill（无法共享编辑）。

---

## 1. 包与运行时所有权

```
foo.motionproject/
  document.json      # 合成 / 层 / Fill·Stroke 引用与 uniformValues
  shader.json        # 过程色定义库
  assets/            # 现有图片等
```

```cpp
// Document
std::vector<ShaderDefinition> shaders;

struct ShaderUniformDecl {
    std::string name;
    UniformFormat format;  // 迁入 Core，adapter 共用
    int count = 1;         // v1 可先约束 count == 1
};

struct ShaderDefinition {
    EntityId id = EntityId::Generate();
    std::string name;
    std::string mainImage;  // 用户 mainImage(uv) 体
    std::vector<ShaderUniformDecl> uniforms;  // scheme only
};
```

### `shader.json` 形状

```json
{
  "schemaVersion": 1,
  "shaders": [
    {
      "id": "<uint64 string or decimal>",
      "name": "Ripple",
      "mainImage": "vec4 mainImage(vec2 uv) { ... }",
      "uniforms": [
        { "name": "rippleCount", "format": "float", "count": 1 }
      ]
    }
  ]
}
```

- `shader.json` 的 `schemaVersion` **独立**于 `document.json`，可分开演进。
- 打开：缺失 `shader.json` → 空库；再加载 `document.json` 并校验引用。
- 保存：App 包层同时写两文件；无 shader 时可写 `shaders: []` 或省略文件（加载当空库）。
- 改 `mainImage` / scheme 后 **保持同一 `EntityId`**；渲染侧调用 `invalidateColorSourcePipeline(id)`。

---

## 2. Fill / Stroke 互斥与 uniformValues

### 2.1 绘制源

```cpp
enum class StylePaintMode : uint8_t {
    Color = 0,
    Shader = 1,
};

class FillStyle : public LayerStyle {
    StylePaintMode paintMode = StylePaintMode::Color;

    Animatable<Color> color{Color{0, 0, 0, 1}};  // Color 模式

    EntityId shaderId;                  // Shader 模式；须指向 Document.shaders
    ShaderUniformValues uniformValues;  // Shader 模式；按 name 对齐 scheme

    FillRule fillRule = FillRule::NonZero;
    BlendMode blendMode = BlendMode::Normal;
};

// StrokeStyle 同构（另含 width / cap / join / trim / …）
```

**不变式（命令与反序列化强制）：**

| `paintMode` | `color` | `shaderId` / `uniformValues` |
|---|---|---|
| `Color` | 有效、可关键帧 | `shaderId` 无效；`uniformValues` 空 |
| `Shader` | **不参与渲染**（可保留上次纯色以便切回） | `shaderId` 有效且在库中；values 与 scheme 对齐 |

模式切换用专用命令（如 `SetStylePaintMode`），避免半状态。

### 2.2 Scheme（仅在 `ShaderDefinition`）

- 字段：`name` + `UniformFormat` + `count`
- **不包含** `iResolution` / `iTime` / `iTimeDelta` / `iFrame` / `iFrameRate`

`UniformFormat` 从 adapter 迁到 Core（`include/MotionStudio/…`），adapter 改引用 Core，避免文档 schema 依赖 adapter。

### 2.3 Values（Fill / Stroke；可扩展）

```cpp
enum class ShaderUniformValueKind : uint8_t {
    AnimFloat = 0,   // v1 ← Float
    AnimFloat2 = 1,  // v1 ← Float2
    AnimFloat3 = 2,  // v1 ← Float3
    AnimColor = 3,   // v1 ← Float4（UI/语义为 Color）
    // 预留（本阶段不实现编辑）
    StaticInt = 4,
    AnimFloat4 = 5,
    StaticMat3 = 6,
    TextureAsset = 7,
};

struct ShaderUniformValue {
    std::string name;
    ShaderUniformValueKind kind = ShaderUniformValueKind::AnimFloat;
    Animatable<float> floatValue{0.f};
    Animatable<Vec2> float2Value{Vec2{0, 0}};
    Animatable<Vec3> float3Value{Vec3{0, 0, 0}};
    Animatable<Color> colorValue{Color{1, 1, 1, 1}};
};

struct ShaderUniformValues {
    std::vector<ShaderUniformValue> entries;
};
```

**绑定规则：**

1. 绑定时按 scheme 生成缺省 `uniformValues`（缺补默认，多余丢弃）。
2. 改 scheme（改名 / 改 format）：所有引用该 `shaderId` 的 Fill/Stroke **重对齐** values。
3. format → kind（v1）：`Float→AnimFloat`，`Float2→AnimFloat2`，`Float3→AnimFloat3`，`Float4→AnimColor`；其它 format **暂不允许**出现在用户可编辑 scheme（或加载硬失败）。
4. `PropertyPath` 示例：`styles[0].uniformValues.rippleCount` → 对应 `Animatable<>`。
5. 求值：`evaluate(frame)` → adapter `UniformData::setData`。

后续只加 `ShaderUniformValueKind` + 存储 + serializer 分支；scheme 的 `UniformFormat` 已能表达 Int/Mat/Sampler。

### 2.4 与 adapter 边界

```
Document.shaders[id]           → mainImage + Uniform decls
Fill.uniformValues @ frame     → setData(...)
Fill.shaderId                  → ColorSourceEffect::Make(shaderId, ...)
改 mainImage / decls           → invalidateColorSourcePipeline(shaderId)
```

Core 不知道 tgfx；bridge / 预览管线在画 Fill/Stroke 时组装。

---

## 3. 序列化、引用与索引

### 3.1 版本

| 文件 | 版本策略 |
|---|---|
| `document.json` | `schemaVersion` **不升**（保持 1）；缺 `paintMode` / shader 字段 → 默认 `Color` |
| `shader.json` | 独立 `schemaVersion`，从 1 起 |

`document.json` **不**内嵌完整 shader 源码，只存引用与 values。

Core API 形状（示意）：

- `Serializer::Save` / `Load`：文档（含样式上的 shader 引用）
- `Serializer::SaveShaders` / `LoadShaders`：shader 库  
- App `MotionProjectDocument` 编排读写两个文件（对齐现有 `document.json` + `assets/`）

### 3.2 校验

1. `paintMode == Shader` ⇒ `shaderId` ∈ `Document.shaders`
2. 每个 `uniformValues` 条目的 name ∈ scheme，且 kind 与 format 兼容
3. 未知 format / kind → **硬失败**（避免静默错画）
4. `paintMode == Color` ⇒ 写回时不输出 `shaderId` / `uniformValues`（或加载时忽略）

### 3.3 删除与解绑

| 操作 | 行为 |
|---|---|
| 改回 Color | `paintMode=Color`；清 `shaderId` + `uniformValues`；渲染用 `color` |
| 删除 `ShaderDefinition` | 仍被引用 → 命令失败；无引用才移除，并 `invalidate` |
| 跨文档粘贴 | 实现计划细化：复制定义进目标库或重映射 id |

不做「缺失 shader 粉红占位」；引用完整性由命令保证。

### 3.4 EntityIndex

v1：**不**扩展 `EntityIndex`；`findShader(id)` 扫 `Document.shaders`（与 Asset 一致）。热路径不够再加注册表。

### 3.5 渲染 / 导出

- 求值管线对 Shader 模式携带：`shaderId`、`mainImage`、scheme、本帧已求值的用户 uniform。
- `sourceBounds` 仍由形状 AABB 决定（与现 ColorSourceEffect 一致）。
- **Lottie / PAG 导出**：本阶段不支持过程色（跳过或后续栅格；非本设计实现范围）。

---

## 测试要点（实现计划展开）

- `shader.json` / `document.json` round-trip；缺 `shader.json` 打开为空库
- Color ↔ Shader 模式切换不变式
- 同 `shaderId` 两 Fill 不同 `uniformValues`
- 改 scheme 后引用样式重对齐
- 删除仍被引用的 shader → 失败
- `Animatable` float/vec/color uniform 关键帧 round-trip + evaluate
- adapter：`Make(shaderId)` + invalidate 与文档编辑衔接（集成或现有测试扩展）

## 实现分期（建议）

1. Core 模型 + `UniformFormat` 上移 + serializer（document schema 不升 + shader.json）
2. undo 命令 / PropertyPath / bridge
3. 预览：SceneEvaluator → adapter ColorSourceEffect
4. App：包读写 `shader.json` + Inspector 最小编辑

---

## 修订记录

| 日期 | 说明 |
|---|---|
| 2026-08-07 | 初稿：包分离、XOR paint、可扩展 uniformValues、引用完整性 |
