# PAG RuntimeFilter + BrightnessContrast Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，按 Task 逐步实现。步骤用 checkbox（`- [ ]`）跟踪。

**Goal:** 在 `adapter/tgfx` 落地 PAG 同构的 `RuntimeFilter` 基类，并移植 `BrightnessContrast`，用像素测试验证。

**Architecture:** 移植 PAG `RuntimeFilter`（`tgfx::RuntimeEffect`：输入纹理 → 输出纹理）到 `motion` 命名空间。Pipeline / sampler 按 `filterType()` 缓存在现有 `motion::RenderCache`。`BrightnessContrastFilter::Apply` 对 `Image` 做 `ImageFilter::Runtime`。不改 Core / DrawCommand / App。`ColorSourceEffect` 生产代码零改动；Metal 测试夹具抽到 `TgfxTestGPUEnvironment`，ColorSource 测试改用共享头。

**Tech Stack:** C++17、tgfx Metal `RuntimeEffect`、GoogleTest（`.mm` + Metal）

**Spec:** `docs/superpowers/specs/2026-08-13-pag-runtime-filter-design.md`

## 全局约束

- 不改 Core、`DrawCommand`、`TgfxCanvasAdapter::endLayer`、App UI、PAG 导出
- 不链接 `pag::RuntimeFilter` / `pag::RenderCache` / `pag::Effect`
- 不改 `ColorSourceEffect` 生产代码（RenderCache 只加 filter 缓存 API）
- Metal 测试夹具抽到 `adapter/tgfx/tests/TgfxTestGPUEnvironment.{h,mm}`，禁止在各测试文件再复制一份
- 不移植 Layer Style、FastBlur、其余 PAG Effect
- 不移植 `ComputeVerticesForMotionBlurAndBulge`
- UBO 用 `RenderCache::acquireUniformSlice`，不每帧 `createBuffer`
- 失败返回 `false` / `nullptr`，不引入 PAG `LOGE`
- 禁止 `dynamic_cast` 与 C++ 异常；成员 `= {}` 初始化；`if`/`switch` 分支必须 `{}`
- CMake 已 glob `adapter/tgfx/src` 与 `tests`，不必改 `CMakeLists.txt`
- Commit：英语一句、句号结尾、120 字符内、无其它标点；不 push；在 master 则先建 `feature/{username}_runtime_filter`
- 每完成一步立刻把本 plan 对应 `- [ ]` 改为 `- [x]` 并更新 Task `**Status:**`，与代码一并 commit

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `adapter/tgfx/tests/TgfxTestGPUEnvironment.h` / `.mm` | 共享 Metal 夹具：`TgfxTestGPUEnvironment`、`SaveWebp`、`OutputPath`、像素 helper |
| `adapter/tgfx/tests/ColorSourceEffectTest.mm` | 改用共享夹具；shader / `MakeFivePointStar` 仍留本地 |
| `adapter/tgfx/src/RenderCache.h` / `.cpp` | `FilterResources` + 按 `uint32_t` 缓存 |
| `adapter/tgfx/src/effects/RuntimeFilter.h` / `.cpp` | 滤镜基类：VS、pipeline、`onDraw` |
| `adapter/tgfx/src/effects/BrightnessContrastFilter.h` / `.cpp` | 第一个滤镜：PAG shader + Apply |
| `adapter/tgfx/tests/RenderCacheTest.cpp` | CPU：add / find / releaseAll |
| `adapter/tgfx/tests/BrightnessContrastFilterTest.mm` | GPU 像素测试（include 共享头） |
| `docs/pag-runtime-filter.md` | 调用约定 |
| `docs/README.md` | 索引一行 |
| spec | 状态改为已实现 |

---

### Task 1: 抽出 TgfxTestGPUEnvironment

**Status:** ✅ Done

**Files:**
- Create: `adapter/tgfx/tests/TgfxTestGPUEnvironment.h`
- Create: `adapter/tgfx/tests/TgfxTestGPUEnvironment.mm`
- Modify: `adapter/tgfx/tests/ColorSourceEffectTest.mm`

