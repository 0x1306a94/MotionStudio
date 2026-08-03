# Layout Position UI（左上角 Position）— 设计说明

日期：2026-08-03  
状态：已实现（待人机验收）  

## 目标

所有向用户**展示 / 编辑**的 `transform.position` 数字，语义为：图层**局部 AABB 左上角**在父空间中的位置（接近 Figma / Sketch 的布局坐标）。

底层存储、渲染矩阵、序列化、Lottie / PAG 导出保持现有 AE 语义不变：

```
local = T(position) · R(rotation) · S(scale) · T(-anchorPoint)
```

即存储的 `position` 仍是锚点在父空间中的位置。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 改动层 | 仅 App UI 呈现 / 编辑（方案 A） |
| 覆盖范围 | 所有露出 `transform.position` 数字的 UI（方案 C） |
| 左上角定义 | 局部 AABB `localBounds.min`（非局部原点硬编码） |
| 补偿 | 按实际 `anchor` / `scale` / `rotation` + bounds，不写死 `size * 0.5` |
| 实现路径 | Swift 纯函数 + `MotionDocumentCore` 门面（方案 1） |
| 旋转 / 缩放 | `offset = R·S·(anchor − bounds.min)` |

## 非目标

- 不改 Core `Transform` 模型、文件格式、schema
- 不改 `ShapeProperty.position`（形状内部偏移）
- 不改 FreeTransform 等内部几何写回（继续写存储坐标）
- 不做屏幕视觉 AABB（旋转后数字对应局部 min 经 `R·S` 映射的点，不一定等于选中框屏幕左上角）
- 不改画布运动路径描线（锚点轨迹可视化，不是数字编辑）

## 换算

局部点 `p` 在父空间：

```
parent(p) = position + R · S · (p − anchor)
```

取 `p = localBounds.min`。定义 UI offset 为锚点相对左上角的父空间向量：

```
offset(frame) = rotate(scale(anchor − bounds.min))
layoutPosition  = storedPosition − offset   // UI 显示（= parent(bounds.min)）
storedPosition  = layoutPosition + offset   // UI 写入
```

`localBounds` 来自现有 `ms_layer_local_bounds` / `MotionDocumentCore.layerLocalBounds`。

边界：

- `localBounds` 不可用或空 → `offset = 0`，退化为原始 `position`
- 修改 anchor / scale / rotation 时**不**自动改存储 `position`；layout 数字可能变化，世界外形不变
- 锚点预设仍写存储 `position`（现有 `AnchorPreset.compensatedPosition`）；UI 读数走 layout 门面

## 架构

```
UI (Inspector / 关键帧数值 / Motion Path 点)
  → MotionDocumentCore.evaluateLayoutPosition / writeLayoutPosition
      → layerLocalBounds + evaluate(anchor, scale, rotation, position)
      → LayoutPosition.{offset, toLayout, toStored}
      → 现有 setStaticVec2 / addKeyframeVec2(transform.position)

内部几何 (FreeTransform / 锚点补偿 / recenter)
  → 直接 evaluateVec2 / writeVec2(transform.position)   // 存储坐标
```

## 核心接口（Swift）

```swift
enum LayoutPosition {
    static func offset(anchor: CGVector,
                       scale: CGVector,
                       rotationDegrees: Float,
                       localBounds: CGRect) -> CGVector
    static func toLayout(stored: CGVector, offset: CGVector) -> CGVector
    static func toStored(layout: CGVector, offset: CGVector) -> CGVector
}

// MotionDocumentCore
func evaluateLayoutPosition(compositionID: UInt64,
                            layerID: UInt64,
                            frame: Int64) -> CGVector
func writeLayoutPosition(compositionID: UInt64,
                         layerID: UInt64,
                         frame: Int64,
                         value: CGVector)
```

`writeLayoutPosition` 内部沿用现有规则：playhead 已有关键帧则 upsert keyframe，否则 `setStatic`。

## 入口清单

### 改为 layout 读写

- `TransformInspector`：Position X/Y
- 任何直接展示 / 编辑 `transform.position` **关键帧数值**的 UI
- `MotionPathInspector`：关键帧点 `p0/p3` 按各自帧 offset 转为 layout 显示；切线为相对位移——段内 offset 不变时完全正确；段内 anchor / scale / rotation / bounds 动画时为近似（接受）

### 保持存储坐标

- `FreeTransformDrag` 及 resize / 锚点补偿写回
- `MotionDocumentCore` 内几何辅助写回
- 画布运动路径 chrome
- Bridge 通用 `evaluateVec2` / 序列化 / 导出
- 添加关键帧：对当前**存储求值**打点（外形已正确）

## 测试

纯函数 / Swift 测试优先：

1. Image `200×200`、anchor=`(100,100)`、position=`(200,200)` → layout=`(100,100)`
2. recenter Shape：bounds≈`(-w/2,-h/2,w,h)`、anchor=`(0,0)`、position=`(P,P)` → layout=`(P−w/2, P−h/2)`
3. 写入 layout=`(0,0)` 后视觉左上角落在父空间原点；存储 position = layout + offset
4. 旋转 90° / 非单位 scale：用 `R·S·(min−anchor)` 验证读写往返
5. bounds 缺失：offset=0，等于原始 position

手动验收：

- 新建 rect / image 后 Inspector 输入 `(0,0)`，视觉左上角贴合成左上角
- 改锚点预设后 layout 数字变化、外形不动
- 画布拖拽后 Inspector 与视觉左上角一致

## 实现顺序（概要）

1. `LayoutPosition` 纯函数 + 单元测试
2. `MotionDocumentCore` 门面
3. `TransformInspector` 接入
4. `MotionPathInspector` 关键帧点显示换算（若有其它数值入口一并接入）
5. 手动验收上述场景
