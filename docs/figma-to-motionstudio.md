# Figma → MotionStudio 对照换算

手抄或对照 Figma Design / Motion 旋转与时间到本项目时的换算约定。Core 语义对齐 AE / Lottie，**不会**为对齐 Figma 面板去改内部模型。

相关：时间模型见 [`data-model.md`](data-model.md) §1；Transform 见同页。

---

## 1. 时间：毫秒 → 帧

| | Figma Motion | MotionStudio |
|---|---|---|
| 时间轴单位 | 毫秒（ms） | 整数帧号 `FrameTime` |
| 速率 | 与预览时钟相关 | `Composition.frameRate`（`num/den`，默认常见 30/1） |

换算（与 `FrameRate::fromSeconds` 一致，四舍五入到最近帧）：

```text
seconds = figmaMs / 1000
frame   = round(seconds * frameRate.num / frameRate.den)
```

整数帧率（`den == 1`，`fps = num`）可简化为：

```text
frame = round(figmaMs * fps / 1000)
```

### 示例（合成帧率 30 fps）

| Figma Motion (ms) | MotionStudio 帧 |
|---|---|
| 0 | 0 |
| 500 | 15 |
| 1000 | 30 |
| 1500 | 45 |
| 33 | 1（`round(0.99)`） |
| 16 | 0（`round(0.48)`） |

### 注意

1. **先对齐帧率**：手抄前确认 MotionStudio 合成的 `frameRate` 与你打算使用的 fps 一致（例如都按 30）。Figma 时间轴本身不绑定「工程帧率」，换算必须显式选定 fps。
2. **非整数帧率**：用 `num/den`（如 30000/1001），不要先近似成 29.97 再乘，以减少累积误差。
3. **时长**：Figma 动画总长 `D` ms → 合成 `duration`（帧）≈ `round(D * fps / 1000)`；图层 `in`/`out` 同公式。
4. **吸附**：关键帧时间落在整数帧上；两帧若 round 到同一帧，按时间序微调 ±1 或合并，避免同 path 同帧双关键帧。
5. **逆向**：`figmaMs ≈ frame * 1000 * den / num`（展示用，可再 round）。

---

## 2. 旋转：符号与 Design + Motion

### 2.1 符号约定

| 来源 | 正角度方向 | 说明 |
|---|---|---|
| Figma **Design** 面板 / Plugin `rotation` | 逆时针为正 | Design 里 `+90` ≈ 逆时针 90° |
| Figma **Dev Mode** CSS `rotate()` | 顺时针为正（与 Design 相反） | Design `90°` → CSS 常为 `rotate(-90deg)` |
| MotionStudio / AE / Lottie | 顺时针为正（屏幕 Y 向下） | `Mat3::Rotate(+90)`：`(1,0) → (0,1)` |

相对 Figma Design / Motion **面板读数**：

```text
motionStudioRotation = -figmaPanelRotation
```

也可直接抄 Dev Mode 的 CSS 角度（已与本项目同向）。

### 2.2 Design 旋转 + Motion 旋转关键帧

Figma 图层常同时有：

1. **Design 旋转**：静态位姿（例如 `40.98`）
2. **Motion 旋转关键帧**：时间轴字段（例如 `5 → -3 → 31`）

Motion 模式里 Design 度数可能显示为 `0` 或单独字段——不代表没有基底。通常：

```text
figmaAbsoluteRotation ≈ designRotation + motionKeyframeRotation
```

MotionStudio 只有一条 `transform.rotation`。写入**绝对角度**后再取负。

### 2.3 静态位姿（仅 Design）

Design 旋转为 `R`，几何为**未烘焙旋转**的本地形状：

```text
transform.rotation（静态）= -R
```

例：Design `40.98` → MotionStudio `-40.98`。

**勿双重旋转：** 若路径/矩形已按画面朝向画好（几何已含旋转），则 `transform.rotation` 保持 `0`，不要再填 `R`。

### 2.4 带动画

设 Design 旋转为 `R`，Motion 关键帧为 `m[i]`，时间戳为 `tMs[i]`：

```text
frame[i]              = round(tMs[i] * fps / 1000)   // §1
motionStudioKF[i]     = -(R + m[i])
```

**不要：** 只动画 `m[i]`；静态保留 `-R` 再叠相对 `m[i]`；`R+m` 后忘记取负。

#### 数值示例

| 项 | Figma |
|---|---|
| Design 旋转 | `40.98` |
| Motion 关键帧（值） | `5 → -3 → 31` |
| Motion 时间（例） | `0ms → 400ms → 1000ms`（30 fps） |

| Figma t | Figma m | 绝对角 `R+m` | MS 帧 | MS `rotation` |
|---|---|---|---|---|
| 0 ms | 5 | 45.98 | 0 | **-45.98** |
| 400 ms | -3 | 37.98 | 12 | **-37.98** |
| 1000 ms | 31 | 71.98 | 30 | **-71.98** |

### 2.5 自检

1. 第一帧：画面接近 Design `40.98°` 且 Motion 为 `5` → MS 应接近 **`-46°`**，不是 `-5°` / `-41°`。
2. 对不上：以最终朝向为准，再取负。
3. 若 Motion 关键帧已是绝对角：只取负，不再加 `R`。

---

## 3. 相关代码

- 时间：`FrameRate::fromSeconds` / `toSeconds`（`include/MotionStudio/common/Time.h`）
- 旋转矩阵：`Mat3::Rotate`（`include/MotionStudio/common/Mat3.h`）
