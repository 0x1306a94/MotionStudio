# Color Source Core 存储 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Core 持久化过程色定义（`shader.json` + `Document.shaders`），Fill/Stroke 以 color XOR shader 引用，并完成序列化与基础 undo / PropertyPath。本 plan **不含任何 UI**（无 App / Inspector / Swift）。

**Architecture:** `ShaderDefinition` 独立于 `Asset`；`UniformFormat` 上移到 Core 供 adapter 共用；`FillStyle`/`StrokeStyle` 用 `StylePaintMode` 互斥；用户 uniform v1 仅 Float/Float2/Float3/Float4→Color 的 `Animatable<>`。文档 `schemaVersion` 不升；`shader.json` 独立 version=1。

**Tech Stack:** C++17 Core、nlohmann/json、GoogleTest、现有 Undo/PropertyPath、adapter ColorSourceEffect（`EntityId` shaderId）。

**Spec:** `docs/superpowers/specs/2026-08-07-color-source-core-storage-design.md`

## Global Constraints

- 分支：留在当前 feature 分支（如 `feature/runtime_shader`）；未经明确要求不得往 `master` 提交。
- **范围：仅 Core（+ adapter 为 `UniformFormat` 上移所必需的改动）与文档/测试。禁止改 App / SwiftUI / Inspector。**
- **自动 commit：** 每完成一个 Task（或可独立验证的 Step 组）必须提交；**提交前先**把本 plan 对应 checkbox 改为 `[x]`、更新 `**Status:**`，再 `git add` 代码与本 plan 一并 commit（或 plan 紧随其后单独 commit）。
- Commit 信息：英语、≤120 字符、句号结尾、句中无其他标点；侧重用户可感知变化。
- 按本 plan 实现时：每完成一个 Step 立刻勾选；未同步 plan 视为该步未完成。
- `document.json`：`dto::SCHEMA_VERSION` **保持 1**；缺 `paintMode` / shader 字段 → 默认 `Color`（与 VertexMirrorMode 同样策略）。
- `shader.json`：独立 `schemaVersion = 1`。
- 宣称 Core 完成前优先 ASan 构建：
  `cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON && cmake --build build`
- 遵守现有编码规范（禁异常、禁 `dynamic_cast`、错误用 `Expected`、禁 lambda 优先显式函数）。
- Core **不**链接 tgfx；过程色绘制仍在 adapter（预览接线不在本 plan）。

---

## 文件对照

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/common/Vec3.h` + `src/common/Vec3.cpp` | 新建；Float3 animatable 载体 |
| `include/MotionStudio/animation/Interpolator.h` + `src/animation/Interpolator.cpp` | `Interpolator<Vec3>` |
| `include/MotionStudio/common/UniformFormat.h` + `src/common/UniformFormat.cpp` | 自 adapter 上移 `UniformFormat` / GLSL 名 / sampler 判断 |
| `adapter/tgfx/src/effects/Uniform.h`（及 .cpp） | 改为 `#include "MotionStudio/common/UniformFormat.h"`，删除重复 enum |
| `include/MotionStudio/model/ShaderDefinition.h` | `ShaderUniformDecl` / `ShaderDefinition` |
| `include/MotionStudio/model/ShaderUniformValues.h` | `ShaderUniformValueKind` / `ShaderUniformValue` / `ShaderUniformValues` + realign API |
| `src/model/ShaderUniformValues.cpp` | `MakeDefaultUniformValues` / `RealignUniformValues` / format↔kind |
| `include/MotionStudio/model/StylePaintMode.h` | `Color` / `Shader` |
| `include/MotionStudio/model/LayerStyle.h` | Fill/Stroke 增加 paintMode / shaderId / uniformValues |
| `include/MotionStudio/model/Document.h` + `src/model/Document.cpp` | `shaders`；`findShader`；引用计数/删除校验辅助 |
| `include/MotionStudio/serialization/Dto.h` | `SCHEMA_VERSION` 保持 1；format/kind/paintMode 字符串映射 |
| `include/MotionStudio/serialization/Serializer.h` + `Serializer.cpp` | 样式字段；`serializeShaders` / `deserializeShaders` |
| `src/serialization/SchemaMigrator.cpp` | **不改**（document schema 不升版） |
| `include/MotionStudio/model/PropertyPath.h` + `PropertyPath.cpp` | `styles[i].uniformValues.<name>` |
| `include/MotionStudio/undo/*`（新建命令） | Add/Remove/UpdateShader、SetStylePaintMode |
| `tests/common/Vec3Test.cpp` | Vec3 基础 |
| `tests/model/ShaderUniformValuesTest.cpp` | realign / defaults |
| `tests/serialization/SerializerTest.cpp` | shaders + Fill shader 字段 round-trip；缺字段默认 Color |
| `tests/model/PropertyPathTest.cpp` 或新建 | uniformValues 解析 |
| `tests/undo/*` | 删引用中 shader 失败；模式切换 |
| `docs/data-model.md` / `docs/color-source-effect.md` | 同步模型与包文件说明 |
| Spec | 状态改为实现中 |

