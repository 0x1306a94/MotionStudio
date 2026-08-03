# PAG Export — 设计说明

日期：2026-07-31  
状态：§0 依赖已落地；PagExporter 待实现  
范围：库层 only（Core `export/` 编排 + 链接 `pag_codec` 的 `Codec::Encode` + GoogleTest；**无导出 UI / 无 Bridge API**）

参考实现：`third_party/libpag/exporter`（AE → `pag::File` → `Codec::Encode`）。  
本 spec 描述 **MotionStudio Document → `.pag`**，不是 PAGX（`third_party/libpag/spec/pagx_spec*.md`）也不是 AE 插件本身。

## 目标

1. 将 MotionStudio `Document` 中指定合成导出为 **二进制 `.pag`**
2. **尽量全矢量**：能映射到 `pag::*` 的字段一律保留关键帧结构与可编辑性
3. **无 PAG 对应物 / 映射失败** 时，将最小范围降级为 `BitmapComposition`（整层或 Group 子树）
4. 编码走 **`pag::Codec::Encode`**（经 MS 侧 `adapter/pag_codec`，源码来自 `third_party/libpag`）
5. 验收以 **`pag::File::Load` + 结构断言** 为主；不要求首版自动化像素对比

## 现状

| 层级 | 能力 |
| --- | --- |
| 模型 | Shape / Text / Image / Group / Precomp；Transform KF；Mask；TrackMatte；FollowPath；Fill/Stroke styles |
| 渲染 | `SceneEvaluator` → `BuildCommands` → `PlayCommands`；tgfx Metal 预编译 + adapter |
| `export/` | 已有视频导出设计（`VideoExporter`）；**尚无 PAG 导出** |
| Lottie | 架构规划「模型直转」；与 PAG 同属保留关键帧路径，实现可并行 |
| tgfx | **唯一源码**：`third_party/libpag/third_party/tgfx`；MS 用 `cmake/BuildTgfx.cmake` 预编译 **Metal** `tgfx.a` |
| libpag / pag_codec | DEPS 拉 `third_party/libpag`；**不** `add_subdirectory(libpag)`；`adapter/pag_codec` 只编 base+codec |
| AE exporter | `PAGExport` → `ExportComposition` / `ExportLayers` → `pag::File` → `Codec::Encode` + `File::Load` 自检 |

## 非目标

- App 导出 UI / Bridge `ms_pag_export_*`
- 音频轨、`VideoComposition`、硬件编码、TimeStretch UI
- PAGX（XML）导入导出
- 完整 AE 特效 / 相机 / Solid 层（MS 模型无则不做）
- 与 PAGViewer 的自动化像素回归（可作手工验收清单）
- 自研 `.pag` 编码器（不复刻 `src/codec/tags`）
- 并行维护独立 `third_party/tgfx` 仓库（已拆除；源码只随 libpag）
- 在导出路径链接整库 libpag 渲染 / OpenGL 平台层

---

## §0 依赖收敛与 Catalyst（已落地）

> 日用拓扑摘要亦见 [architecture.md §第三方依赖](../../architecture.md)。  
> 早期草案曾写「`add_subdirectory(libpag)` + 拆除 `BuildTgfx`」；落地后改为下文，**以本节为准**。

### 0.1 约束与取舍（为何这样）

| 约束 | 影响 |
| --- | --- |
| 预览必须 Metal | App / `tgfx_adapter` 走 tgfx Metal；不能与 GLES 构建混链两份 tgfx |
| 只能一份 tgfx | 禁止独立 `third_party/tgfx`；源码固定为 libpag 内嵌树 |
| Catalyst 无 OpenGLES | 上游 libpag 渲染/platform 偏 GL；整库 `add_subdirectory(libpag)` 在 Catalyst 上不可用 |
| 导出只需编解码 | `Codec::Encode` / `File::Load` 在 `src/base` + `src/codec`；**不需要** libpag 播放/GPU |
| 不改上游 CMake 做 codec-only | 曾试 `PAG_BUILD_CODEC_ONLY` 再撤回；裁剪放在 MS 的 `adapter/pag_codec`，便于升级 libpag |

因此：

1. **源码唯一**：tgfx / libpag 均来自 DEPS 的 `third_party/libpag`（及其 `third_party/tgfx`）。
2. **构建分叉**：tgfx 仍由 MS **预编译 Metal**（`BuildTgfx.cmake` + vendor `build_tgfx`）；编解码由 **`pag_codec` 自建 CMake** 编译，**不**走上游整库目标。
3. **链同一份预编译 tgfx**：`tgfx_adapter` / `pag_codec` / 相关测试均 `motionstudio_link_prebuilt_tgfx`（可执行文件也要挂 search path——静态库上的 Xcode `LIBRARY_SEARCH_PATHS` 不会传到最终 link）。

