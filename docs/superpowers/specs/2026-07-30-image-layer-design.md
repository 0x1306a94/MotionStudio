# Image Layer — 设计说明

日期：2026-07-30  
状态：已确认，待实现  
分支：`feature/0x1306a94_image_layer`

## 目标

端到端最小可用的图片图层：

1. 项目包内管理图片资源（`assets/` + 相对路径）
2. 空占位 Image Layer → Inspector 绑定 Asset
3. 容器尺寸与 transform 分离；拖角可按模式改容器或缩放
4. 填充模式对齐 `PAGScaleMode`（None / Stretch / LetterBox / Zoom）
5. 画布绘制、选中、undo、存盘重开

## 现状

| 层级 | 能力 |
| --- | --- |
| `LayerType::Image` + `ImageContent.assetId` | 模型骨架已有 |
| `Asset{name, path}` | 仅路径字符串，无宽高 |
| 序列化 Image / Asset | 已有（缺 size / scaleMode / 宽高） |
| `DrawCommand` / Evaluator / Adapter | **无** DrawImage |
| 工具栏 Add Image | 占位 alert |
| 项目包 `assets/` | `packageFileWrapper` 写出空目录 |
| `ms_document_load` | 从 JSON **内存**加载，无 project root |

## 非目标

- 运行时替换图的 PAG 导出（语义预留，本里程碑不做导出）
- undo 清理磁盘上的 asset 文件
- 字体 Asset、scaleMode 关键帧
- Project 面板以外的导入入口（Inspector 不提供导入）
- 拖角用修饰键临时切模式

---

## §1 数据模型

```cpp
enum class ImageScaleMode : uint8_t {
    None = 0,       // PAGScaleMode::None
    Stretch = 1,
    LetterBox = 2,  // default（与 PAG 一致）
    Zoom = 3,
};

struct Asset {
    EntityId id;
    AssetType type = AssetType::Image;
    std::string name;
    std::string path;   // 相对项目根，如 "assets/photo.png"
    int width = 0;      // 源图像素宽（导入时写入）
    int height = 0;
};

class ImageContent : public LayerContent {
    EntityId assetId;                          // 无效 = 未绑定
    Animatable<Vec2> size{Vec2{200, 200}};     // 容器尺寸（可关键帧）
    ImageScaleMode scaleMode = ImageScaleMode::LetterBox;
};
```

**约定**

- 新建空层：`assetId` 无效，`size` 静态 `200×200`，`anchorPoint = (100, 100)`，`position` = 合成中心，`scaleMode = LetterBox`。
- 绑定 asset 后：**不自动改 size**；Inspector 提供「重置为源尺寸」。
- `Document` 增加**非持久化** `projectRoot`（绝对路径）。`Asset.path` 只存相对路径。
- PropertyPath：新增 `image.size`（走现有 SetStaticValue / 关键帧命令）。
- 序列化：Asset 增 `width`/`height`；ImageContent 增 `size`、`scaleMode`。**不升 schemaVersion**（开发阶段直接改 JSON）。

---

## §2 渲染管线

采用 **方案 1**：SceneState 带齐绘制信息，`DrawImage` 命令自包含。

### 求值

```cpp
struct EvaluatedImageItem {
    EntityId assetId;
    std::string absolutePath;   // projectRoot + Asset.path；无法解析则为空
    Vec2 containerSize;         // evaluate(size)
    Vec2 intrinsicSize;         // Asset width/height
    ImageScaleMode scaleMode;
};

struct EvaluatedLayer {
    // ...existing...
    std::vector<EvaluatedShapeItem> shapeItems;  // Shape 层
    std::optional<EvaluatedImageItem> imageItem; // Image 层（与 shapeItems 互斥）
};
```

- 未绑定 asset / path 空 / 文件缺失：不发 `DrawImage`；层仍参与 hit/bounds（容器矩形）。
- Image 层不伪造 shapeItems。

### DrawCommand

```cpp
enum class DrawCommandType { ..., DrawImage };

// DrawImage 字段：
std::string imagePath;       // 绝对路径
Vec2 imageContainerSize;     // layer 局部容器 [0,0]–[cw,ch]
Vec2 imageIntrinsicSize;
ImageScaleMode imageScaleMode;
```

`BuildCommands`：有有效 `imageItem`（path 非空）则发 `DrawImage`；仍走 Save / Transform / Opacity / Blend / masks 外壳。

### Scale 数学（layer 局部）

容器矩形 `[0,0]–[cw,ch]`。先 clip 到容器，再按 mode 算 dstRect：