**Interfaces:**
- Produces: `namespace tgfx_test` 下 `TgfxTestGPUEnvironment`、`Pixel`、`PixelAt`、`ChannelDelta`、`MakeSolidImage`、`ReadCenter`、`SaveWebp`、`OutputPath`
- Produces: 头文件纯 C++（PIMPL 藏 `id<MTLDevice>`）；实现 `.mm`
- Produces: `OutputPath` 在 `.mm` 里用 `__FILE__`，目录仍是 `adapter/tgfx/tests/out/`
- 不抽：`MakeFivePointStar`；不改 `TgfxRenderAdapterTest.cpp`

- [x] **Step 1: 写共享头与实现**

`adapter/tgfx/tests/TgfxTestGPUEnvironment.h`：

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <tgfx/core/Color.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Context.h>

namespace tgfx_test {

class TgfxTestGPUEnvironment {
  public:
    static std::unique_ptr<TgfxTestGPUEnvironment> Make(int width, int height);
    ~TgfxTestGPUEnvironment();

    tgfx::Context *lockContext();
    void unlockContext();
    tgfx::Surface *surface();

  private:
    TgfxTestGPUEnvironment();

    struct Data;
    std::unique_ptr<Data> data_ = nullptr;
};

struct Pixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

Pixel PixelAt(const std::vector<uint8_t> &pixels, int width, int x, int y);
int ChannelDelta(uint8_t a, uint8_t b);
std::shared_ptr<tgfx::Image> MakeSolidImage(tgfx::Context *context, int size, tgfx::Color color);
bool ReadCenter(tgfx::Surface *surface, int size, Pixel *out);
bool SaveWebp(const std::vector<uint8_t> &rgba, int width, int height, const std::string &path);
std::string OutputPath(const std::string &fileName);

}  // namespace tgfx_test
```

`adapter/tgfx/tests/TgfxTestGPUEnvironment.mm`：把 `ColorSourceEffectTest.mm` 里现有 `TgfxTestGPUEnvironment` / `SaveWebp` / `OutputPath` 挪过来，成员放进 `Data`（`mtlDevice`、`device`、`surface`）。丢掉未使用的 `width_` / `height_`。析构必须在 `.mm` 定义（PIMPL）。`PixelAt` / `ChannelDelta` / `MakeSolidImage` / `ReadCenter` 按 plan 原 BrightnessContrast 测试里的实现写进 `.mm`。`SaveWebp` 逻辑与现文件逐行一致。

- [x] **Step 2: ColorSourceEffectTest 改用共享头**

删除匿名命名空间里的 `TgfxTestGPUEnvironment`、`SaveWebp`、`OutputPath`。保留 shader 常量和 `MakeFivePointStar`。

文件顶部改为 include `"TgfxTestGPUEnvironment.h"`，去掉仅夹具需要的 Metal / Bitmap / EncodedFormat / fstream（若无其它引用）。测试体内：

```cpp
using tgfx_test::OutputPath;
using tgfx_test::SaveWebp;
using tgfx_test::TgfxTestGPUEnvironment;
```

`TgfxTestGPUEnvironment::Make` 等调用保持不变。

- [x] **Step 3: 跑 ColorSource 测试确认仍通过**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='ColorSourceEffectTest.*'
```

预期：PASS（与抽取前行为一致；`tests/out/` 路径不变）。

- [x] **Step 4: Commit**

同步本 plan Task 1 checkbox 为 `[x]`、`**Status:** ✅ Done`。

```bash
git add adapter/tgfx/tests/TgfxTestGPUEnvironment.h adapter/tgfx/tests/TgfxTestGPUEnvironment.mm \
  adapter/tgfx/tests/ColorSourceEffectTest.mm \
  docs/superpowers/specs/2026-08-13-pag-runtime-filter-design.md \
  docs/superpowers/plans/2026-08-13-pag-runtime-filter.md
git commit -m "Share Metal test fixtures across tgfx adapter tests."
```

---

### Task 2: RenderCache FilterResources

**Status:** 待开始