### 0.2 已落地拓扑

```
DEPS
  repos: libpag @ pin（内嵌 third_party/tgfx；无独立 tgfx 项）
  actions:
    depctl --clean（third_party）
    python3 libpag/third_party/tgfx/third_party/shaderc/utils/git-sync-deps
    patches/libpag-tgfx-vendor_tools-maccatalyst-arm64.patch  → …/tgfx/third_party/vendor_tools
    patches/libpag-tgfx-maccatalyst-arm64.patch               → …/tgfx
  （不对 third_party/libpag 根目录打 Catalyst patch——不编上游整库）

sync_deps.sh
  depctl --skip-paths
    third_party/libpag/third_party/tgfx/third_party/shaderc,
    third_party/libpag/third_party/tgfx/third_party/tint,
    third_party/libpag/third_party/libyuv

third_party/libpag/
  include/pag/…          ← pag_codec PUBLIC include
  src/base + src/codec   ← pag_codec 编译（不含 src/rendering / src/platform 原生 GPU）
  src/platform/Platform.cpp + adapter/pag_codec/src/PlatformStub.cpp
  third_party/tgfx/      ← 唯一 tgfx 源码（Metal 预编译输入；需 Node 跑 build_tgfx）

根 CMakeLists（仅 APPLE 块）
  LIBPAG_DIR / TGFX_SOURCE_DIR / TGFX_INCLUDE_DIR / TGFX_OUT_DIR / TGFX_CMAKE_ARGS
        │
        ├─ adapter/tgfx      → Prebuild Metal tgfx.a → tgfx_adapter（+ 测试）
        ├─ adapter/avf
        ├─ adapter/pag_codec → 静态库 pag_codec + pag_codec_test
        ├─ bridge            → 链 tgfx_adapter；include 用 TGFX_INCLUDE_DIR
        └─（待加）pag_export → PUBLIC core，PRIVATE pag_codec；测试同挂 tgfx prebuild/link
```

预编译产物（按 Config / 平台）：

```
${CMAKE_BINARY_DIR}/tgfx_prebuilt/<Config>/<mac|ios|catalyst>/<arch>/tgfx.a
# BuildTgfx 另建 libtgfx.a → tgfx.a 符号链接，供 Xcode -ltgfx
# Xcode gen：apps/gen_xcode/tgfx_prebuilt/…
# App：Base.xcconfig 的 LIBRARY_SEARCH_PATHS + -ltgfx（与预编译目录对齐）
```

链接方式差异（实现时勿混用假设）：

| 生成器 | 如何链 tgfx |
| --- | --- |
| Ninja | `target_link_libraries(... "${TGFX_PREBUILT_MAC_LIB}")` 绝对路径 `.a` |
| Xcode | `-ltgfx` + 按 sdk 的 `LIBRARY_SEARCH_PATHS`（macosx* → catalyst 目录） |
| 可执行测试 | 必须对**最终二进制**再调 `motionstudio_add_tgfx_prebuild` / `motionstudio_link_prebuilt_tgfx`；静态库上的 search path **不**传到 exe（`pag_codec_test` / 将来 `pag_export_tests`） |

平台范围：

- `pag_codec` / 预编译 tgfx / 将来 `pag_export`：**仅 Apple**（根 CMake `if (APPLE)`）。
- 非 Apple 构建不拉 libpag/tgfx；Core（无 pag）仍可单测。

关键文件：

| 区域 | 路径 |
| --- | --- |
| DEPS / sync | `DEPS`；`sync_deps.sh`（skip-paths） |
| Catalyst patches | `patches/libpag-tgfx-maccatalyst-arm64.patch`、`patches/libpag-tgfx-vendor_tools-maccatalyst-arm64.patch` |
| 预编译入口 | `cmake/BuildTgfx.cmake`；`adapter/tgfx/CMakeLists.txt`（`motionstudio_*_tgfx_*`） |
| 编解码裁剪 | `adapter/pag_codec/`（CMake + `PlatformStub.cpp` + `tests/PagCodecTest.cpp`） |
| 根变量 | 根 `CMakeLists.txt`：`LIBPAG_DIR` / `TGFX_*` |
| App 链接 | `apps/MotionStudioApp/Configurations/Base.xcconfig` |
| CI 缓存 | `build/tgfx_prebuilt/`（命中则跳过 tgfx 重编；见 AGENTS.md） |

