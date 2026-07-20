---
description: 测试规则
alwaysApply: true
---

## 框架与组织

- GoogleTest 单元测试，CTest 驱动，`gtest_discover_tests` 接入（依赖经 DEPS/depctl 同步，不入库）。
- 测试文件与 src 同构：`tests/<模块>/<类型>Test.cpp`；新增测试文件登记到 `tests/CMakeLists.txt`。
- 用例命名 `TEST(类型名Test, 行为描述)`，一个用例验证一个行为。

## 验收流程

每次提交前本地全绿：

```bash
./sync_deps.sh   # 首次或 DEPS 变更后
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

CI（macOS runner）执行相同流程且 ASan+UBSan 开启。

## 断言约定

- **不使用 `EXPECT_THROW`**（项目无异常）：`Expected` 结果用 `hasValue()` / `errorMessage()` 断言。
- debug assert 的失败行为用 `EXPECT_DEATH(expr, "断言关键词")` 验证。
- 浮点比较用 `approxEqual`；`Vec2` 等聚合体作为宏参数时整体加括号或先赋值临时变量（避免宏参数歧义）。

## 专项测试模式

- **序列化 round-trip**：以 JSON 稳定性验证——`serialize → deserialize → serialize` 两次输出完全相等。
- **undo 一致性**：操作前后 `documentFingerprint` 相等（debug 断言级验收）。
- **随机/fuzz 测试**：固定随机种子保证可复现；1000 次随机命令 + 随机 undo/redo 在 ASan 下无崩溃无泄漏。
- **setParent 环检测**：自环、间接环、悬空父级三种用例必备。

## 覆盖要求

- 新模块、新命令、缺陷修复必须附带测试，测试全绿才允许提交。
- 命令类测试须覆盖：正常路径、undo/redo 往返、目标已删除时的静默跳过、可合并命令的合并行为。

## 性能基准

- 性能基准（如 M2 单帧求值 < 2ms）作为独立 benchmark 用例，CI 仅记录不阻断。
