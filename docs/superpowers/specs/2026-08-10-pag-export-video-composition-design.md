# PAG Export VideoComposition — 设计说明

日期：2026-08-10  
状态：已实现  
范围：`_bmp` 导出可选 `VideoComposition`（Apple VideoToolbox + Alpha 侧边条）+ 导出选项 UI；依赖已落地的 [PAG export `_bmp` / Bitmap](./2026-07-31-pag-export-design.md) 与 [TgfxBitmapFrameSource](./2026-08-10-tgfx-bitmap-frame-source-design.md)

相关：AE `GetCompositionType`（支持 VideoSequence 时 `_bmp` 默认 Video）；`pag::VideoComposition` / `VideoSequence`；现有 `PagBitmapFallback` / `EncodeBitmapSequence`

## 目标

1. `_bmp` 可导出为 **`pag::VideoComposition`**，播放侧走硬解，改善全屏/shader 类位图卡顿
2. 导出选项：`PagBmpSequenceType { Auto, Video, Bitmap }`，默认 **Auto**
3. Video **一律**带 RGB|Alpha 侧边条（对齐 PAG `makeRGBAAA`）
4. Auto：几乎静态 → Bitmap；否则 Video；Video 编码失败 → **报错**（不静默回落）
5. Bridge + App 导出 UI 透传选项；复用现有 `BitmapFrameSource` 离屏

## 非目标

- 非 Apple 平台 Video 编码
- 音频轨、完整 `MP4Header` 产品化、码率/档位细调 UI
- 修改 `_bmp` 触发规则或矢量映射
- 链 AE exporter Qt/`OfflineVideoEncoder`

## 已锁定决策

| 项 | 选择 |
|---|---|
| 选项形态 | `Auto` / `Video` / `Bitmap`（默认 Auto） |
| Alpha | Video 一律侧边条 |
| Auto 回落 | 几乎静态 → Bitmap；编码失败不静默改 Bitmap |
| UI 默认 | Auto |
| 架构 | 方案 1：平行 Video 管线 + 共享 FrameSource + VT Annex-B |
| 尺寸 / 放置 | 与 Bitmap 相同（`ComputeBitmapSize` + 层 PreCompose 放大） |

---

## §1 架构

```
导出 UI: bmpSequenceType = Auto|Video|Bitmap（默认 Auto）
        ↓
ms_pag_export / PagExporter::Export
        ↓
_bmp 触发（合成名 / 层名，规则不变）
        ↓
resolveSequenceKind(type, almostStatic?)
   ├─ Bitmap → PagBitmapFallback + WebP BitmapSequence（现有）
   └─ Video  → PagVideoFallback（新）
                 FrameSource.renderFrame (RGBA)
                 → PackRgbAlphaSideBySide
                 → PagVideoSequenceEncoder (VideoToolbox, Annex-B)
                 → pag::VideoComposition + VideoSequence
                 → PreCompose 放回宿主（含 maxResolution 放大）
```

| 组件 | 路径 | 职责 |
|---|---|---|
| `PagBmpSequenceType` | `PagExportOptions` | 选项枚举 |
| `PagVideoSequenceEncode` | `src/export/pag/` | 拼图 + VT 编码 → `VideoSequence` |
| `PagVideoFallback` 或扩展 `PagBitmapFallback` | `src/export/pag/` | 建 `VideoComposition` + 层/合成入口 |
| `TgfxBitmapFrameSource` | `adapter/tgfx` | 复用离屏 RGBA |
| Bridge / App | `MSPagExportOptions` + 设置页 | 透传；分段控件 |

模块：`pag_export`（Apple）链接 VideoToolbox；非 Apple 选 Video/Auto（非静态）→ `EncodeFailed`。

---

## §2 选项 / Bridge / UI

### 2.1 Core

```cpp
enum class PagBmpSequenceType { Auto = 0, Video = 1, Bitmap = 2 };

struct PagExportOptions {
    // 现有字段...
    PagBmpSequenceType bmpSequenceType = PagBmpSequenceType::Auto;
};
```

