# Shader Uniform 默认值 / 可动画 + Color format — 设计说明

日期：2026-08-10  
状态：已批准，待实现  
范围：`ShaderUniformDecl` 元数据、Inspector float2/3/4 布局、`UniformFormat::Color`；不升 `document.json` schemaVersion

相关：`docs/data-model.md`（Shader / StylePaintMode）、`docs/color-source-effect.md`、既有 color-source Core 存储 spec

## 目标

1. Inspector 中 `float2`（如 `center`）可正常输入，不被窄栏挤没
2. 自定义 Uniform 可声明 **是否支持关键帧** 与 **默认值**
3. `UniformFormat` 增加独立 **`Color`**；不再把 `Float4` 当作颜色

## 非目标

- 自动把旧 `"format":"float4"` 改写成 `"color"`（见迁移 B）
- 新增 Static* `ShaderUniformValueKind`（仍用 `Animatable`，不可动画时保证无关键帧）
- 改 default 时启发式同步「仍等于旧默认」的实例
- 画布 point-angle-radius 控件（Figma transform handle）

## 已锁定决策

| 项 | 选择 |
|---|---|
| 方案 | 扩展 `ShaderUniformDecl`（方案 1） |
| 关关键帧 | 数据层强制静态；Realign 时有关键帧则 `evaluate(0)` 后拍平 |
| 默认值生效 | 仅新建绑定 / Realign **新增** uniform；已有实例保留 |
| Color | 新 format；GPU 仍为 `vec4` |
| 旧 float4 | 不迁移 → 四通道数值（`AnimFloat4`）；要颜色需手改 `color` |

---

## §1 数据模型与序列化

### `ShaderUniformDecl`

```cpp
struct ShaderUniformDecl {
    std::string name;
    UniformFormat format = UniformFormat::Float;
    int count = 1;
    bool animatable = true;  // 缺省 true，兼容旧文件
    float defaultFloat = 0.f;
    Vec2 defaultFloat2{0, 0};
    Vec3 defaultFloat3{0, 0, 0};
    // Float4 默认零向量；Color 默认不透明白（与现 colorValue 默认一致）
    Color defaultFloat4{0, 0, 0, 0};  // 存 r,g,b,a 通道，语义随 format
    Color defaultColor{1, 1, 1, 1};
};
```

实现时可把 `defaultFloat4` / `defaultColor` 收成按 format 选用的存储，对外序列化形状如下即可。

### `UniformFormat::Color`

- 枚举 **追加在末尾**（`UniformFormat` 与 `MS_UNIFORM_FORMAT` 均不得插入中间，保持序稳定）
- `UniformFormatGLSLTypeName(Color)` → `"vec4"`
- `UniformFormatByteSize(Color)` → 与 `Float4` 相同
- JSON 字符串：`"color"`
- Bridge：`MS_UNIFORM_FORMAT_COLOR`（新 rawValue，接在现有 editable 值之后或枚举末尾，与 C++ 追加策略一致且不复用旧序号）

### `KindForFormat`（v1 用户 scheme）

| format | kind | Inspector |
|---|---|---|
| Float | AnimFloat | 单数值 |
| Float2 | AnimFloat2 | X/Y |
| Float3 | AnimFloat3 | X/Y/Z |
| Float4 | **AnimFloat4** | X/Y/Z/W（不再 ColorPicker） |
| Color | **AnimColor** | ColorPicker |

### `shader.json` 示例

```json
{
  "name": "center",
  "format": "float2",
  "count": 1,
  "animatable": false,
  "default": [50.0, 200.0]
}
```

| format | `default` JSON |
|---|---|
| float | number |
| float2 / float3 | number 数组 |
| float4 | `[x,y,z,w]` |
| color | `#RRGGBBAA`（与文档 color 静态值同形） |

**兼容**

- 缺 `animatable` → `true`
- 缺 `default` → float/float2/float3/float4 为零；color 为 `#FFFFFFFF`
- 包 `schemaVersion` 不升

### 实例值与拍平

- 仍用 `ShaderUniformValue` + `Animatable<T>`
- `animatable == false`：无 keyframes，只改 static
- Realign 保留同名同 kind 时，若 decl 现为不可动画且 `hasKeyframes()`：  
  `setStatic(evaluate(0))` 后 `clearKeyframes()`（Core 无 playhead，用帧 0 可测）

```text
MakeDefaultUniformValue(decl):
  kind = KindForFormat(decl.format)
  v = { name, kind }
  v.setStatic(defaultFrom(decl))
  return v

Realign(decl, prev):
  if prev same name+kind:
    v = prev
    if !decl.animatable && v.hasKeyframes():
      v.setStatic(v.evaluate(0)); v.clearKeyframes()
  else:
    v = MakeDefaultUniformValue(decl)
```

---

## §2 UI 与行为

### Shader Editor

添加/编辑 Uniform：

| 控件 | 说明 |
|---|---|
| Name / Format | format 含 `float4` 与 `color` |
| Animatable | Toggle，默认开 |
| Default | 随 format：标量 / 分轴 / ColorPicker |

保存写入 scheme（`ms_document_update_shader` 的 uniforms JSON）。

### Inspector

1. **float2 / float3 / float4 布局**  
   - 名称单独一行  
   - 分轴紧凑数字框，**禁止**再嵌套整颗带 78pt 标签的 `NumberPropertyRow`  
   - 可动画时：行尾一颗钻石绑定整个向量

2. **color**  
   - ColorPicker +（可动画时）钻石

3. **`animatable == false`**  
   - 隐藏钻石  
   - 只 `setStatic*`  
   - 若仍调用 `addKeyframe*`：Bridge/Core 按 static 处理或拒绝，避免脏关键帧

### Realign / 默认值场景表

| 场景 | 行为 |
|---|---|
| 新绑 / Realign 新增 | 用 decl default |
| 同名同 kind 保留 | 保留实例；必要时拍平关键帧 |
| `animatable` false→true | 保持静态值，之后可打关键帧 |
| 仅改 default | 不影响已有实例 |
| 旧 `float4` 文件 | 按 `AnimFloat4` 四通道显示；不改写成 color |

---

## §3 测试要点

- 旧 scheme 无新字段 → 可读，行为与今相同（float4 除外：现为四通道而非 ColorPicker）
- `default: [50,200]` 新绑后 center 正确
- `animatable: false` 无钻石；原有关键帧在更新 scheme 后被拍平
- 窄栏下 float2 可输入
- `color` 与 `float4` 控件与 kind 分离；round-trip `shader.json`

## §4 实现落点（参考）

| 区域 | 文件（示意） |
|---|---|
| format | `UniformFormat.h/.cpp`、`Dto`、bridge enum、Swift `editableCases` |
| decl / defaults / realign | `ShaderDefinition.h`、`ShaderUniformValues.*`、`Serializer` |
| UI | `ShaderEditorSheet.swift`、`StyleShaderPaintControls.swift` |
| 测试 | `ShaderUniformValuesTest`、序列化 / undo shader 相关 |

---

## 修订记录

- 2026-08-10：批准方案 1；关关键帧拍平；默认值仅新增；增加 `Color`；旧 float4 不迁移。