**本 plan 明确不包含：** 任何 UI、App / Swift / Inspector、`MotionProjectDocument` 包读写、SceneEvaluator→ColorSourceEffect 预览接线、Lottie/PAG 导出（均属后续计划）。

---

### Task 1: `Vec3` + `Interpolator<Vec3>`

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/common/Vec3.h`
- Create: `src/common/Vec3.cpp`
- Modify: `include/MotionStudio/animation/Interpolator.h`
- Modify: `src/animation/Interpolator.cpp`
- Create: `tests/common/Vec3Test.cpp`
- Modify: core 的 CMake/`tests` 注册（若需显式加源文件；项目若用 glob 则自动收录）

**Interfaces:**
- Produces:
  ```cpp
  struct Vec3 { float x=0, y=0, z=0; /* +, -, *, ==, != */ };
  bool ApproxEqual(Vec3, Vec3, float epsilon = 1e-5f);
  template<> struct Interpolator<Vec3> { static Vec3 Lerp(const Vec3&, const Vec3&, float t); };
  ```

- [x] **Step 1: 写失败测试**

`tests/common/Vec3Test.cpp`：

```cpp
#include <gtest/gtest.h>
#include "MotionStudio/common/Vec3.h"
#include "MotionStudio/animation/Interpolator.h"

using motion::ApproxEqual;
using motion::Interpolator;
using motion::Vec3;

TEST(Vec3Test, ArithmeticAndApproxEqual) {
    Vec3 a{1, 2, 3};
    Vec3 b{4, 5, 6};
    EXPECT_EQ((a + b), (Vec3{5, 7, 9}));
    EXPECT_EQ((b - a), (Vec3{3, 3, 3}));
    EXPECT_EQ((a * 2.f), (Vec3{2, 4, 6}));
    EXPECT_TRUE(ApproxEqual(a, Vec3{1, 2, 3}));
}

TEST(Vec3Test, InterpolatorLerpMidpoint) {
    Vec3 mid = Interpolator<Vec3>::Lerp(Vec3{0, 0, 0}, Vec3{2, 4, 6}, 0.5f);
    EXPECT_TRUE(ApproxEqual(mid, Vec3{1, 2, 3}));
}
```

- [x] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target core_tests -j$(sysctl -n hw.ncpu)
./build/tests/core_tests --gtest_filter='Vec3Test.*'
```

Expected: 编译失败（缺头文件）或链接失败。

- [x] **Step 3: 实现 Vec3 + Interpolator\<Vec3\>**

镜像 `Vec2` 的 API 风格；`Lerp` 为分量线性插值。

- [x] **Step 4: 跑测试通过**

```bash
./build/tests/core_tests --gtest_filter='Vec3Test.*'
```