**Files:**
- Modify: `adapter/tgfx/src/RenderCache.h`
- Modify: `adapter/tgfx/src/RenderCache.cpp`
- Create: `adapter/tgfx/tests/RenderCacheTest.cpp`

**Interfaces:**
- Produces: `struct FilterResources { shared_ptr<RenderPipeline> pipeline; shared_ptr<Sampler> sampler; }`
- Produces: `FilterResources *findFilterResources(uint32_t type) const`
- Produces: `void addFilterResources(uint32_t type, unique_ptr<FilterResources> resources)`
- Produces: `releaseAll()` 同时清空 `filterResourcesMap_`

- [ ] **Step 1: 写失败测试**

`adapter/tgfx/tests/RenderCacheTest.cpp`：

```cpp
#include <gtest/gtest.h>
#include <memory>

#include "RenderCache.h"

using motion::FilterResources;
using motion::RenderCache;

TEST(RenderCacheTest, StoresAndClearsFilterResources) {
    RenderCache cache;
    EXPECT_EQ(cache.findFilterResources(1), nullptr);

    auto resources = std::make_unique<FilterResources>();
    FilterResources *raw = resources.get();
    cache.addFilterResources(1, std::move(resources));
    EXPECT_EQ(cache.findFilterResources(1), raw);
    EXPECT_EQ(cache.findFilterResources(2), nullptr);

    cache.addFilterResources(1, nullptr);
    EXPECT_EQ(cache.findFilterResources(1), raw);

    cache.releaseAll();
    EXPECT_EQ(cache.findFilterResources(1), nullptr);
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='RenderCacheTest.*'
```

若二进制不在该路径，用 `find build -name tgfx_adapter_test` 定位。预期：FAIL（`FilterResources` / `findFilterResources` 未声明）。

- [ ] **Step 3: 实现 RenderCache 增量**

`RenderCache.h`：在 `namespace tgfx` 前向声明加 `class Sampler;`。在 `UniformBufferSlice` 后、`class RenderCache` 前插入：

```cpp
struct FilterResources {
    std::shared_ptr<tgfx::RenderPipeline> pipeline = nullptr;
    std::shared_ptr<tgfx::Sampler> sampler = nullptr;
};
```

`RenderCache` public 增加：

```cpp
    FilterResources *findFilterResources(uint32_t type) const;
    void addFilterResources(uint32_t type, std::unique_ptr<FilterResources> resources);
```

private 增加：

```cpp
    std::unordered_map<uint32_t, std::unique_ptr<FilterResources>> filterResourcesMap_ = {};
```

头文件已有 `<memory>` / `<unordered_map>`。类注释改为同时服务 ColorSourceEffect 与 RuntimeFilter。

`RenderCache.cpp` 在 `addColorSourcePipeline` 一段之后实现：

```cpp
FilterResources *RenderCache::findFilterResources(uint32_t type) const {
    auto result = filterResourcesMap_.find(type);
    if (result != filterResourcesMap_.end()) {
        return result->second.get();
    }
    return nullptr;
}

void RenderCache::addFilterResources(uint32_t type, std::unique_ptr<FilterResources> resources) {
    if (resources == nullptr) {
        return;
    }
    filterResourcesMap_[type] = std::move(resources);
}
```

`releaseAll()` 里在 `colorSourceSourceKeys_.clear();` 之后加 `filterResourcesMap_.clear();`。

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='RenderCacheTest.*'
```

预期：PASS

- [ ] **Step 5: Commit**

同步本 plan Task 2 checkbox 为 `[x]`、`**Status:** ✅ Done`。spec/plan 已在 Task 1 入库则不必再 add spec。

```bash
git add adapter/tgfx/src/RenderCache.h adapter/tgfx/src/RenderCache.cpp \
  adapter/tgfx/tests/RenderCacheTest.cpp \
  docs/superpowers/plans/2026-08-13-pag-runtime-filter.md