`pag_codec` 运行时链接（摘要，详见其 CMake）：预编译 tgfx、Foundation/Metal/MetalKit/QuartzCore/CoreGraphics/CoreText/CoreVideo/CoreMedia/ImageIO、iconv；Xcode 另链 UIKit，Ninja 另链 ApplicationServices/Cocoa。

### 0.3 明确不做 / 已否决

| 方案 | 否决原因 |
| --- | --- |
| 独立 `third_party/tgfx` + 另一份 pin | 双份 ABI/头文件；升级分叉 |
| `add_subdirectory(libpag)` 整库 | 拉渲染 + GL platform；Catalyst 无 GLES |
| 改上游 CMake `PAG_BUILD_CODEC_ONLY` | 维护成本高；升级易冲突；裁剪应在 MS 侧 |
| 对 libpag 根打 Catalyst / GLES stub 以编整库 | 导出不需要；维护面大；已用 `pag_codec` 绕开 |
| 导出路径再编一份 GLES tgfx | 与预览 Metal tgfx 冲突；导出不需要 GPU |
| pag_codec 用 NativePlatform | 拉视频解码 / GPU 平台；stub `Platform::Current()` + 空 `VideoDecoderFactory` 即可 |
| 跑 libpag 自带 `./sync_deps.sh` 作为 MS 主同步 | MS 用根 `DEPS` + shaderc `git-sync-deps` + skip-paths；勿混用两套 |

### 0.4 验收（§0）

- [x] DEPS 无独立 `third_party/tgfx`；源码仅 `third_party/libpag/third_party/tgfx`
- [x] Catalyst patch 作用于 libpag 内 tgfx / vendor_tools
- [x] Ninja / Xcode：`tgfx_adapter`（及 App）链预编译 Metal `tgfx.a`
- [x] `pag_codec` + `pag_codec_test` 可编过；测试可执行文件自带 tgfx search path / PRE_BUILD
- [ ] `PagExporter`（§1 起）——**依赖 §0，可开始**

### 0.5 与 PagExporter 的关系

§0 已满足。后续：

- `pag_export`（Apple）：PUBLIC `core`，**PRIVATE `pag_codec`**（不要链上游目标名 `pag`）
- `pag_export_tests`：同 `pag_codec_test`，自挂 tgfx PRE_BUILD + link
- `pag::*` / `#include "pag/file.h"` 仅限 `src/export/pag/` 实现文件；公共头只暴露 MS 类型
- Encode 入口以 `pag::Codec::Encode` / `VerifyAndMake` / `Decode`（或 `File::Load`）为准，与 `pag_codec_test` 对齐

实现分期见 [plan](../plans/2026-07-31-pag-export.md) Phase 1a（矢量）/ 1b（Bitmap 等）；本 §0 不阻塞 1a。

---

## §1 架构与职责

采用 **方案 A：直接模型映射**（对齐 AE exporter / Lottie「模型直转」；不走逐帧求值重建矢量）。

```
Document + PagExportOptions
        │
        ▼
┌──────────────────┐
│ PagExporter      │  Core export/：校验、编排、警告、写文件
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ PagFileBuilder   │  Document → pag::File 对象图（矢量优先）
└────────┬─────────┘
         │ 不可映射层 / Group 子树
         ▼
┌──────────────────┐     Evaluate → BuildCommands → PlayCommands → RGBA
│ BitmapFallback   │◄──── 接口在 Core；实现可注入（单测用假源）
└────────┬─────────┘
         │
         ▼
  pag::Codec::Encode(file) → bytes / 可选写 outputPath
```

| 组件 | 层 | 职责 |
| --- | --- | --- |
| `PagExporter` | Core `export/` | 公共入口；options 校验；调用 Builder + Encode；收集 warnings |
| `PagFileBuilder` | Core `src/export/`（不进公共头） | MS → `pag::*` 映射；触发 Fallback |
| `BitmapFrameSource` | 接口 Core；生产实现可在 adapter | 为降级层提供逐帧 RGBA |
| libpag 源码 | `third_party/libpag` | 唯一 tgfx + PAG 编解码源；**不**走上游整库 CMake |
| `pag_codec` | `adapter/pag_codec` | base+codec + Platform stub；链预编译 Metal tgfx |
| `PagExporter` 实现 | `pag_export` 静态库 | 映射 + `Codec::Encode`；PRIVATE 链 `pag_codec` |

模块依赖：