Expected: PASS

- [x] **Step 5: 先更新本 plan 状态，再自动 commit**

勾选本 Task 全部 Step、`**Status:** ✅ Done`，然后：

```bash
git add include/MotionStudio/common/Vec3.h src/common/Vec3.cpp \
  include/MotionStudio/animation/Interpolator.h src/animation/Interpolator.cpp \
  tests/common/Vec3Test.cpp \
  docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Add Vec3 and Interpolator support for shader uniforms."
```

---

### Task 2: `UniformFormat` 上移到 Core

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/common/UniformFormat.h`
- Create: `src/common/UniformFormat.cpp`（从 `adapter/tgfx/src/effects/Uniform.cpp` 挪 GLSL 名 / size / IsSampler）
- Modify: `adapter/tgfx/src/effects/Uniform.h` — 删除 enum，改为 include Core + 保留 `class Uniform`
- Modify: adapter 所有 `#include "effects/Uniform.h"` 使用者保持可编译
- Test: 现有 `tgfx_adapter_test` / `Uniform` 相关若有则跑通；至少编过 `tgfx_adapter`

**Interfaces:**
- Produces: `motion::UniformFormat` 与现 adapter enum **同序同名**；`UniformFormatGLSLTypeName`、`IsSamplerFormat`、以及原 `Uniform::size()` 依赖的 size 逻辑放在 Core 或仍由 `Uniform` 类调用 Core 辅助函数。

- [x] **Step 1: 在 Core 增加头/实现，adapter 改为转发**

`Uniform.h`（adapter）保留：

```cpp
#pragma once
#include "MotionStudio/common/UniformFormat.h"
#include <string>
namespace motion {
class Uniform { /* 现有构造与 name/format/count/size，size 实现调 Core */ };
}
```

- [x] **Step 2: 构建 adapter + core**

```bash
cmake --build build --target core tgfx_adapter tgfx_adapter_test -j$(sysctl -n hw.ncpu)
```

Expected: 成功（ASan 下 ColorSourceEffect 已知 glslang 问题可先用 `build-noasan` 跑 adapter 视觉测）。

- [x] **Step 3: 先更新本 plan 状态，再自动 commit**

```bash
git add adapter/tgfx/src/effects/Uniform.h adapter/tgfx/src/effects/Uniform.cpp \
  include/MotionStudio/common/UniformFormat.h src/common/UniformFormat.cpp \
  docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Move UniformFormat into Core for document shader schemes."
```

---

### Task 3: ShaderDefinition + ShaderUniformValues + Document.shaders

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/model/ShaderDefinition.h`
- Create: `include/MotionStudio/model/ShaderUniformValues.h`
- Create: `src/model/ShaderUniformValues.cpp`
- Modify: `include/MotionStudio/model/Document.h` — `std::vector<ShaderDefinition> shaders;`
- Create: `include/MotionStudio/model/ShaderLibrary.h`（可选自由函数）或把 helpers 放在 `ShaderUniformValues.h`：
  ```cpp
  ShaderDefinition *FindShader(Document &doc, EntityId id);
  const ShaderDefinition *FindShader(const Document &doc, EntityId id);
  bool ShaderIsReferenced(const Document &doc, EntityId shaderId);
  Expected<void, std::string> FormatSupportsAnimKind(UniformFormat, ShaderUniformValueKind);
  ShaderUniformValueKind KindForFormat(UniformFormat); // v1 仅 4 种，其它返回错误用 Expected
  ShaderUniformValues MakeDefaultUniformValues(const std::vector<ShaderUniformDecl> &decls);
  Expected<ShaderUniformValues, std::string> RealignUniformValues(
      const std::vector<ShaderUniformDecl> &decls, const ShaderUniformValues &previous);
  ```
- Test: `tests/model/ShaderUniformValuesTest.cpp`

**Interfaces:**
- v1 `KindForFormat`: Float→AnimFloat, Float2→AnimFloat2, Float3→AnimFloat3, Float4→AnimColor；其它 → `Unexpected("unsupported uniform format for v1")`
- `count != 1` → Unexpected（v1）
- `MakeDefaultUniformValues`：跳过失败 format 不应发生；调用方先保证 decls 合法

- [x] **Step 1: 失败测试**

```cpp
TEST(ShaderUniformValuesTest, MakeDefaultsForFloatAndColor) {
    std::vector<ShaderUniformDecl> decls = {
        {"rippleCount", UniformFormat::Float, 1},
        {"tint", UniformFormat::Float4, 1},
    };
    auto values = MakeDefaultUniformValues(decls);
    ASSERT_EQ(values.entries.size(), 2u);
    EXPECT_EQ(values.entries[0].kind, ShaderUniformValueKind::AnimFloat);
    EXPECT_EQ(values.entries[1].kind, ShaderUniformValueKind::AnimColor);
}

