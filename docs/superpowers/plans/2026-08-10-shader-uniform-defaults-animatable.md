# Shader Uniform 默认值 / 可动画 / Color Format — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**Goal:** Scheme uniform 支持 `animatable` + 类型化默认值；`UniformFormat::Color` 与 `Float4`（`Vec4`）分离；Inspector 窄栏下 float2/3/4 可正常输入。

**Architecture:** 扩展 `ShaderUniformDecl`（JSON 可选字段）。新增 `common/Vec4`，按 `Vec3` 模式把 `AnimFloat4` 接到 Animatable / PropertyPath / 求值 / adapter。`Color` → `AnimColor` + ColorPicker；`Float4` → `AnimFloat4` + 四轴数值。`animatable` 变为 false 时 Realign 用帧 0 拍平关键帧。默认值仅用于新绑定 / Realign 新增项。

**Tech Stack:** C++17 core、GoogleTest、bridge C ABI、SwiftUI App

**Spec:** `docs/superpowers/specs/2026-08-10-shader-uniform-defaults-animatable-design.md`

## 全局约束

- 不提升 `document.json` / 包 `schemaVersion`
- 不自动把旧 `"format":"float4"` 改写成 `"color"`
- 不新增 Static* `ShaderUniformValueKind`；不可动画 = 无关键帧 + 仅 static
- `UniformFormat::Color` 与 `AnimatableType::Vec4` 追加在各自枚举**末尾**（不重排既有项）
- `MS_UNIFORM_FORMAT_COLOR = 4` 为 UI 子集，用 switch 映射（C++ `Color` 在全量枚举末尾——禁止二者 `static_cast`）
- Commit 步骤：遵循仓库 git-workflow；不 push

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/common/Vec4.h`, `src/common/Vec4.cpp` | `Vec4` + `ApproxEqual` |
| `include/MotionStudio/animation/AnimatableType.h`, `Interpolator.*`, `Animatable.cpp` | `Vec4` 动画类型 |
| `include/MotionStudio/common/UniformFormat.h`, `src/common/UniformFormat.cpp`, `Dto.cpp` | `Color` format + 字符串 |
| `include/MotionStudio/model/ShaderDefinition.h` | decl：`animatable` + defaults |
| `include/MotionStudio/model/ShaderUniformValues.h`, `src/model/ShaderUniformValues.cpp` | `float4Value`、Kind、MakeDefault、Realign 拍平 |
| `include/MotionStudio/render/ShaderPaint.h`, `SceneEvaluator.cpp` | 求值 `AnimFloat4` |
| `src/model/PropertyPath.cpp`, `src/undo/CommandHelpers.cpp` | 解析/写 `AnimFloat4` |
| `src/serialization/Serializer.cpp` | decl/value JSON；`Vec4` |
| `adapter/tgfx/src/TgfxCanvasAdapter.cpp` | `WriteShaderUniformValues` 写 float4 |
| `bridge/include/motionstudio_bridge.h` + shader/property cpp | Color、animatable/default、vec4 API |
| `ShaderEditorSheet.swift`, `StyleShaderPaintControls.swift`, bridging extensions | Editor + Inspector UI |
| `docs/data-model.md` | 文档同步 |
| Tests | `Vec4Test`、`ShaderUniformValuesTest`、serializer/bridge 按需 |

---

### Task 1: Vec4 + Animatable 接线

**Status:** Pending

**Files:**
- Create: `include/MotionStudio/common/Vec4.h`, `src/common/Vec4.cpp`, `tests/common/Vec4Test.cpp`
- Modify: `include/MotionStudio/animation/AnimatableType.h`, `include/MotionStudio/animation/Interpolator.h`, `src/animation/Interpolator.cpp`, `src/animation/Animatable.cpp`
- Modify: `src/serialization/Serializer.cpp`（`Vec4ToJson` / `Vec4FromJson` / `ValueToJson` / `FromJson<Vec4>`）
- Modify: `src/undo/CommandHelpers.cpp`（所有 `AnimatableType` switch + `Keyframe` 变体——对齐 `Vec3`）

**Interfaces:**
- Produces: `struct Vec4 { float x,y,z,w; }`；`AnimatableType::Vec4`；`Interpolator<Vec4>::Lerp`；`Animatable<Vec4>` 显式实例化；序列化 `[x,y,z,w]`

- [ ] **Step 1: 写失败测试**

`tests/common/Vec4Test.cpp`（对齐 `Vec3Test.cpp`）：

```cpp
#include <gtest/gtest.h>
#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/common/Vec4.h"