git commit -m "Add RenderCache storage for RuntimeFilter GPU resources."
```

---

### Task 3: RuntimeFilter + BrightnessContrast + 像素测试

**Status:** 待开始

**Files:**
- Create: `adapter/tgfx/src/effects/RuntimeFilter.h`
- Create: `adapter/tgfx/src/effects/RuntimeFilter.cpp`
- Create: `adapter/tgfx/src/effects/BrightnessContrastFilter.h`
- Create: `adapter/tgfx/src/effects/BrightnessContrastFilter.cpp`
- Create: `adapter/tgfx/tests/BrightnessContrastFilterTest.mm`

**Interfaces:**
- Consumes: Task 2 的 `FilterResources` / `findFilterResources` / `addFilterResources` / `acquireUniformSlice`
- Consumes: Task 1 的 `tgfx_test::TgfxTestGPUEnvironment` / `MakeSolidImage` / `ReadCenter` / `Pixel` / `ChannelDelta`
- Produces: `uint32_t NextRuntimeFilterType()`（从 1 递增，跳过 0）
- Produces: `class RuntimeFilter : public tgfx::RuntimeEffect`，公开 `typeId() const` → `filterType()`
- Produces: `BrightnessContrastFilter::Apply(shared_ptr<Image>, RenderCache*, float brightness, float contrast, Point* offset) -> shared_ptr<Image>`
- Produces: uniform `Args` binding 0：`mBrightness = b>0 ? b/250 : b/650`，`mContrast = 1 + c/300`

对照源：`third_party/libpag/src/rendering/filters/RuntimeFilter.{h,cpp}` 与 `BrightnessContrastFilter.{h,cpp}`。`ToVertexPoint` / `ToTexturePoint` 写在 `RuntimeFilter.cpp` 匿名命名空间，不拆 FilterHelper。

- [ ] **Step 1: 写失败测试**

`adapter/tgfx/tests/BrightnessContrastFilterTest.mm`。夹具用 Task 1 的 `tgfx_test`，不要再复制 `TgfxTestGPUEnvironment`。

对比度用例不用 RGB 128（shader 绕 0.5 旋转，128/255≈0.5，几乎不动）。改用 RGB 64，预期被推得更暗、更远离 128。这是 spec 意图，不是放宽断言。

```objc
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "RenderCache.h"
#include "TgfxTestGPUEnvironment.h"
#include "effects/BrightnessContrastFilter.h"

#include <tgfx/core/Canvas.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/Surface.h>