TEST(ShaderUniformValuesTest, RealignDropsRemovedAndAddsNew) {
    ShaderUniformValues previous;
    ShaderUniformValue keep;
    keep.name = "rippleCount";
    keep.kind = ShaderUniformValueKind::AnimFloat;
    keep.floatValue = Animatable<float>{5.f};
    previous.entries.push_back(keep);

    std::vector<ShaderUniformDecl> decls = {
        {"rippleCount", UniformFormat::Float, 1},
        {"speed", UniformFormat::Float, 1},
    };
    auto realigned = RealignUniformValues(decls, previous);
    ASSERT_TRUE(realigned.hasValue());
    ASSERT_EQ(realigned->entries.size(), 2u);
    EXPECT_FLOAT_EQ(realigned->entries[0].floatValue.staticValue(), 5.f);
    EXPECT_EQ(realigned->entries[1].name, "speed");
}

TEST(ShaderUniformValuesTest, RejectsIntFormatInV1) {
    auto kind = KindForFormat(UniformFormat::Int);
    EXPECT_FALSE(kind.hasValue());
}
```

（按项目 `Animatable` / `Expected` 实际 API 调整断言：`hasValue()` / `staticValue()` 等。）

- [x] **Step 2: 实现至测试通过**

- [x] **Step 3: Document 增加 `shaders`；实现 `FindShader` / `ShaderIsReferenced`（遍历所有 layer.styles）**

- [x] **Step 4: 先更新本 plan 状态，再自动 commit**

```bash
git add include/MotionStudio/model/ShaderDefinition.h \
  include/MotionStudio/model/ShaderUniformValues.h src/model/ShaderUniformValues.cpp \
  include/MotionStudio/model/Document.h src/model/Document.cpp \
  tests/model/ShaderUniformValuesTest.cpp \
  docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Add ShaderDefinition and uniform value realignment on Document."
```

---

### Task 4: `StylePaintMode` 挂到 Fill/Stroke

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/model/StylePaintMode.h`
- Create: `include/MotionStudio/model/LayerStylePaint.h`
- Create: `src/model/LayerStylePaint.cpp`
- Modify: `include/MotionStudio/model/LayerStyle.h`
- Modify: `src/model/ShaderUniformValues.cpp` — `ShaderIsReferenced` 检查 Fill/Stroke `shaderId`
- Test: `tests/model/LayerStylePaintModeTest.cpp`

**Interfaces:**
```cpp
enum class StylePaintMode : uint8_t { Color = 0, Shader = 1 };

// FillStyle / StrokeStyle 增加：
StylePaintMode paintMode = StylePaintMode::Color;
EntityId shaderId{};              // Color 模式必须 !isValid()
ShaderUniformValues uniformValues; // Color 模式必须 empty
```

辅助（`ShaderUniformValues.cpp` 或 `LayerStylePaint.cpp`）：