using motion::ApproxEqual;
using motion::Interpolator;
using motion::Vec4;

TEST(Vec4Test, ArithmeticAndApproxEqual) {
    Vec4 a{1, 2, 3, 4};
    Vec4 b{4, 5, 6, 7};
    EXPECT_EQ((a + b), (Vec4{5, 7, 9, 11}));
    EXPECT_EQ((b - a), (Vec4{3, 3, 3, 3}));
    EXPECT_EQ((a * 2.f), (Vec4{2, 4, 6, 8}));
    EXPECT_TRUE(ApproxEqual(a, Vec4{1, 2, 3, 4}));
}

TEST(Vec4Test, InterpolatorLerpMidpoint) {
    Vec4 mid = Interpolator<Vec4>::Lerp(Vec4{0, 0, 0, 0}, Vec4{2, 4, 6, 8}, 0.5f);
    EXPECT_TRUE(ApproxEqual(mid, Vec4{1, 2, 3, 4}));
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='Vec4Test.*'
```

预期：FAIL（头文件不存在或未注册）

- [ ] **Step 3: 实现 Vec4 + Interpolator + AnimatableType + 显式实例化**

`Vec4.h` / `Vec4.cpp` 照抄 `Vec3` 四分量。  
`AnimatableType` 在枚举**末尾**加 `Vec4`（在 `String` 之后）。  
`Interpolator<Vec4>::Lerp` 分量 lerp。  
`Animatable.cpp`：`valueType()` → `AnimatableType::Vec4`；`template class Animatable<Vec4>;`

Serializer：`Vec4ToJson` / `Vec4FromJson`（4 元数组），挂到 `ValueToJson` / `FromJson`，使 `AnimatableToJson<Vec4>` 可用。

CommandHelpers：每个 `switch (valueType())` 为 `Vec4` 增加与 `Vec3` 相同的分支；`std::get_if<Keyframe<Vec4>>` 同理。

- [ ] **Step 4: 跑测试确认通过**

```bash
./build/tests/core_tests --gtest_filter='Vec4Test.*'
```

预期：PASS

- [ ] **Step 5: Commit**

```bash
git add include/MotionStudio/common/Vec4.h src/common/Vec4.cpp tests/common/Vec4Test.cpp \
  include/MotionStudio/animation/AnimatableType.h include/MotionStudio/animation/Interpolator.h \
  src/animation/Interpolator.cpp src/animation/Animatable.cpp \
  src/serialization/Serializer.cpp src/undo/CommandHelpers.cpp
git commit -m "Add Vec4 and wire it through Animatable interpolation."
```

---

### Task 2: UniformFormat::Color + KindForFormat 拆分

**Status:** Pending

**Files:**
- Modify: `include/MotionStudio/common/UniformFormat.h`, `src/common/UniformFormat.cpp`
- Modify: `src/serialization/Dto.cpp`（`ToString` / `uniformFormatFromString`）
- Modify: `src/model/ShaderUniformValues.cpp`（`KindForFormat`）
- Modify: `include/MotionStudio/model/ShaderUniformValues.h`（注释：Float4→AnimFloat4，Color→AnimColor）
- Modify: `tests/model/ShaderUniformValuesTest.cpp` 及所有把 `Float4` 当颜色的测试（改为 `UniformFormat::Color`）
- Grep 更新：`tests/undo/ShaderCommandTest.cpp`、`tests/model/LayerStylePaintModeTest.cpp`、`tests/model/PropertyPathTest.cpp` 等 `Float4` tint 用例 → `Color`

**Interfaces:**
- Produces: `UniformFormat::Color`（枚举末尾）；GLSL `"vec4"`；JSON `"color"`；`KindForFormat(Float4)=AnimFloat4`，`KindForFormat(Color)=AnimColor`

- [ ] **Step 1: 改失败测试**

把 `MakeDefaultsForFloatAndColor` 改为：

```cpp
TEST(ShaderUniformValuesTest, MakeDefaultsForFloatAndColor) {
    std::vector<ShaderUniformDecl> decls = {
        {"rippleCount", UniformFormat::Float, 1},
        {"tint", UniformFormat::Color, 1},
    };
    auto values = MakeDefaultUniformValues(decls);
    ASSERT_EQ(values.entries.size(), 2u);
    EXPECT_EQ(values.entries[0].kind, ShaderUniformValueKind::AnimFloat);
    EXPECT_EQ(values.entries[1].kind, ShaderUniformValueKind::AnimColor);
}

TEST(ShaderUniformValuesTest, Float4MapsToAnimFloat4) {
    auto kind = KindForFormat(UniformFormat::Float4);
    ASSERT_TRUE(kind.hasValue());
    EXPECT_EQ(*kind, ShaderUniformValueKind::AnimFloat4);
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='ShaderUniformValuesTest.*'
```

预期：FAIL（无 `Color`，或 Float4 仍映射 AnimColor）

- [ ] **Step 3: 实现 format + KindForFormat**

`UniformFormat` 末尾追加 `Color`。  
`UniformFormatGLSLTypeName` / `ByteSize`：与 Float4 相同；非 sampler。  
Dto：`"color"` ↔ `UniformFormat::Color`。  
`KindForFormat`：`Float4 → AnimFloat4`，`Color → AnimColor`。

- [ ] **Step 4: 相关测试全部通过**

```bash
./build/tests/core_tests --gtest_filter='ShaderUniformValuesTest.*:ShaderCommandTest.*:PropertyPathTest.*:LayerStylePaintModeTest.*'
```

预期：PASS（tint 已改为 Color）

- [ ] **Step 5: Commit**

```bash
git commit -m "Add UniformFormat Color and map Float4 to AnimFloat4."
```

---

### Task 3: Decl 默认值 + animatable + Realign 拍平

**Status:** Pending

**Files:**
- Modify: `include/MotionStudio/model/ShaderDefinition.h`
- Modify: `src/model/ShaderUniformValues.cpp`（`MakeDefaultUniformValue(decl)` 读 default；Realign 拍平）
- Modify: `src/serialization/Serializer.cpp`（`ShaderUniformDeclToJson` / `FromJson`）
- Modify: `tests/model/ShaderUniformValuesTest.cpp`
- 可选：已有 shader serializer 测试则扩展 round-trip

**Interfaces:**
- Consumes: Task 1 `Vec4`；Task 2 `Color` / `AnimFloat4`
- Produces: decl 字段 `animatable` + `defaultFloat*` / `defaultColor`；JSON 可选 `animatable` / `default`

- [ ] **Step 1: 写失败测试**

```cpp
TEST(ShaderUniformValuesTest, DefaultFromDeclOnMake) {
    ShaderUniformDecl center{"center", UniformFormat::Float2, 1};
    center.animatable = false;
    center.defaultFloat2 = Vec2{50.f, 200.f};
    auto values = MakeDefaultUniformValues({center});
    ASSERT_EQ(values.entries.size(), 1u);
    EXPECT_FALSE(values.entries[0].float2Value.hasKeyframes());
    EXPECT_FLOAT_EQ(values.entries[0].float2Value.staticValue().x, 50.f);
    EXPECT_FLOAT_EQ(values.entries[0].float2Value.staticValue().y, 200.f);
}

TEST(ShaderUniformValuesTest, RealignFlattensKeyframesWhenNotAnimatable) {
    ShaderUniformValue prev;
    prev.name = "offset";
    prev.kind = ShaderUniformValueKind::AnimFloat;
    prev.floatValue.setStaticValue(0.f);
    prev.floatValue.addKeyframe(Keyframe<float>{0, 0.f});
    prev.floatValue.addKeyframe(Keyframe<float>{10, 1.f});
    ShaderUniformValues previous;
    previous.entries.push_back(prev);

    ShaderUniformDecl decl{"offset", UniformFormat::Float, 1};
    decl.animatable = false;
    auto realigned = RealignUniformValues({decl}, previous);
    ASSERT_TRUE(realigned.hasValue());
    EXPECT_FALSE(realigned->entries[0].floatValue.hasKeyframes());
    EXPECT_FLOAT_EQ(realigned->entries[0].floatValue.staticValue(), 0.f);  // evaluate(0)
}

TEST(ShaderUniformValuesTest, ChangingDefaultDoesNotTouchExisting) {
    ShaderUniformValue prev;
    prev.name = "ringCount";
    prev.kind = ShaderUniformValueKind::AnimFloat;
    prev.floatValue.setStaticValue(20.f);
    ShaderUniformValues previous;
    previous.entries.push_back(prev);

    ShaderUniformDecl decl{"ringCount", UniformFormat::Float, 1};
    decl.defaultFloat = 99.f;
    auto realigned = RealignUniformValues({decl}, previous);
    ASSERT_TRUE(realigned.hasValue());
    EXPECT_FLOAT_EQ(realigned->entries[0].floatValue.staticValue(), 20.f);
}
```

（按项目 `Animatable` / `Keyframe` 真实 API 微调 `hasKeyframes` / `addKeyframe` 等调用名。）

- [ ] **Step 2: 跑测试确认失败**

```bash
./build/tests/core_tests --gtest_filter='ShaderUniformValuesTest.Default*:ShaderUniformValuesTest.Realign*:ShaderUniformValuesTest.Changing*'
```

- [ ] **Step 3: 实现 decl 字段 + MakeDefault + Realign + JSON**

`ShaderUniformDecl` 按 spec 加字段（默认：`animatable=true`；float/vec 为零；`defaultColor={1,1,1,1}`）。

`MakeDefaultUniformValue`：按 format `setStatic` 对应 default。  
`Realign`：同名同 kind 保留；若 `!decl.animatable && hasKeyframes` → `setStatic(evaluate(0))` + `clearKeyframes()`。

`ShaderUniformDeclToJson`：写 `name/format/count`；实现上**始终写出** `animatable` 与 `default`（便于 Editor round-trip）；读取时缺省兼容。  
`FromJson`：缺 `animatable`→true；缺 `default`→类型默认；`default` 按 format 解析（float 数字；float2/3/4 数组；color `#RRGGBBAA`）。

- [ ] **Step 4: 测试通过 + JSON 往返（可加 Serializer 单测）**

- [ ] **Step 5: Commit**

```bash
git commit -m "Add shader uniform decl defaults and animatable flatten on realign."
```

---

### Task 4: AnimFloat4 端到端（属性路径 + GPU）

**Status:** Pending

**Files:**
- Modify: `include/MotionStudio/model/ShaderUniformValues.h`（`Animatable<Vec4> float4Value`）
- Modify: `include/MotionStudio/render/ShaderPaint.h`（`EvaluatedShaderUniform` 增 `Vec4 float4Value`）
- Modify: `src/model/PropertyPath.cpp`（`resolveUniformEntry` → `&entry.float4Value`）
- Modify: `src/render/SceneEvaluator.cpp`（`EvaluateUniformValues` case AnimFloat4）
- Modify: `src/serialization/Serializer.cpp`（AnimFloat4 读写 `float4Value`，不再 unsupported）
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`（`WriteShaderUniformValues` 写 4 floats）
- Test: PropertyPath / Serializer / 现有 paint 测试按需

**Interfaces:**
- Produces: 完整 `AnimFloat4` 属性路径与绘制上传

- [ ] **Step 1: 写失败测试**

在 `PropertyPathTest`（或新建）绑定 Float4 uniform，`setStatic` / evaluate 四分量；或 Serializer round-trip `animFloat4` + `float4Value: {static:[1,2,3,4]}`。

- [ ] **Step 2: 跑测试确认失败**

- [ ] **Step 3: 实现 value 字段 + resolve + evaluate + serialize + adapter**

```cpp
// EvaluateUniformValues
case ShaderUniformValueKind::AnimFloat4:
    evaluated.float4Value = entry.float4Value.evaluatePreview(time);
    break;

// WriteShaderUniformValues
case ShaderUniformValueKind::AnimFloat4: {
    const float v[4] = {value.float4Value.x, value.float4Value.y,
                        value.float4Value.z, value.float4Value.w};
    uniformData->setData(value.name, v, sizeof(v));
    break;
}
```

Color 仍走 `ToTgfxColor`。

- [ ] **Step 4: 测试通过**

- [ ] **Step 5: Commit**

```bash
git commit -m "Wire AnimFloat4 through property path evaluation and GPU upload."
```

---

### Task 5: Bridge API

**Status:** Pending

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_shader.cpp`
- Modify: `bridge/src/common/motionstudio_bridge_property.cpp`
- Modify: `apps/.../MotionDocumentCore.swift`、`MotionStudioBridgingExtension.swift`
- Test: `bridge/tests/BridgeTest.cpp` 按需

**Interfaces:**
- Produces:
  - `MS_UNIFORM_FORMAT_COLOR = 4`
  - `bool ms_document_shader_uniform_animatable_at(...)`
  - default getters（或一次 JSON；推荐按 format：float / xy / xyz / xyzw / rgba）
  - `ms_property_evaluate_vec4` / `set_static_vec4` / `add_keyframe_vec4`（若 vec3 有对称 remove 则一并补）
  - `ToMSUniformFormat` 识别 `Color`
  - 对不可动画 uniform：Inspector 不调用 `addKeyframe*`；Core Realign 已拍平。Bridge 可选在 `addKeyframe*` 前查 scheme，不可动画则改走 setStatic（或返回 false）

- [ ] **Step 1: 扩展 header + Swift wrapper（先暴露编译缺口）**

- [ ] **Step 2: 实现 C API + ToMSUniformFormat(Color)**

editable 注释改为：UI 子集 0–4，经 switch 映射，禁止 `static_cast<UniformFormat>`。

- [ ] **Step 3: Bridge 测试或最小 smoke**

- [ ] **Step 4: Commit**

```bash
git commit -m "Expose shader uniform Color animatable defaults and vec4 properties."
```

---

### Task 6: App UI（Editor + Inspector）

**Status:** Pending

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/ProjectPanel/ShaderEditorSheet.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/StyleShaderPaintControls.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Bridge/MotionStudioBridgingExtension.swift`（`editableCases` 含 `.COLOR`）
- Docs: `docs/data-model.md`（Float4≠Color；decl 字段）；spec 状态 → 已实现

**Interfaces:**
- Consumes: Task 5 bridge
- Produces: Editor 可编 animatable/default；Inspector 紧凑 vec 行；`animatable==false` 无钻石且只 static

- [ ] **Step 1: 修 float2/3/4 布局（可先不接 animatable）**

`StyleShaderPaintControls`：`FLOAT2/3/4` 改为

```swift
VStack(alignment: .leading, spacing: 4) {
    HStack {
        Text(name).font(.callout)
        Spacer(minLength: 0)
        if animatable {
            // 整向量一颗钻石
        }
    }
    HStack(spacing: 6) {
        compactAxisField("X", ...)
        compactAxisField("Y", ...)
        // 需要时 Z/W
    }
}
```

`compactAxisField`：短标签（约 12–16pt）+ `TextField`，**不要**嵌套完整 `NumberPropertyRow`（其内置 78pt label）。

`FLOAT4`：四轴；`COLOR`：ColorPicker（从原 FLOAT4 分支移出）。

- [ ] **Step 2: ShaderEditorSheet 草稿字段**

`ShaderUniformDraft` 增加 `animatable: Bool`、`default`（按 format 存）。  
Add/Edit：Toggle + default 控件。  
Save：uniforms JSON 含 `animatable` + `default`。

- [ ] **Step 3: Inspector 读 `animatable`**

钻石显隐；写入只走 `setStatic*`。

- [ ] **Step 4: 手动验证**

App：Concentric `center` float2 可输入 50/200；新建 color uniform 为 ColorPicker；float4 为四框；关 Animatable 后无钻石。

- [ ] **Step 5: 更新文档 + spec 状态 + Commit**

```bash
git commit -m "Add shader uniform editor defaults and fix vector inspector layout."
```

---

## Spec 覆盖检查

| Spec 要求 | Task |
|---|---|
| float2 可输入 | 6 |
| decl 上 animatable + defaults | 3, 5, 6 |
| Realign 拍平 evaluate(0) | 3 |
| 默认值仅新建/新增 | 3 |
| UniformFormat::Color | 2, 5, 6 |
| Float4 → AnimFloat4 + Vec4 | 1, 2, 4 |
| 旧 float4 不迁移 | 2（无改写逻辑） |
| Color 与 Float4 均上传 GPU vec4 | 2（GLSL 名）、4（upload） |

## 一致性备注

- Bridge `MS_UNIFORM_FORMAT_COLOR = 4` 与 C++ `Color` 末尾序不同：一律 switch 映射
- 现有测试里把 `Float4` 当 tint 的 → 全部改为 `Color`
- `Animatable` / `Keyframe` API 名以仓库代码为准（Step 1 测试按实际微调）
