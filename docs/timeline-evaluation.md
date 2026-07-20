# 时间轴与曲线求值

本文档定义关键帧动画的求值机制：给定时间 `t`，如何算出每个 `Animatable<T>` 的值。数据模型见 [data-model.md](data-model.md)。

## 1. 求值流程

```
evaluate(Animatable<T>, t):
  1. 无关键帧        → 返回静态值
  2. t ≤ 首帧 / ≥ 末帧 → 钳制到首/末帧的值（不外推）
  3. 二分查找所在区间 [kf0, kf1]
  4. progress = (t - kf0.time) / (kf1.time - kf0.time)    // 归一化到 [0,1]
  5. easedProgress = applyEasing(kf0.easing, progress)    // 时间缓动
  6. 若为 Vec2 且有空间手柄 → 空间贝塞尔求值（见 §3）
     否则 → Interpolator<T>::lerp(kf0.value, kf1.value, easedProgress)
```

```cpp
template<typename T>
T evaluateKeyframes(const std::vector<Keyframe<T>>& keyframes, FrameTime t) {
    if (t <= keyframes.front().time) return keyframes.front().value;
    if (t >= keyframes.back().time)  return keyframes.back().value;

    auto it = std::upper_bound(keyframes.begin(), keyframes.end(), t,
        [](FrameTime t, const Keyframe<T>& kf) { return t < kf.time; });
    const auto& kf0 = *(it - 1);
    const auto& kf1 = *it;

    float progress = float(t - kf0.time) / float(kf1.time - kf0.time);
    float eased = applyEasing(kf0.easing, progress);
    return Interpolator<T>::lerp(kf0.value, kf1.value, eased);
}
```

`applyEasing`：`Linear` 直接返回 progress；`Hold` 返回 0（保持前值直到下一帧）；`Bezier` 调用 §2 的求解器。

## 2. 贝塞尔缓动求值

缓动曲线是三次贝塞尔：P0=(0,0)、P1=(inX,inY)、P2=(outX,outY)、P3=(1,1)。给定时间进度 x ∈ [0,1]，求值进度 y。x(t) 单调但反函数无解析解，需数值求解：

```cpp
float solveBezierEasing(float x1, float y1, float x2, float y2, float x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;

    // 阶段一：牛顿迭代（通常 4~8 次收敛）
    float t = x;
    for (int i = 0; i < 8; i++) {
        float err = cubicBezierX(x1, x2, t) - x;
        if (std::abs(err) < 1e-7f) break;
        float dx = cubicBezierDX(x1, x2, t);
        if (std::abs(dx) < 1e-7f) break;
        t -= err / dx;
    }

    // 阶段二：牛顿发散时二分兜底
    if (t < 0 || t > 1) {
        float lo = 0, hi = 1;
        t = x;
        for (int i = 0; i < 20; i++) {
            float cur = cubicBezierX(x1, x2, t);
            if (std::abs(cur - x) < 1e-7f) break;
            (cur < x ? lo : hi) = t;
            t = (lo + hi) * 0.5f;
        }
    }
    return cubicBezierY(y1, y2, t);
}

// B(t) = 3(1-t)²t·p1 + 3(1-t)t²·p2 + t³
inline float cubicBezierX(float x1, float x2, float t) {
    float mt = 1 - t;
    return 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t * t * t;
}
```

**牛顿 + 二分混合的理由**：纯牛顿法在极端控制点（inX 接近或超出 [0,1]）时可能发散；纯二分法收敛慢。此混合策略与 WebKit/Blink 的 CSS transition 引擎相同，经过充分验证。验收标准：与 CSS `cubic-bezier()` 参考实现误差 < 1e-5。

## 3. 空间插值（Spatial Bezier）

position 类属性有两个**相互独立**的维度：

| 维度 | 控制什么 | 数据来源 |
|---|---|---|
| 时间缓动（§2） | **何时**到达下一关键帧 | `Keyframe.easing` |
| 空间插值（本节） | **沿什么路径**移动 | `Keyframe.spatialIn/OutTangent` |

求值顺序：progress 先经时间缓动得到 easedProgress，再代入空间曲线：

```cpp
Vec2 evaluateSpatial(const Keyframe<Vec2>& kf0, const Keyframe<Vec2>& kf1,
                     float easedProgress) {
    if (!kf0.spatialOutTangent || !kf1.spatialInTangent)
        return Interpolator<Vec2>::lerp(kf0.value, kf1.value, easedProgress);

    // 空间三次贝塞尔：P0=起点, P1=起点+出手柄, P2=终点+入手柄, P3=终点
    return cubicBezierPoint(kf0.value,
                            kf0.value + *kf0.spatialOutTangent,
                            kf1.value + *kf1.spatialInTangent,
                            kf1.value,
                            easedProgress);
}
```

无空间手柄时退化为直线插值。空间手柄仅在 `Animatable<Vec2>`（position）上有意义；UI 上表现为关键帧之间的弧线拖拽手柄。

## 4. Precomp 时间映射

嵌套合成（Precomp）图层把外部时间映射到源合成的内部时间：

```
innerTime = (outerTime - layer.inPoint) × layer.timeStretch + layer.startTime
```

求值 Precomp 图层时：先算 innerTime，再对源 Composition 的所有图层按 innerTime 递归求值。验收：至少 3 层嵌套 Precomp 的正确性测试。

## 5. 缓存策略

**M1 不做求值缓存**。理由：单属性求值本身很快（一次二分 + 几次浮点运算）；缓存的主要价值在避免重复遍历 parent 变换链，而这在典型场景（< 100 图层）下开销可忽略。

M2 性能测试（100 图层 × 50 关键帧，单帧求值 < 2ms）若不达标，再引入：

```cpp
class EvaluationCache {
    void invalidate(EntityId id, const std::string& propertyPath);  // 命令修改后调用
    void invalidateAll();
    // 键：(entityId, propertyPath, time)，脏标记由 Animatable 维护
};
```

## 相关文档

- 属性与关键帧的数据结构：[data-model.md](data-model.md)
- 求值结果如何变成画面：[rendering.md](rendering.md)