```
export → model + animation
export → render               （仅 BitmapFallback 路径）
pag_export → core + pag_codec （pag::* 不出现在 include/MotionStudio 公共 API）
tgfx_adapter → 预编译 tgfx.a  （源码 = libpag/.../tgfx；include 同左）
pag_codec    → 预编译 tgfx.a  （同一份）
```

**构建约定**

- tgfx **源码**唯一：`third_party/libpag/third_party/tgfx`；**构建**走 MS `BuildTgfx.cmake`（Metal），禁止再拉独立 `third_party/tgfx`
- PAG 编解码走 `adapter/pag_codec`，**禁止** `add_subdirectory(third_party/libpag)` 整库
- `pag_export` / adapter **不**链接 `exporter/`（AE SDK / Qt）
- 公共头只暴露 MS 类型与 `std::vector<uint8_t>`；`#include "pag/file.h"` 仅限 `src/export/pag/` 实现文件
- tgfx / libpag 版本以 **libpag 仓库 pin** 为准；Catalyst 靠 `patches/libpag-tgfx-*.patch`

---

## §2 关键接口

### 2.1 Options / Result / Warning

```cpp
namespace motion {

struct PagExportOptions {
    std::string outputPath;           // 非空则额外写入文件；空则只返回 bytes
    EntityId compositionId;           // 无效 → Document 主/第一个 Composition
    bool allowBitmapFallback = true;  // false：遇不可映射则失败，不写残缺文件
    float bitmapScale = 1.0f;         // >0；作用于降级位图像素尺寸
};

struct PagExportWarning {
    EntityId entityId;                // 相关层/合成；无效表示全局
    std::string code;                 // 稳定机器码，见 §4
    std::string message;              // 人类可读
};

struct PagExportResult {
    std::vector<uint8_t> bytes;
    std::vector<PagExportWarning> warnings;
};

enum class PagExportError {
    InvalidComposition,
    InvalidOptions,
    MappingFailed,      // allowBitmapFallback == false 时的不可映射
    EncodeFailed,
    WriteFailed,
    Cancelled,          // 预留；首版可无进度回调
};

}  // namespace motion
```

### 2.2 BitmapFrameSource

```cpp
class BitmapFrameSource {
public:
    virtual ~BitmapFrameSource() = default;

    // 为「降级单元」准备渲染上下文（层或其子树在宿主合成时间轴上的可见区间）
    virtual Expected<void, std::string> prepare(
        const Document& document, EntityId hostCompositionId,
        EntityId rootLayerId, TimeRange visibleRange, float bitmapScale) = 0;

    // time：宿主合成时间轴帧号；返回未预乘或预乘均可，须在文档中固定一种（推荐预乘 RGBA8）
    virtual Expected<BitmapFrame, std::string> renderFrame(FrameTime time) = 0;

    virtual void finish() = 0;
};

struct BitmapFrame {
    int width = 0;
    int height = 0;
    const uint8_t* rgba = nullptr;  // 行主序，top-left
    size_t rowBytes = 0;
    bool premultiplied = true;
};
```

首版单测可注入纯色/固定图案源；生产实现（后置）复用离屏 tgfx 路径，**不**纳入本里程碑交付。

### 2.3 PagExporter

```cpp
class PagExporter {
public:
    // 调用期间 Document 必须不可变。
    // frameSource：allowBitmapFallback 且确实触发降级时必需；未触发可为 nullptr。
    static Expected<PagExportResult, PagExportError> Export(
        const Document& document, const PagExportOptions& options,
        BitmapFrameSource* frameSource = nullptr);
};
```

伪代码：

```
resolved = resolveComposition(document, options)
validate(options)  // bitmapScale > 0 等
builder = PagFileBuilder(document, resolved, options, frameSource)
file = builder.build()            // 内部收集 warnings；必要时 Fallback
if builder.failed: return MappingFailed / …
bytes = Codec::Encode(file)
if bytes == null: return EncodeFailed
optional write options.outputPath
optional verify: File::Load(bytes) != null  // debug / 测试默认开；release 可关
return {bytes, warnings}
```

---

## §3 映射表（尽量全矢量）

时间：MS `FrameTime`（帧）↔ PAG `Frame`；`FrameRate` → `composition->frameRate`（Hz，`num/den` 转 double）。  
ID：导出时分配稳定 `pag::ID`（可用递增计数器）；**不要求**等于 MS `EntityId` 数值，但 Builder 内维护双向表以便 parent / trackMatte / image 引用。

### 3.1 合成与层类型