```cpp
void ClearShaderPaint(FillStyle &style);   // paintMode=Color; shaderId={}; uniformValues={}
Expected<void, std::string> BindShaderPaint(FillStyle &style, const ShaderDefinition &shader);
// Stroke 重载同样
```

`BindShaderPaint`：设 `paintMode=Shader`、`shaderId=shader.id`、`uniformValues=MakeDefaultUniformValues(shader.uniforms)`。

- [x] **Step 1: 改头文件 + Bind/Clear 测试**

- [x] **Step 2: 实现并通过**

- [x] **Step 3: 先更新本 plan 状态，再自动 commit**

```bash
git add include/MotionStudio/model/StylePaintMode.h include/MotionStudio/model/LayerStyle.h \
  src/model/LayerStylePaint.cpp tests/model/LayerStylePaintModeTest.cpp \
  docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Add XOR color and shader paint modes on Fill and Stroke."
```

---

### Task 5: 序列化 `shader.json` + document 可选 paint 字段（schema 不升）

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/serialization/Dto.h` — **不改** `SCHEMA_VERSION`（保持 1）；增加 format/kind/paintMode 字符串映射
- Modify: `include/MotionStudio/serialization/Serializer.h` — 增加：
  ```cpp
  static std::string serializeShaders(const Document &document);
  static Expected<std::vector<ShaderDefinition>, std::string> deserializeShaders(const std::string &jsonText);
  ```
  `deserialize` 文档时：校验 Shader 模式引用；可选接收已加载的 shaders（见下）
- Modify: `Serializer.cpp` — LayerStyle 读写 `paintMode`/`shaderId`/`uniformValues`；缺字段 → Color；Color 模式不写 shader 字段
- Modify: `SchemaMigrator.cpp` — **不改**
- Modify: `deserialize` 流程：先应能单独 `deserializeShaders`；文档反序列化假设 `document.shaders` 已由调用方填入 **或** `deserialize` 只还原引用、由 App 合并。  
  **本 Task 约定：**  
  1. `deserializeShaders` → `vector<ShaderDefinition>`  
  2. `deserialize(documentJson)` 得到 Document（styles 带 shaderId）  
  3. 测试里手动 `doc->shaders = *shaders` 后调用 `ValidateShaderReferences(*doc)`  
  提供：
  ```cpp
  Expected<void, std::string> ValidateShaderReferences(const Document &document);
  ```
- Test: `tests/serialization/SerializerTest.cpp`

**JSON 约定（shader.json）：**

```json
{
  "schemaVersion": 1,
  "shaders": [
    {
      "id": 123456789,
      "name": "Ripple",
      "mainImage": "vec4 mainImage(vec2 uv){ return vec4(uv,0.0,1.0); }",
      "uniforms": [ { "name": "rippleCount", "format": "float", "count": 1 } ]
    }
  ]
}
```

**Fill style（Shader 模式）片段：**

```json
{
  "type": "fill",
  "paintMode": "shader",
  "shaderId": 123456789,
  "uniformValues": [
    { "name": "rippleCount", "kind": "animFloat", "floatValue": { "staticValue": 5.0 } }
  ],
  "fillRule": "nonZero",
  "blendMode": "normal"
}
```

Color 模式保持现有 `color` 字段；可写 `"paintMode":"color"` 或省略（默认 color）。

- [x] **Step 1: 写 SerializerTest**

```cpp
TEST(SerializerTest, ShaderLibraryRoundTrip) {
    Document doc;
    ShaderDefinition shader;
    shader.id = EntityId::Generate();
    shader.name = "Ripple";
    shader.mainImage = "vec4 mainImage(vec2 uv){ return vec4(1.0); }";
    shader.uniforms.push_back({"rippleCount", UniformFormat::Float, 1});
    doc.shaders.push_back(shader);

    auto json = Serializer::serializeShaders(doc);
    auto loaded = Serializer::deserializeShaders(json);
    ASSERT_TRUE(loaded.hasValue());
    ASSERT_EQ(loaded->size(), 1u);
    EXPECT_EQ((*loaded)[0].id, shader.id);
    EXPECT_EQ((*loaded)[0].mainImage, shader.mainImage);
}

