# RuntimeFilter 与 BrightnessContrast

`adapter/tgfx` 下的图层后处理滤镜：输入纹理 → 输出纹理，经 tgfx `RuntimeEffect` / `ImageFilter::Runtime` 接到 `Image`。本文说明职责拆分、pipeline 缓存与 `Apply` 约定。

过程色填充见 [color-source-effect.md](color-source-effect.md)，不是后处理，不要混用。

相关代码：

- `adapter/tgfx/src/effects/RuntimeFilter.{h,cpp}`
- `adapter/tgfx/src/effects/BrightnessContrastFilter.{h,cpp}`
- `adapter/tgfx/src/RenderCache.{h,cpp}`
- `adapter/tgfx/tests/BrightnessContrastFilterTest.mm`
- `adapter/tgfx/tests/RenderCacheTest.cpp`

## 1. 职责拆分

| 层级 | 角色 | 生命周期 |
|---|---|---|
| `RuntimeFilter` 子类 | 轻量实例：滤镜参数（如 brightness / contrast）、指向 `RenderCache` | **每次 `Apply` 新建**；参数不同必须多实例 |
| `RenderCache` | 重资源：`FilterResources`（pipeline + sampler），按 `filterType()` 共享 | 跟 tgfx `Context` 绑定；context 切换时 `releaseAll` |
| `ColorSourceEffect` | 过程色 fill（Shadertoy `mainImage`），不是图层后处理 | 见 [color-source-effect.md](color-source-effect.md) |

```
BrightnessContrastFilter::Apply(input, &cache, brightness, contrast, &offset)
        │  每次新建 filter 实例
        │  input->makeWithFilter(ImageFilter::Runtime(filter), offset)
        ▼
canvas->drawImage(image)
```

## 2. 调用约定

```cpp
tgfx::Point offset = {};
auto image = BrightnessContrastFilter::Apply(input, &cache, brightness, contrast, &offset);
canvas->drawImage(image);
```

- `input == nullptr` 时 `Apply` 返回 `nullptr`
- 参数不同必须多实例；GPU `RenderPipeline` 仍按 `filterType()` 共享
- 同一次 flush 内不要复用同一个 `RuntimeEffect` 改参数再画（tgfx 离屏结果按 effect 对象身份缓存）

BrightnessContrast 参数与 PAG 一致：`brightness` / `contrast` 为 AE 风格数值。写入 UBO 时：

- `mBrightness = brightness > 0 ? brightness / 250 : brightness / 650`
- `mContrast = 1 + contrast / 300`

## 3. Pipeline 缓存

缓存键 = `filterType()`（`DEFINE_RUNTIME_FILTER_TYPE` 生成的进程内静态 `uint32_t`），**不是** `EntityId`。

- shader 是编译期常量，无源码编辑路径，**不必** `invalidate`
- 公开 `typeId()` 转发 `filterType()`，测试可用 `cache.findFilterResources(probe->typeId())`
- `releaseAll()` 会清空 `filterResourcesMap_`

## 4. Uniform buffer

`onUpdateUniforms` 经 `RenderCache::acquireUniformSlice` 写入 tgfx `GlobalCache` UBO 池，不每帧 `createBuffer`。帧推进与 ColorSourceEffect 相同，见 [color-source-effect.md](color-source-effect.md) §4.2。

## 5. 本阶段边界

未接入 `TgfxCanvasAdapter::endLayer`、Core `Layer::effects`、App UI、PAG 导出 Effect 块。预览/文档里暂时看不到滤镜效果；像素测试在 `tgfx_adapter_test`。