| MotionStudio | PAG | 说明 |
| --- | --- | --- |
| `Composition` | `VectorComposition` | width/height/duration/frameRate/background |
| `LayerType::Shape` | `ShapeLayer` | contents 见 §3.3 |
| `LayerType::Text` | `TextLayer` | `sourceText`；尽量进入 `editableTexts` |
| `LayerType::Image` | `ImageLayer` + `ImageBytes` | asset 读入；尽量 `editableImages` |
| `LayerType::Group` | `NullLayer` | 见 §3.5 |
| `LayerType::Precomp` | `PreComposeLayer` | 引用已导出的子 `Composition`；时间映射见下 |
| （降级产物） | `BitmapComposition` + `PreComposeLayer` | 见 §3.6 |

Precomp 时间（与现有求值一致）：

```
innerTime = (outer - inPoint) × timeStretch + startTime
```

映射到 PAG：`layer->startTime` / `duration` / `stretch`（或等价字段）与 AE exporter 对齐；实现时以 `pag::PreComposeLayer` / `Layer` 字段为准做一次对照表。

### 3.2 层公共属性

| MS | PAG |
| --- | --- |
| `name` | `layer->name` |
| `inPoint` / `outPoint` | `startTime` + `duration`（注意 exclusive/inclusive 边界，与 AE exporter 一致化） |
| `visible` | `isActive`（**例外**：被用作 Track Matte 源的层必须 `isActive = false`，见 §5.1） |
| `transform`（anchor/position/scale/rotation/opacity + KF） | `Transform2D` + `Property` / `AnimatableProperty` |
| `blendMode` | `BlendMode` 同名子集；无对应则 warning + 近似为 Normal **或** 触发该层 Fallback（优先：无对应则 Fallback，保视觉） |
| `masks[]` | `MaskData`：path / mode / opacity / inverted / feather / expansion |
| `trackMatteType` + `trackMatteLayerId` | `trackMatteType` + `trackMatteLayer`（源层须紧邻目标之上且 `isActive=false`，见 §5.1） |
| `styles`（Fill/Stroke） | Shape → shape contents；**Text → `TextDocument` 的 fill/stroke 字段**（非 ShapeElement） |
| `followPath.enabled == true` | 优先 BitmapFallback；不可用则**跳过该层** + warning（整份导出不因单层 `MappingFailed` 中断，见 §5.1） |
| `locked` | 忽略（PAG 无编辑锁） |

**缓动**：MS 贝塞尔缓动 → PAG keyframe bezier；空间贝塞尔（position path）若 PAG 支持则映射，否则采样为多关键帧并 warning（code: `SpatialEasingApproximated`）。

### 3.3 Shape

MS `ShapeContent` 当前为 **单 geometry** + 层级 `styles`（Fill/Stroke，含 trim）。

| MS | PAG ShapeElement |
| --- | --- |
| `ShapePath` | `ShapePath`（或等价路径元素） |
| `ShapeRect` | `Rectangle` |
| `ShapeEllipse` | `Ellipse` |
| `ShapeTrimPath` / Stroke trim* | `TrimPath` |
| `FillStyle` | `Fill` |
| `StrokeStyle` | Shape `Stroke`（width/cap/join/miter）。`Inside`/`Outside` → **平行 ShapeLayer**（outline `ShapePath` + `Fill`；trim 先作用于 stroke 几何再做 Inside/Outside 布尔，对齐 MS）；trim 动画按帧烘焙 Path。`Center`+Trim → 平行层（Path+TrimPaths+Stroke），**禁止**把 TrimPaths 放进主层（会裁掉 Fill）。主层仅 Fill + 无 Trim 的 Center Stroke |

\* 若 trim 仅在 `StrokeStyle` 上：导出为 shape 内容链中的 TrimPath，行为对齐 MS 渲染。

### 3.4 Text / Image

**Text**：`text` / `fontFamily` / `fontStyle` / `fontSize` / `size` / `align` → `TextDocument`；**必须**从 `Layer.styles` 取首个 Fill → `applyFill`/`fillColor`、首个 Stroke → `applyStroke`/`strokeColor`/`strokeWidth`（`strokeOverFill = true`，对齐 MS 先填后描）。漏映射时 PAG 默认 `fillColor=Black`，预览会像「只剩描边」。多 Fill/Stroke → 取第一个 + warning。

`boxTextMode` 直映 PAG / AE 点文本 vs 段落（框）文本：