TEST(SerializerTest, DocumentShaderPaintRoundTrip) {
    // 建最小 composition+layer+Fill Shader 模式，serialize/deserialize，
    // 合并 shaders，ValidateShaderReferences 成功，uniform 静态值一致；schemaVersion 仍为 1
}

TEST(SerializerTest, MissingPaintModeDefaultsToColor) {
    // 手写 schemaVersion:1 且仅有 color、无 paintMode 的 JSON，deserialize 后 paintMode==Color
}
```

- [x] **Step 2: 实现至 PASS**

```bash
./build/tests/core_tests --gtest_filter='SerializerTest.*Shader*:SerializerTest.MissingPaintMode*'
```

- [x] **Step 3: 更新 `docs/data-model.md`（schemaVersion 仍为 1；新增可选 paint/shader 字段说明）**

- [x] **Step 4: 先更新本 plan 状态，再自动 commit**

```bash
git add include/MotionStudio/serialization/Dto.h \
  include/MotionStudio/serialization/Serializer.h src/serialization/Serializer.cpp \
  tests/serialization/SerializerTest.cpp \
  docs/data-model.md docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Serialize shader libraries without bumping document schema version."
```

---

### Task 6: Undo 命令 — shader CRUD + SetStylePaintMode

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/undo/AddShaderCommand.h` + `.cpp`
- Create: `include/MotionStudio/undo/RemoveShaderCommand.h` + `.cpp`
- Create: `include/MotionStudio/undo/UpdateShaderDefinitionCommand.h` + `.cpp`（改 name/mainImage/uniforms，并对所有引用 Realign）
- Create: `include/MotionStudio/undo/SetStylePaintModeCommand.h` + `.cpp`
- Modify: `CommandKind.h` 增加对应 kind（若项目用枚举分发）
- Modify: CMake/源收录
- Test: `tests/undo/ShaderCommandTest.cpp`

**Interfaces:**
```cpp
// RemoveShaderCommand::execute
//   if (ShaderIsReferenced(doc, id)) → no-op 或记录失败；本项目命令习惯静默跳过时，
//   改为 Expected 不合适；**采用：execute 返回前不删除，测试断言 shaders.size 不变**。
//   更好：RemoveShaderCommand 构造不失败，execute 若引用存在则跳过删除（与「拒绝删除」一致）。

// SetStylePaintModeCommand(layerId, styleIndex, mode, shaderId /*Shader 模式时*/)
//   Color ← 调用 ClearShaderPaint
//   Shader ← FindShader + BindShaderPaint；找不到则跳过
```

- [x] **Step 1: 测试删除被引用 shader 不删除**

```cpp
TEST(ShaderCommandTest, RemoveShaderSkippedWhenReferenced) {
    // doc 有 shader + fill 绑定；RemoveShaderCommand execute；EXPECT_EQ(doc.shaders.size(), 1u);
}

TEST(ShaderCommandTest, RemoveShaderSucceedsWhenUnreferenced) {
    // 无引用；remove 后 size 0
}

TEST(ShaderCommandTest, SetPaintModeToShaderBindsDefaults) {
    // Color → Shader 后 uniformValues 非空且 paintMode==Shader
}
```

- [x] **Step 2: 实现命令并通过**

- [x] **Step 3: UpdateShaderDefinition 后 Realign 所有引用样式的测试**

- [x] **Step 4: 先更新本 plan 状态，再自动 commit**

```bash
git add include/MotionStudio/undo/ src/undo/ tests/undo/ShaderCommandTest.cpp \
  docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Add undo commands for shaders and style paint mode."
```

---

### Task 7: PropertyPath 解析 `uniformValues.<name>`

**Status:** ✅ Done