using motion::BrightnessContrastFilter;
using motion::RenderCache;
using tgfx_test::ChannelDelta;
using tgfx_test::MakeSolidImage;
using tgfx_test::Pixel;
using tgfx_test::ReadCenter;
using tgfx_test::TgfxTestGPUEnvironment;
```

```objc
TEST(BrightnessContrastFilterTest, IdentityLeavesOpaqueColorUnchanged) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{128.f / 255.f, 64.f / 255.f, 32.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto filtered = BrightnessContrastFilter::Apply(input, &cache, 0.f, 0.f, &offset);
    ASSERT_NE(filtered, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(filtered);
    Pixel center = {};
    ASSERT_TRUE(ReadCenter(env->surface(), kSize, &center));
    env->unlockContext();

    EXPECT_LE(ChannelDelta(center.r, 128), 2);
    EXPECT_LE(ChannelDelta(center.g, 64), 2);
    EXPECT_LE(ChannelDelta(center.b, 32), 2);
    EXPECT_GE(center.a, 250);
}

TEST(BrightnessContrastFilterTest, PositiveBrightnessRaisesValue) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{128.f / 255.f, 64.f / 255.f, 32.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto filtered = BrightnessContrastFilter::Apply(input, &cache, 100.f, 0.f, &offset);
    ASSERT_NE(filtered, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(filtered);
    Pixel center = {};
    ASSERT_TRUE(ReadCenter(env->surface(), kSize, &center));
    env->unlockContext();

    const int luma = static_cast<int>(center.r) + static_cast<int>(center.g) + static_cast<int>(center.b);
    EXPECT_GT(luma, 128 + 64 + 32 + 20);
}

TEST(BrightnessContrastFilterTest, PositiveContrastPushesAwayFromMidGrey) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{64.f / 255.f, 64.f / 255.f, 64.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto filtered = BrightnessContrastFilter::Apply(input, &cache, 0.f, 100.f, &offset);
    ASSERT_NE(filtered, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(filtered);
    Pixel center = {};
    ASSERT_TRUE(ReadCenter(env->surface(), kSize, &center));
    env->unlockContext();

    EXPECT_LT(static_cast<int>(center.r), 64 - 8);
    EXPECT_LT(static_cast<int>(center.g), 64 - 8);
    EXPECT_LT(static_cast<int>(center.b), 64 - 8);
}

TEST(BrightnessContrastFilterTest, SharesPipelineAcrossInstances) {
    constexpr int kSize = 8;
    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);

    RenderCache cache;
    cache.attachToContext(context);
    const tgfx::Color fill{128.f / 255.f, 64.f / 255.f, 32.f / 255.f, 1.f};
    auto input = MakeSolidImage(context, kSize, fill);
    ASSERT_NE(input, nullptr);

    tgfx::Point offset = {};
    auto first = BrightnessContrastFilter::Apply(input, &cache, 0.f, 0.f, &offset);
    auto second = BrightnessContrastFilter::Apply(input, &cache, 100.f, 0.f, &offset);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    auto *canvas = env->surface()->getCanvas();
    canvas->clear();
    canvas->drawImage(first);
    canvas->drawImage(second);

    auto probe = std::make_shared<BrightnessContrastFilter>(&cache, 0.f, 0.f);
    auto *resources = cache.findFilterResources(probe->typeId());
    ASSERT_NE(resources, nullptr);
    ASSERT_NE(resources->pipeline, nullptr);
    env->unlockContext();
}

TEST(BrightnessContrastFilterTest, ApplyNullInputReturnsNull) {
    RenderCache cache;
    tgfx::Point offset = {};
    EXPECT_EQ(BrightnessContrastFilter::Apply(nullptr, &cache, 0.f, 0.f, &offset), nullptr);
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target tgfx_adapter_test
```

预期：编译失败（找不到 `BrightnessContrastFilter.h`）。

- [ ] **Step 3: 实现 RuntimeFilter**

`adapter/tgfx/src/effects/RuntimeFilter.h`：

```cpp
#pragma once

#include "RenderCache.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <tgfx/core/Image.h>
#include <tgfx/gpu/RuntimeEffect.h>

namespace tgfx {
class GPU;
class CommandEncoder;
class Texture;
class RenderPass;
class RenderPipeline;
class RenderPipelineDescriptor;
class RenderPassDescriptor;
struct Attribute;
struct BindingEntry;
struct Point;
class Rect;
enum class MapDirection;
}  // namespace tgfx

namespace motion {

#define DEFINE_RUNTIME_FILTER_TYPE                            \
    uint32_t filterType() const override {                    \
        static const uint32_t type = NextRuntimeFilterType(); \
        return type;                                          \
    }

uint32_t NextRuntimeFilterType();

class RuntimeFilter : public tgfx::RuntimeEffect {
  public:
    explicit RuntimeFilter(RenderCache *cache,
                           const std::vector<std::shared_ptr<tgfx::Image>> &extraInputs = {});

    uint32_t typeId() const {
        return filterType();
    }

    tgfx::Rect filterBounds(const tgfx::Rect &srcRect, tgfx::MapDirection) const override;
    bool onDraw(tgfx::CommandEncoder *encoder,
                const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                std::shared_ptr<tgfx::Texture> outputTexture,
                const tgfx::Point &offset) const override;

  protected:
    RenderCache *cache = nullptr;

    virtual uint32_t filterType() const = 0;
    virtual tgfx::Rect filterBounds(const tgfx::Rect &srcRect) const;
    virtual std::string onBuildVertexShader() const;
    virtual std::string onBuildFragmentShader() const;
    virtual int sampleCount() const;
    virtual std::vector<tgfx::Attribute> vertexAttributes() const;
    virtual std::vector<tgfx::BindingEntry> uniformBlocks() const;
    virtual std::vector<tgfx::BindingEntry> textureSamplers() const;
    virtual std::vector<float> computeVertices(const tgfx::Texture *source, const tgfx::Texture *target,
                                               const tgfx::Point &offset) const;
    virtual size_t vertexCount() const;
    virtual void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                                  const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                                  const tgfx::Point &offset) const;
    virtual void onConfigurePipeline(tgfx::RenderPipelineDescriptor *descriptor) const;
    virtual std::unique_ptr<FilterResources> onCreateFilterResources() const;
    virtual void onConfigureRenderPass(tgfx::RenderPassDescriptor *desc, FilterResources *resources, tgfx::GPU *gpu,
                                       const std::shared_ptr<tgfx::Texture> &outputTexture) const;

    FilterResources *getFilterResources(tgfx::GPU *gpu) const;

  private:
    std::shared_ptr<tgfx::RenderPipeline> createPipeline(tgfx::GPU *gpu) const;
};

}  // namespace motion
```

前向声明若导致编译错误：改为直接 `#include` 对应 tgfx 头（`Attribute.h`、`CommandEncoder.h`、`GPU.h`、`RenderPass.h`、`RenderPipeline.h`、`Texture.h`、`Rect.h`、`Point.h`），不要改语义。

`RuntimeFilter.cpp` 按 PAG `RuntimeFilter.cpp` 移植，差异如下：

1. `namespace motion`；include `"RuntimeFilter.h"` 与 `"RenderCache.h"`；不要 PAG `Log.h` / `FilterHelper.h`。
2. `NextRuntimeFilterType()`：`static std::atomic<uint32_t> next{1};`，`fetch_add`，跳过 0。
3. `#version` 前缀与 ColorSourceEffect 相同：

```cpp
std::string GetVersionPrefix(tgfx::GPU *gpu) {
    auto info = gpu->info();
    const bool isDesktop = info->version.find("OpenGL ES") == std::string::npos;
    return isDesktop ? "#version 150\n\n" : "#version 300 es\n\n";
}
```

4. 默认 VS / 默认 FS 从 PAG 原样复制（`aPosition`+`aTextureCoord` → `vertexColor`；FS 采样 `sTexture`）。
5. 匿名命名空间：

```cpp
tgfx::Point ToTexturePoint(const tgfx::Texture *source, const tgfx::Point &texturePoint) {
    return {texturePoint.x / static_cast<float>(source->width()),
            texturePoint.y / static_cast<float>(source->height())};
}

tgfx::Point ToVertexPoint(const tgfx::Texture *target, const tgfx::Point &point) {
    return {2.0f * point.x / static_cast<float>(target->width()) - 1.0f,
            2.0f * point.y / static_cast<float>(target->height()) - 1.0f};
}
```

6. `createPipeline`：PAG 的 vertex layout / blend / `textureSamplers` / `uniformBlocks` / `sampleCount` / `onConfigurePipeline`。另外设 `colorAttachment.format = tgfx::PixelFormat::RGBA_8888`，以及 vertex/fragment `entryPoint = "main"`（与 ColorSourceEffect 一致，Metal 需要）。编译失败返回 `nullptr`，不打日志。
7. `getFilterResources`：`cache == nullptr` 返回 `nullptr`。否则 `findFilterResources(filterType())`；没有则 `createPipeline` + `gpu->createSampler(ClampToEdge, Linear, MipmapMode::None)`，`onCreateFilterResources()` 填 pipeline/sampler，`addFilterResources`。
8. `onDraw`：对齐 PAG（含 `sampleCount()>1` 的 MSAA 分支）。无 `LOGE`：参数非法 / 无 resources / beginRenderPass 失败 / 建 VBO 失败则 `return false`。`setTexture(0, inputTextures[0], sampler)`，额外输入从 1 起绑定同一 sampler。然后 `onUpdateUniforms`，`draw(TriangleStrip, vertexCount())`。
9. `computeVertices`：对齐 PAG 四顶点 strip（content 用 `filterBounds(input)` + offset，UV 用 input bounds）。
10. 默认：`filterBounds(src)=src`；`sampleCount()=1`；`vertexCount()=4`；`uniformBlocks()={}`；`textureSamplers()={{"sTexture",0}}`；`onUpdateUniforms` / `onConfigurePipeline` / `onConfigureRenderPass` 空；`onCreateFilterResources` 返回 `make_unique<FilterResources>()`。
11. 公开 override `filterBounds(src, MapDirection)` 转调 protected `filterBounds(src)`。
12. 构造函数：`RuntimeEffect(extraInputs), cache(cache)`。

需要的 tgfx 头（按编译补齐）：`Attribute.h`、`CommandEncoder.h`、`GPU.h`、`GPUBuffer.h`、`PixelFormat.h`、`RenderPass.h`、`RenderPipeline.h`、`Sampler.h`、`ShaderModule.h`、`ShaderStage.h`、`Texture.h`、`Rect.h`、`Point.h`、`Color.h`（MSAA `PMColor::Transparent`）。

- [ ] **Step 4: 实现 BrightnessContrastFilter**

`adapter/tgfx/src/effects/BrightnessContrastFilter.h`：

```cpp
#pragma once

#include "RuntimeFilter.h"

namespace motion {

class BrightnessContrastFilter : public RuntimeFilter {
  public:
    static std::shared_ptr<tgfx::Image> Apply(std::shared_ptr<tgfx::Image> input, RenderCache *cache, float brightness,
                                              float contrast, tgfx::Point *offset);

    BrightnessContrastFilter(RenderCache *cache, float brightness, float contrast);

  protected:
    DEFINE_RUNTIME_FILTER_TYPE

    std::string onBuildFragmentShader() const override;
    std::vector<tgfx::BindingEntry> uniformBlocks() const override;
    void onUpdateUniforms(tgfx::RenderPass *renderPass, tgfx::GPU *gpu,
                          const std::vector<std::shared_ptr<tgfx::Texture>> &inputTextures,
                          const tgfx::Point &offset) const override;

  private:
    float brightness = 0.f;
    float contrast = 0.f;
};

}  // namespace motion
```

`BrightnessContrastFilter.cpp`：

- `FRAGMENT_SHADER` **整段复制** `third_party/libpag/src/rendering/filters/BrightnessContrastFilter.cpp` 里的 raw string（HSV、`EPSILON`、预乘 `rgb * a`）。不要改算法。
- `Apply`：`input == nullptr` 返回 `nullptr`。否则 `make_shared<BrightnessContrastFilter>(cache, brightness, contrast)`，`return input->makeWithFilter(tgfx::ImageFilter::Runtime(filter), offset);`
- 构造函数：`RuntimeFilter(cache), brightness(brightness), contrast(contrast)`
- `uniformBlocks`：`return {{"Args", 0}};`
- `onUpdateUniforms`：

```cpp
struct Uniforms {
    float brightness = 0.0f;
    float contrast = 0.0f;
};
Uniforms uniforms = {};
uniforms.brightness = brightness > 0 ? brightness / 250.f : brightness / 650.f;
uniforms.contrast = 1.0f + contrast / 300.f;
if (cache == nullptr) {
    return;
}
auto slice = cache->acquireUniformSlice(sizeof(Uniforms));
if (slice.buffer == nullptr) {
    return;
}
void *mapped = slice.buffer->map(slice.offset, sizeof(Uniforms));
if (mapped == nullptr) {
    return;
}
std::memcpy(mapped, &uniforms, sizeof(Uniforms));
slice.buffer->unmap();
renderPass->setUniformBuffer(0, slice.buffer, slice.offset, sizeof(Uniforms));
```

include：`BrightnessContrastFilter.h`、`RenderCache.h`、`<cstring>`、`tgfx/core/ImageFilter.h`、`tgfx/gpu/GPU.h`、`tgfx/gpu/GPUBuffer.h`、`tgfx/gpu/RenderPass.h`。

- [ ] **Step 5: 跑测试确认通过**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='BrightnessContrastFilterTest.*:RenderCacheTest.*'
```

预期：全部 PASS。若 Identity 差 1–2 属 GPU 量化，断言已是 `<= 2`。若 Contrast 方向反了，先核对 uniform 映射是否与 PAG 一致，禁止改 shader 凑测试。

- [ ] **Step 6: Commit**

同步本 plan Task 3 checkbox 与 `**Status:** ✅ Done`。

```bash
git add adapter/tgfx/src/effects/RuntimeFilter.h adapter/tgfx/src/effects/RuntimeFilter.cpp \
  adapter/tgfx/src/effects/BrightnessContrastFilter.h adapter/tgfx/src/effects/BrightnessContrastFilter.cpp \
  adapter/tgfx/tests/BrightnessContrastFilterTest.mm \
  docs/superpowers/plans/2026-08-13-pag-runtime-filter.md
git commit -m "Port RuntimeFilter and BrightnessContrast from PAG."
```

---

### Task 4: 文档

**Status:** 待开始

**Files:**
- Create: `docs/pag-runtime-filter.md`
- Modify: `docs/README.md`（索引表在 `color-source-effect.md` 行后插入一行）
- Modify: `docs/superpowers/specs/2026-08-13-pag-runtime-filter-design.md`（状态改为已实现）

**Interfaces:**
- Consumes: Task 3 的 `Apply` / `typeId` / `filterType` 缓存键
- Produces: 调用约定文档，对标 `docs/color-source-effect.md`

- [ ] **Step 1: 写 `docs/pag-runtime-filter.md`**

内容必须包含：

1. 相关路径：`RuntimeFilter.*`、`BrightnessContrastFilter.*`、`RenderCache`、测试文件。
2. 职责表：`RuntimeFilter`（每次 Apply 新实例）/ `RenderCache`（按 `filterType` 共享 pipeline）/ `ColorSourceEffect`（过程色 fill，不是后处理）。
3. 调用：

```
auto image = BrightnessContrastFilter::Apply(input, &cache, brightness, contrast, &offset);
canvas->drawImage(image);
```

参数不同必须多实例；pipeline 仍共享。
4. 缓存键 = `filterType()`（`DEFINE_RUNTIME_FILTER_TYPE` 静态 id），不是 `EntityId`。shader 是编译期常量，无 invalidate。
5. UBO 走 `acquireUniformSlice`。
6. 本阶段未接入 `endLayer` / Core effects。

- [ ] **Step 2: 更新 `docs/README.md` 索引**

在 `color-source-effect.md` 那一行后面插入：

```
| [pag-runtime-filter.md](pag-runtime-filter.md) | RuntimeFilter / BrightnessContrast：PAG 图层后处理滤镜基类、pipeline 缓存、Apply 约定 |
```

spec 文首「状态：待实现」改为「状态：已实现（plan Tasks 1–4）」。

- [ ] **Step 3: Commit**

同步本 plan Task 4 为 ✅ Done。

```bash
git add docs/pag-runtime-filter.md docs/README.md \
  docs/superpowers/specs/2026-08-13-pag-runtime-filter-design.md \
  docs/superpowers/plans/2026-08-13-pag-runtime-filter.md
git commit -m "Document RuntimeFilter apply and pipeline cache conventions."
```

---

## Spec 覆盖对照

| Spec | Task |
|---|---|
| §5 共享 `TgfxTestGPUEnvironment` + ColorSource 改用 | Task 1 |
| §1 职责拆分、Apply 数据流 | Task 3 |
| §2 文件布局 | Task 1–4 |
| §3.1 RuntimeFilter + NextRuntimeFilterType | Task 3 |
| §3.2 RenderCache FilterResources | Task 2 |
| §3.3 BrightnessContrast Apply | Task 3 |
| §4 shader 原样 + uniform 映射 | Task 3 |
| §5 四则 GPU 用例 + null input | Task 3（对比度用 RGB 64，见 Step 1 说明） |
| §6 文档 | Task 4 |
| §7 nullptr / 无 cache 返回 false | Task 3 `onDraw` / `Apply` |
| §8 非目标 | 全局约束 |
| `typeId()` | spec 测试需要 `findFilterResources(type)`；protected `filterType` 不够，公开 `typeId()` 转发 |