- `boxTextMode == false`（默认点文本）→ `boxText = false`，`boxTextSize = 0`，`firstBaseLine = 0`；并把 `transform.position.y` **加上** 真实 `ascent`（`PagExportOptions::textAscentResolver`；未提供时回退 `fontSize * 0.8`。MS 原点在文字顶边，PAG/AE 原点在首行 baseline，见 §5.1）。
- `boxTextMode == true`（框文本：换行 + shrink）→ `boxText = true`，`boxTextPos = (0,0)`，`boxTextSize = size`；`firstBaseLine ≈ boxTextPos.y + ascent`（同上；**禁止**对框文本留 0，否则 PAG 会竖直居中）。框文本不改 position。
- 不再为 shrink 发 `TextFeatureApproximated`（MS shrink 对齐 PAG 框文本适配）。

**Image**：读 `Asset.path`（相对 `projectRoot`）→ 编码为 `ImageBytes`（**源像素尺寸**，对齐 AE）；`ImageContent.size` + `scaleMode` 按 `ComputeImageDestinationRect` **烘焙进** `transform.scale` / `anchor`（AE 无独立容器，改大小走 Transform）。`ImageFillRule::scaleMode` 保留给运行时换图。Zoom/None 溢出容器时加矩形 Mask。`size` 关键帧首版按 inPoint 静帧烘焙并 warning。

**合成背景 / 圆角**：始终在图层栈最底插入 `CompositionBackground` Shape（铺 `backgroundColor`）——多数 PAG 预览只画图层、不铺 `Composition.backgroundColor` 字段。`cornerRadius > 0` 时 backdrop 用圆角矩形，并外包一层带圆角 Mask 的 PreCompose 做整体裁剪。

默认可替换：导出后若未标记不可替换，依赖 `Codec::Encode` 默认把全部 text/image 标为 editable（与 libpag codec 行为一致）；若某层 Fallback 掉则自然不在 editable 列表。

### 3.5 Group → NullLayer

- MS 用 `parentId` 树；PAG 为扁平 `layers[]` + `parent` 指针
- `LayerType::Group` → `pag::NullLayer`，保留 Transform / timing / masks / trackMatte
- 子层 `parent` 指向该 NullLayer
- 图层顺序见 §5.1（MS 底→顶构建，再反转为 PAG File 顶→底；背景层在数组末尾）
- Group **自身**带 FollowPath 或不可映射 blend 等 → **整棵子树** 一并 Fallback（§3.6）

### 3.6 BitmapFallback（整层 / 最小子树）

**触发条件（非穷尽，实现用统一 `needsFallback(layer)`）**

- `followPath.enabled`
- BlendMode 无 PAG 对应且选择「保视觉」策略
- Shape/Text/Image 映射中不可恢复的错误（缺 asset、空 geometry 等可先失败或跳过；严重视觉偏离走 Fallback）
- Group 自身触发时：子树整体降级

**单元形态**

1. 离屏渲染该层（或子树）在宿主时间轴 `[in, out)` 的每一帧（乘 `bitmapScale`）
2. 填入 `pag::BitmapComposition`（`BitmapSequence` / 帧列表，对齐 libpag 现有结构）
3. 用 `PreComposeLayer` 替换原层（或 Group 根）：
   - **推荐**：烘焙结果已是「世界外观」→ PreComposeLayer 使用 **单位 transform**，仅保留原层 timing（in/out/startTime）；避免双重变换
   - 子树降级时：只保留一个 PreComposeLayer，原子层不再单独导出
4. 追加 `PagExportWarning`（如 `UnsupportedFollowPath`）

**`allowBitmapFallback == false`**：不调用 FrameSource；该层按 soft-fail 跳过（§5.1），不整份中断。

**首版不交付**生产 `BitmapFrameSource` 实现；测试用 Fake 源验证「警告 + Bitmap 子合成挂接 + Encode/Load」。

---

## §4 Warning codes（稳定字符串）

| code | 含义 |
| --- | --- |
| `UnsupportedFollowPath` | FollowPath 层已跳过或 Bitmap 降级 |
| `UnsupportedBlendMode` | 混合模式无映射，已降级或近似 |
| `StrokePositionBaked` | Inside/Outside 已导出为平行 outline ShapeLayer |
| `StrokePositionBakeFailed` | 轮廓烘焙失败，该描边省略 |
| `StrokeTrimSeparated` | 带 Trim 的 Center Stroke 已拆到平行层，避免裁切 Fill |
| `SpatialEasingApproximated` | 空间缓动被采样近似 |
| `TextFeatureApproximated` | 文本特性部分丢失但仍矢量 |
| `TextStyleApproximated` | 多 Fill/Stroke 折叠为 TextDocument 单组 |
| `ImageAssetMissing` | 图片资源缺失，层已跳过 |
| `ImageSizeAnimationBakedAsStatic` | 容器尺寸关键帧按 inPoint 静帧烘焙 |
| `BitmapFallback` | 通用降级（可带更具体 code 同时存在） |
| `BitmapFallbackUnavailable` | 允许降级但无 FrameSource，层已跳过 |
| `GroupSubtreeRasterized` | Group 子树整体光栅化 |
| `CompositionCornerRadiusApproximated` | 圆角用 backdrop + clip Precomp 近似 |
| `LayerSkipped` / `SkippedParent` / `SkippedTrackMatte` | 软失败跳层或断链 |