| Mode | 行为 |
| --- | --- |
| None | 左上对齐，不缩放；超出裁剪 |
| Stretch | 铺满容器 |
| LetterBox | 等比完整放下，居中，可能留边 |
| Zoom | 等比盖满，居中，可能裁切 |

### Hit / Bounds / 选中框

- Bounds / Hit：容器矩形（不是缩放后的图内容）。
- 选中框跟容器走。

### Adapter

- `RenderAdapter::drawImage(path, container, intrinsic, mode)`
- tgfx：按 path LRU 缓存解码图；失败 no-op。

### 测试

- ScaleMode dstRect 纯函数单测
- CommandBuilder 产出 `DrawImage`
- tgfx 快照：LetterBox / Stretch

---

## §3 Bridge / App / 交互

### 加载与 project root

```c
// 原内存加载改名（测试 / UIDocument 已读到的 bytes）
MSDocument *ms_document_load_json(const char *jsonText, size_t length, char **errorOut);

// 主入口：文档包目录绝对路径
MSDocument *ms_document_load(const char *packagePath, char **errorOut);
// 读 {packagePath}/document.json，projectRoot = packagePath

void ms_document_set_project_root(MSDocument *doc, const char *absolutePath);
char *ms_document_project_root(MSDocument *doc);  // ms_string_free
```

Swift：优先 `ms_document_load(packagePath)`；仅有 Data 时 `load_json` + `set_project_root(fileURL)`。Save As / 草稿迁址调用 `set_project_root`。

### 导入与建层分离

| 动作 | 入口 | 行为 |
| --- | --- | --- |
| 导入图片资源 | **Project 面板** | iPad：可选相册（PHPicker）或文件；Catalyst：文件选择。拷到 `assets/`，写入 `Document.assets` |
| 新建图片图层 | 工具栏 Add Image | 空占位层（见 §1），不带 asset |
| 绑定 / 解绑 | **Inspector** | 下拉已导入 image asset；`assetId=0` 解绑 |
| scaleMode | Inspector | 四选一 |
| 重置容器 | Inspector | 将 `size` 设为源图像素尺寸（需已绑定） |

```c
uint64_t ms_command_import_image_asset(MSDocument *doc,
                                       const char *sourceAbsolutePath,
                                       const char *preferredFileName);
uint64_t ms_command_add_image_layer(MSDocument *doc, uint64_t compositionId);
bool ms_layer_set_image_asset(MSDocument *doc, uint64_t layerId, uint64_t assetId);
void ms_layer_set_image_scale_mode(MSDocument *doc, uint64_t layerId, int mode);
int ms_layer_image_scale_mode(MSDocument *doc, uint64_t layerId);
```

- `image.size` 走 PropertyPath。
- import 重名加后缀；**undo 只还原模型，不删除磁盘文件**。
- `packageFileWrapper` 必须把 `assets/` 下真实文件写入包内（不再写空目录）。

### 拖角模式切换（明确 UI，无快捷键）

单选 Image 层时，画布工具条或选中态提供明显切换：

| 模式 | 拖角 / 拖边写什么 |
| --- | --- |
| **容器**（默认） | `image.size`（必要时微调 position 保持对侧锚点）；**不写** `transform.scale` |
| **缩放** | 现有 `transform.scale` 行为 |

- 多选或非 Image 层：无此切换，始终 transform 行为。
- move / rotate / anchor：与模式无关，行为不变。
- **不提供** Option/Alt 等修饰键临时切模式。

---

## §4 错误处理与测试

| 场景 | 行为 |
| --- | --- |
| 无 projectRoot / 拼不出绝对路径 | 不绘制图；容器仍可选中 |
| 文件缺失 / 解码失败 | 同上 |
| import 源不可读 | 命令失败，返回 0 |
| `ms_document_load` 缺 document.json | errorOut + null |
| size ≤ 0 | 不绘制；hit/bounds 失败 |

**测试覆盖**：序列化 round-trip；ScaleMode dstRect；CommandBuilder；bridge import / add 空层 / set asset / undo；tgfx 快照；Swift package 含真实 assets 文件。

---

## 实现里程碑（建议）

1. 模型 + 序列化 + PropertyPath（`image.size`、scaleMode、Asset 宽高）
2. SceneEvaluator / EvaluatedImageItem / Hit+Bounds
3. DrawCommand + CommandBuilder + ScaleMode 数学
4. tgfx `drawImage` + 缓存 + 快照测试
5. Bridge：load 包路径、import asset、add 空层、set asset/scaleMode
6. App：Project 导入、Add 空层、Inspector（asset / scaleMode / 重置尺寸）、拖角模式切换、package 写入 assets
7. `ms_document_load` 迁移与 Swift 接线
