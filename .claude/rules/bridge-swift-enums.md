---
description: Bridge CF_CLOSED_ENUM 导入 Swift 与 SwiftUI 扩展约定
alwaysApply: true
---

## Bridge 枚举 → Swift

C ABI 用 `CF_CLOSED_ENUM` 定义标签枚举（见 `bridge/include/motionstudio_bridge.h`），经 `MotionStudioBridging` module 导入 Swift。

### 导入形态

```c
typedef CF_CLOSED_ENUM(int, MS_MASK) {
    MS_MASK_INVALID = -1,
    MS_MASK_ADD = 0,
    MS_MASK_SUBTRACT = 1,
    MS_MASK_INTERSECT = 2,
};
```

Swift 侧大致等价于（由编译器生成，勿手写镜像类型）：

```swift
public struct MS_MASK: Equatable, Hashable, RawRepresentable {
    public init(rawValue: Int32)
    public var rawValue: Int32
    public static var INVALID: MS_MASK { get }
    public static var ADD: MS_MASK { get }
    public static var SUBTRACT: MS_MASK { get }
    public static var INTERSECT: MS_MASK { get }
}
```

要点：

- 类型名保留 `MS_*`；case 名去掉公共前缀（`MS_MASK_ADD` → `.ADD`）
- closed enum 支持穷尽 `switch`；对带 `INVALID` 的类型，`switch` 必须显式覆盖 `.INVALID`
- **不会**自动获得 `CaseIterable` / `Identifiable`
- `INVALID = -1`（若有）是查询失败哨兵，不是用户可选值
- 禁止再手写与 bridge 平行的 Swift `enum` / `Int32` 镜像类型；直接使用导入类型

非 Apple 平台的 `CF_CLOSED_ENUM` 回退必须与 Apple 一致：

```c
#define CF_CLOSED_ENUM(_type, _name) _type _name; enum
```

### 扩展放置

所有对导入枚举的 Swift 扩展（`CaseIterable`、`Identifiable`、`label`、UI 辅助属性）统一写在：

`apps/MotionStudioApp/MotionStudioApp/Bridge/MotionStudioBridgingExtension.swift`

## SwiftUI：`CaseIterable` + `Identifiable`

仅在 SwiftUI 需要枚举列表时再扩展（`ForEach` / `Picker` 等）。查询用 / 非 UI 枚举不必强行补齐。

模板：

```swift
extension MS_MASK: @retroactive CaseIterable, @retroactive Identifiable {
    public static var allCases: [MS_MASK] {
        // 只列用户可选值；禁止包含 .INVALID
        [.ADD, .SUBTRACT, .INTERSECT]
    }

    public var id: Int32 {
        rawValue
    }

    var label: String {
        switch self {
        case .ADD: "Add"
        case .SUBTRACT: "Subtract"
        case .INTERSECT: "Intersect"
        case .INVALID: "Invalid"
        }
    }
}
```

规则：

- 必须加 `@retroactive`（协议来自 Swift 标准库，类型来自 C 模块）
- `allCases` **手动列出**合法选项；**禁止**把 `.INVALID` 放进 `allCases`
- `id` 用 `rawValue`（`Int32`）
- 若有展示文案，提供 `label`；`switch` 穷尽所有 case（含 `.INVALID`），供调试/兜底显示
- UI 绑定从 bridge 读到 `.INVALID` 时，在 getter 里回退到合理默认，再交给 `Picker`：

```swift
Binding {
    let mode = core.maskMode(layerID: layerID, index: index)
    return mode == .INVALID ? .ADD : mode
} set: { newValue in
    core.setMaskMode(layerID: layerID, index: index, mode: newValue)
}
```

## 新增 / 修改枚举时的检查清单

1. C 头：`typedef CF_CLOSED_ENUM(int, MS_FOO) { ... }`；需要失败哨兵时加 `MS_FOO_INVALID = -1`
2. 查询失败返回 `MS_FOO_INVALID`，不要伪装成合法默认值；命令参数非法值可在 bridge 内 clamp
3. 更新 `bridge_test` 覆盖失败哨兵与（若适用）非法命令标签
4. Swift：直接使用 `MS_FOO`；若 UI 要列表，在 `MotionStudioBridgingExtension.swift` 按上表扩展
5. 不要复制一份 Swift 侧枚举来「对齐」C 常量