---

## §5 与 AE exporter 的对照（实现指引）

| AE exporter | MotionStudio |
| --- | --- |
| `PAGExport::exportAsFile` | `PagExporter::Export` |
| `ExportVectorComposition` + `ExportLayers` | `PagFileBuilder` |
| `ExportBitmapComposition` / sequence | `BitmapFallback` + `BitmapFrameSource` |
| `ExportVerify` / AlertInfo | `PagExportWarning` |
| `Codec::Encode` + `ValidatePAGFile` | 同；测试强制 `File::Load` |
| Marker / 音频 / Video / BMP 后缀合成名 | **不做** |
| Qt UI / 批量面板 | **不做** |

可读参考（勿直接依赖 exporter 目标）：

- `exporter/src/export/PAGExport.cpp`
- `exporter/src/export/ExportComposition.cpp`
- `exporter/src/export/ExportLayer.cpp`
- `exporter/src/export/data/Transform2D.*`、`Shape.*`、`Mask.*`
- `src/codec/Codec.cpp`（`Encode` / `InstallReferences`）
- `src/rendering/renderers/CompositionRenderer.cpp`（图层绘制序）
- `src/rendering/renderers/TrackMatteRenderer.cpp`

### 5.1 实现坑位（踩坑备忘）

以下与「看起来导出了但预览不对」强相关，改 `PagFileBuilder` 时对照：

1. **图层顺序（PAG File ≠ PAGLayers API）**  
   - MS：`layers[0]` = 最底，末项最上。  
   - PAG **File** 内 `VectorComposition::layers`：`CompositionRenderer` **从尾画到头**，故 **index 0 = 最顶**（注释：*The index order of Layers in File is different from PAGLayers*）。  
   - 导出：按 MS 底→顶构建（便于 Group fallback 先收集子孙），再 `reverse` 成 PAG 顶→底；`CompositionBackground` **push 在末尾**（真正最底）。  
   - 错误地把全幅背景插在 index 0 → 盖住全部内容。

2. **Track Matte**  
   - Decode 时 `InstallReferences` 把有 matte 的层的源**强制绑成** `layers[index - 1]`（只认邻接，不认任意指针）。导出后须把 matte 源排在目标层正上方。  
   - AE exporter：`trackMatteLayer->isActive = false`。PAG 渲染跳过 `!isActive` 层，仅经 `trackMatteLayer` 采样。若源层仍 `isActive=true`，会当普通层画出（例如白色 silhouette 盖住蓝色 Luma 结果）。对齐 MS `usedAsMatteOnly`。

3. **合成背景色**  
   - `Composition.backgroundColor` 会进文件，但很多预览器**不铺该字段**、只画图层。  
   - 必须始终垫底层 `CompositionBackground` Shape；圆角时再加 Precomp + 圆角 Mask 整体裁剪。

4. **Image 容器尺寸**  
   - AE：`ImageBytes` = footage 源尺寸，视觉大小靠 Transform.Scale。  
   - MS：另有 `ImageContent.size` + `scaleMode`。只导出源尺寸会偏小/偏大 → 用 `ComputeImageDestinationRect` 烘焙进 scale/anchor。

5. **Text Fill/Stroke**  
   - 样式在 `Layer.styles`，不在 `TextContent`。必须写入 `TextDocument`；否则默认黑填充、`applyStroke=false`。

6. **点文本竖直位置（baseline）**  
   - MS：层原点在文字**顶边**，首行 baseline ≈ 字体 `ascent`（PingFang SC ≈ `1.06 * fontSize`，不是 `0.8`）。  
   - PAG/AE：点文本层原点在**首行 baseline**（`firstBaseLine=0` 时字形画在 local y=0）。  
   - 只拷贝 position → 文字整体偏上约一个 ascent。导出时 `position.y += ascent`；bridge 用 tgfx `FontMetrics.ascent`（与 MS TextLayout 同源），无 resolver 时回退 `fontSize * 0.8`。

7. **Color alpha → Fill/Stroke opacity**  
   - PAG `Color` 只有 RGB；MS `#RRGGBBAA` 的 A 必须写到 `FillElement`/`StrokeElement` 的 `opacity`（`ConvertColorAlpha`）。漏映射会变成不透明实色。