**Files:**
- Modify: `src/model/PropertyPath.cpp` — `resolveStyleProperty` 扩展：若 name 为 `uniformValues` 则需要下一段；或解析 `uniformValues.rippleCount` 为两段
- 现有路径风格是 `styles[0].color` 分段；增加：`styles[0].uniformValues.rippleCount`
- Modify: `include/MotionStudio/model/PropertyPath.h` 注释
- Test: `tests/model/PropertyPathTest.cpp`（或现有文件追加）

**行为：**

- 仅当 `paintMode == Shader` 时解析成功
- 按 `entries` 中 `name` 找到条目，按 `kind` 返回对应 `AnimatableBase*`（`floatValue` / `float2Value` / `float3Value` / `colorValue`）
- 找不到 → nullptr

- [x] **Step 1: 失败测试 → 实现 → PASS**

```cpp
TEST(PropertyPathTest, ResolvesShaderUniformFloat) {
    // 构造 doc/layer/fill Shader+rippleCount；
    // ResolveAnimatable(doc, {layerId, "styles[0].uniformValues.rippleCount"}) 非空
    // SetStaticValue 或直接改 staticValue 后 evaluate 一致
}
```

- [x] **Step 2: 先更新本 plan 状态，再自动 commit**

```bash
git add include/MotionStudio/model/PropertyPath.h src/model/PropertyPath.cpp \
  tests/model/PropertyPathTest.cpp \
  docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Resolve PropertyPath into shader uniform Animatable values."
```

---

### Task 8: 文档同步 + Spec 状态

**Status:** 待开始

**Files:**
- Modify: `docs/data-model.md` — Document.shaders、Fill/Stroke paint、包内 `shader.json`
- Modify: `docs/color-source-effect.md` — 增加「Core 存储」短节，链到 spec
- Modify: `docs/superpowers/specs/2026-08-07-color-source-core-storage-design.md` — 状态改为「实现中 / Core 计划执行中」

- [ ] **Step 1: 按已实现 API 更新文档（无臆造未做 API）**

- [ ] **Step 2: 全量相关测试**

```bash
cmake --build build --target core_tests -j$(sysctl -n hw.ncpu)
ctest --test-dir build -R 'Vec3Test|ShaderUniformValuesTest|SerializerTest|ShaderCommandTest|PropertyPathTest' --output-on-failure
```

- [ ] **Step 3: 先更新本 plan 全部 Task 状态，再自动 commit**

```bash
git add docs/data-model.md docs/color-source-effect.md \
  docs/superpowers/specs/2026-08-07-color-source-core-storage-design.md \
  docs/superpowers/plans/2026-08-07-color-source-core-storage.md
git commit -m "Document color-source Core storage after implementation."
```

---

## 后续计划（不在本文件展开步骤；均含 UI 或预览，另开 plan）

1. **预览接线：** SceneEvaluator / DrawCommand 携带 shader 快照 → adapter `ColorSourceEffect`；改定义时 invalidate。  
2. **App 包（非本 plan）：** `MotionProjectDocument` 读写 `shader.json`。  
3. **Inspector UI（非本 plan）：** 模式切换、uniform 编辑、源码编辑。  
4. **导出：** PAG/Lottie 明确跳过或栅格。

---

## Spec 覆盖自检

| Spec 项 | Task |
|---|---|
| `shader.json` + Document.shaders | 3, 5 |
| XOR paint + uniformValues | 4, 5, 6 |
| Anim Float/2/3/Color + 可扩展 kind | 1, 3, 7 |
| UniformFormat 上移 | 2 |
| 删被引用失败 | 6 |
| document schema 不升；独立 shader.json version | 5 |
| EntityIndex 不扩 | 3（FindShader 扫描） |
| 导出不支持 | 后续计划 |
| 预览 / App UI | **不在本 plan**（后续） |

## 占位符扫描

无 TBD/TODO 步骤；后续子系统单列，避免本 plan 假完整。