### 2.2 Bridge

```c
typedef CF_CLOSED_ENUM(int, MS_PAG_BMP_SEQUENCE_TYPE) {
    MS_PAG_BMP_SEQUENCE_AUTO = 0,
    MS_PAG_BMP_SEQUENCE_VIDEO = 1,
    MS_PAG_BMP_SEQUENCE_BITMAP = 2,
};

typedef struct MSPagExportOptions {
    const char *outputPath;
    bool allowBitmapExport;
    float bitmapScale;
    MS_PAG_BMP_SEQUENCE_TYPE bmpSequenceType;
} MSPagExportOptions;
```

### 2.3 App

- 保留「Allow bitmap export (_bmp)」；关则树中有 `_bmp` → `MappingFailed`
- 新增分段 `Auto | Video | Bitmap`（仅 allow 开启时可交互）；默认 **Auto**
- `PagExportSettings.bmpSequenceType`

### 2.4 解析

| 条件 | 行为 |
|---|---|
| `allowBitmapExport == false` 且有 `_bmp` | `MappingFailed` |
| `Bitmap` | 仅 WebP `BitmapComposition` |
| `Video` | 仅 `VideoComposition`；失败 → 错误 |
| `Auto` + 几乎静态 | Bitmap |
| `Auto` + 非静态 | Video；失败 → 错误 |

---

## §3 Video 编码与 Alpha

### 3.1 帧布局

- 逻辑尺寸 `W×H` = `ComputeBitmapSize` 结果（传入 FrameSource 的 pixel 尺寸）
- 编码画布水平拼接：左 RGB `W×H`，右 Alpha `W×H`（R=G=B=原 A）
- `sequence->width/height = W/H`
- `alphaStartX = W`，`alphaStartY = 0`
- 编码宽高偶数对齐（与 PAG `getVideoWidth/Height` 奇数 +1 规则一致）

### 3.2 编码器

- Apple VideoToolbox H.264 → Annex-B NAL → `VideoFrame::fileBytes`
- SPS/PPS → `sequence->headers`
- 关键帧间隔：`bitmapKeyFrameInterval`（默认 60）
- 质量：`bitmapImageQuality` 简单映射 bitrate/质量档
- 不写临时 mp4；`MP4Header` 首版可空

### 3.3 静态判定（Auto→Bitmap）

可见区间抽样（首、中、尾 + 少量均匀点），相邻帧变化像素占比低于实现常量阈值（如 `< 0.1%`）→ 几乎静态 → Bitmap。不进 UI。

### 3.4 错误

非 Apple / VT 不可用 / 编码失败 → `PagExportErrorKind::EncodeFailed`，不静默改 Bitmap。

### 3.5 放置

层 `_bmp`：PreCompose 在 maxResolution 缩小后按 `host/bitmap` 放大（与 Bitmap 路径相同）。合成级 `_bmp`：主/嵌套 `VideoComposition` 尺寸为序列逻辑尺寸。

---

## §4 测试与验收

| 用例 | 期望 |
|---|---|
| `Bitmap` 回归 | 现有 PagExporter / Bridge 测绿 |
| `Video` | `File::Load` 含 `VideoComposition`；`alphaStartX == width` |
| `Auto` 静态 | Bitmap |
| `Auto`/`Video` 运动 | Video |
| 层 `_bmp` + maxResolution | PreCompose scale 正确 |
| `allowBitmapExport=false` | MappingFailed |
| 编码失败 stub | EncodeFailed |

手工：全屏 shader `_bmp`，`Auto`/`Video` 导出后 PAGViewer **Videos > 0**，播放优于 Bitmap。

---

## §5 与既有文档关系

- [2026-07-31-pag-export-design.md](./2026-07-31-pag-export-design.md) 非目标中的「`VideoComposition` 不做」对本功能**予以撤销**；其余矢量/Bitmap 规则不变
- Bitmap 路径与 FrameSource 像素尺寸契约保持不变