8. **单层 MappingFailed = soft-fail**  
   - 跳过该层 + warning，继续导出；父层/matte 源被跳过则清链接 + warning。  
   - 硬失败仍限：InvalidComposition / InvalidOptions / EncodeFailed / WriteFailed。

---

## §6 测试计划

| 用例 | 期望 |
| --- | --- |
| Shape Rect/Ellipse/Path + Transform KF | Load 成功；ShapeLayer；关键帧数量符合 |
| Fill + Stroke + Trim | shape contents 含对应元素 |
| Fill + Inside/Outside Stroke | 主 ShapeLayer（Path+Fill）+ 两个平行 outline ShapeLayer；有 `StrokePositionBaked` warning |
| Text + Fill/Stroke styles | TextDocument `applyFill`/`fillColor`/`applyStroke`/`strokeWidth` 正确 |
| Text + `boxTextMode=false`（默认） | `boxText=false`；`firstBaseLine=0`；`position.y` 含 `+ ascent`（无 resolver 时 `fontSize*0.8`） |
| Text + resolver | `position.y` 使用 resolver 返回的 ascent |
| Text + `boxTextMode=true` | `boxText=true`；`boxTextSize=size`；`firstBaseLine ≈ ascent`；无 shrink warning |
| Image 容器 ≠ 源尺寸 + LetterBox | `ImageBytes` 仍为源尺寸；transform.scale 含容器 fit |
| Mask Add/Sub/Intersect | masks 非空且 mode 正确 |
| TrackMatte | 类型/邻接正确；**matte 源 `isActive == false`** |
| Precomp 嵌套 | PreComposeLayer 指向子 VectorComposition；时长关系正确 |
| Group 父子 | NullLayer + child.parent；PAG 顶→底序 |
| 空合成 / 任意合成 | 含 `CompositionBackground`；`backgroundColor` 字段与色块一致 |
| 圆角合成 | 外包 Precomp + mask；inner 末层为圆角 backdrop |
| FollowPath + Fake BitmapFrameSource | warning；存在 BitmapComposition；Load 成功 |
| FollowPath 且无 fallback | **整份仍成功**；问题层跳过 + warning |
| Encode round-trip | `Encode` → `Load` → 再 `Encode` 长度/根合成类型稳定（允许非 byte-identical） |

全部在 ASan 构建下跑；不引入 AE SDK。

---

## §7 文件布局（建议）

```
include/MotionStudio/export/PagExporter.h          # Options / Result / Exporter；（BitmapFrameSource 可后置）
include/MotionStudio/export/PagExportOptions.h     # 可合并进上一头，按现有 export 风格二选一
src/export/pag/PagExporter.cpp                     # 实现目录用 pag/ 子目录，避免与 VideoExporter 混在一层
src/export/pag/PagFileBuilder.h/.cpp               # 内部
src/export/pag/PagAnimatableConvert.h/.cpp         # Animatable/Easing → pag::Property
src/export/pag/PagBitmapFallback.cpp               # Phase 1b
src/export/pag/CMakeLists.txt                      # 或挂到根 CMake；仅 APPLE
tests/export/PagExporterTest.cpp
```

CMake：独立 `pag_export` 私有链接 `pag_codec`；测试链 gtest；Xcode 测试目标必须自挂 tgfx prebuild/link（见 §0.2）。

---

## §8 实现阶段（建议拆分，非本 spec 强制）

0. **§0 依赖收敛 + Catalyst**——**已完成**（见 §0；非「拆 BuildTgfx / add_subdirectory(libpag)」）
1. CMake 上 `pag_export` + 最小 `Codec::Encode` 冒烟（链 `pag_codec`）
2. Composition + Null/Shape + Transform KF
3. Text / Image / Mask / TrackMatte / Precomp
4. BitmapFallback 挂接 + FollowPath 触发
5. Warning 表与单测补全

App UI / Bridge 另开 spec（类似 `mp4-video-export-ui`）。

---

## 开放问题（实现期可定，不阻塞本 spec）

1. ~~`StrokePosition::Inside/Outside`~~——**已定**：优先保证预览可见 → 平行 outline ShapeLayer（不用 LayerStyle tag 92）；带 Trim 时按 Center
2. 缺图 asset：失败 vs 彩色占位——默认 **MappingFailed（该层）**；若 `allowBitmapFallback` 可改为占位 Bitmap
3. ~~libpag CMake 整库 vs 裁剪~~——**已定**：MS `pag_codec`（base+codec），见 §0
