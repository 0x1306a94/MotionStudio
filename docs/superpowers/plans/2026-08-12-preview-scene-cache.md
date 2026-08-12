# PreviewSceneCache 两级缓存 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 静止 playhead 下复用 `SceneState`（Document LRU）与 scene `DrawCommandList`（Canvas LRU），hit-test 不触发 `BuildCommands`；播放中 App 跳过 hit-test。

**Architecture:** `MSDocument` 持 `contentRevision` + `PreviewSceneCache`（`unique_ptr<Entry>`，精确 `PreviewTime` 键）；`MSCanvas::FrameCommandCache` 改为同样键/所有权模型，失效读 Document revision，EDIT/PLAYBACK 均缓存；统一 `EnsurePreviewScene` / `EnsureSceneCommands`。

**Tech Stack:** C++17 bridge、GoogleTest（`bridge_test`）、Swift App（`MotionDocumentCore` / `CanvasViewController`）。

**Spec:** `docs/superpowers/specs/2026-08-12-preview-scene-cache-design.md`

## Global Constraints

- 分支：非 `master` 时直接在当前分支提交；若在 `master` 先建 `feature/{username}_preview_scene_cache`。
- **自动 commit：** 每完成一个 Task 必须提交；**提交前先**把本 plan 对应 checkbox 改为 `[x]`、更新 `**Status:**`，再把代码与本 plan 一并 commit。
- Commit 信息：英语、≤120 字符、句号结尾、句中无其他标点；侧重用户可感知变化。
- 每完成一个 Step 立刻勾选；未同步 plan 视为该步未完成。
- 宣称完成前优先 ASan：`cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON && cmake --build build`，再跑相关 `ctest` / `bridge_test`。
- App / Xcode：优先 Xcode MCP `BuildProject`；不可用再 `xcodebuild`（见 `AGENTS.md`）。
- 编码规范：禁异常、禁 `dynamic_cast`、错误用 `Expected`；bridge 无业务逻辑膨胀，只做缓存与指针传递。
- C++17：无 `std::bit_cast`，用 `memcpy` 把 `PreviewTime`（`double`）转为 `uint64_t` 键。
- **不做：** Core `SceneEvaluator` 内缓存、evaluate 次次 atomic++、增量求值、GPU snapshot。

---

## 文件对照

| 文件 | 职责 |
|---|---|
| `bridge/src/common/PreviewTimeKey.h` | `PreviewTime` → `uint64_t` 键（两级缓存共用） |
| `bridge/src/common/PreviewSceneCache.h` + `.cpp` | Document 侧 SceneState LRU |
| `bridge/src/common/FrameCommandCache.h` + `.cpp` | Canvas 侧 DrawCommand LRU（演进） |
| `bridge/src/common/MSDocument.h` | `contentRevision` + `previewSceneCache` |
| `bridge/src/common/MSCanvas.h` | 删除 `contentRevision`；保留 `frameCommandCache` |
| `bridge/src/common/PreviewEnsure.h` + `.cpp` | `EnsurePreviewScene` / `EnsureSceneCommands` |
| `bridge/include/motionstudio_bridge.h` | Document revision API；删除 canvas revision API |
| `bridge/src/common/motionstudio_bridge_document.cpp` | revision get/set 实现 |
| `bridge/src/common/motionstudio_bridge_composition.cpp` | hit-test/bounds/handles → EnsurePreviewScene；删 printf |
| `bridge/src/common/motionstudio_bridge_canvas.cpp` | draw + path/gradient/motion hit → Ensure*；删 canvas revision |
| `bridge/tests/BridgeTest.cpp` | 缓存单测 + revision API + 集成行为 |
| `apps/.../MotionDocumentCore.swift` | `changed()` 推 Document revision |
| `apps/.../CanvasViewController.swift` | 去 canvas revision；播放跳过 hit-test |
| `docs/superpowers/specs/2026-08-12-preview-scene-cache-design.md` | 实现后可将状态标为已实现（可选） |

`bridge/CMakeLists.txt` 用 `add_files_by_extension` glob，新 `.cpp` 一般无需手改。

---

### Task 1: PreviewTimeKey + PreviewSceneCache + Document revision API

**Status:** ✅ Done

**Files:**
- Create: `bridge/src/common/PreviewTimeKey.h`
- Create: `bridge/src/common/PreviewSceneCache.h`
- Create: `bridge/src/common/PreviewSceneCache.cpp`
- Modify: `bridge/src/common/MSDocument.h`
- Modify: `bridge/include/motionstudio_bridge.h`（Document revision；本 Task 可先保留 canvas revision，Task 3 再删）
- Modify: `bridge/src/common/motionstudio_bridge_document.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Produces:
```cpp
// PreviewTimeKey.h
inline uint64_t PreviewTimeKey(motion::PreviewTime time);

// PreviewSceneCache
struct Entry { motion::PreviewTime time; motion::SceneState state; };
const Entry *find(motion::PreviewTime time) const;
Entry *put(motion::PreviewTime time, std::unique_ptr<Entry> entry);
void invalidateIfStale(uint64_t compositionId, uint64_t revision);

// C ABI
void ms_document_set_content_revision(MSDocument *document, uint64_t revision);
uint64_t ms_document_get_content_revision(const MSDocument *document);
```

- [x] **Step 1: 写失败测试（PreviewSceneCache + Document revision）**

在 `bridge/tests/BridgeTest.cpp` 增加：

```cpp
#include "PreviewSceneCache.h"
#include "PreviewTimeKey.h"

TEST(PreviewSceneCacheTest, HitPutAndRevisionInvalidation) {
    motionstudio::PreviewSceneCache cache;
    cache.invalidateIfStale(10, 1);
    auto entry = std::make_unique<motionstudio::PreviewSceneCache::Entry>();
    entry->time = 3.0;
    entry->state.viewportWidth = 100;
    entry->state.viewportHeight = 50;
    motionstudio::PreviewSceneCache::Entry *stored =
        cache.put(3.0, std::move(entry));
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->state.viewportWidth, 100);

    const auto *found = cache.find(3.0);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, stored);
    EXPECT_EQ(cache.find(3.1), nullptr);
    EXPECT_NE(PreviewTimeKey(3.0), PreviewTimeKey(3.1));

    cache.invalidateIfStale(10, 2);
    EXPECT_EQ(cache.find(3.0), nullptr);
    EXPECT_EQ(cache.size(), 0u);
}

TEST(PreviewSceneCacheTest, LruEvictsOldest) {
    motionstudio::PreviewSceneCache cache;
    cache.invalidateIfStale(1, 1);
    for (int i = 0; i < 257; ++i) {
        auto entry = std::make_unique<motionstudio::PreviewSceneCache::Entry>();
        entry->time = static_cast<double>(i);
        cache.put(entry->time, std::move(entry));
    }
    EXPECT_EQ(cache.size(), 256u);
    EXPECT_EQ(cache.find(0.0), nullptr);
    EXPECT_NE(cache.find(256.0), nullptr);
}

TEST(BridgeDocumentTest, ContentRevisionApi) {
    EXPECT_EQ(ms_document_get_content_revision(nullptr), 0u);
    ms_document_set_content_revision(nullptr, 7);

    MSDocument *document = ms_document_create();
    EXPECT_EQ(ms_document_get_content_revision(document), 0u);
    ms_document_set_content_revision(document, 42);
    EXPECT_EQ(ms_document_get_content_revision(document), 42u);
    ms_document_destroy(document);
}
```

- [x] **Step 2: 跑测试确认失败**

Run:
```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='PreviewSceneCacheTest.*:BridgeDocumentTest.ContentRevisionApi'
```
Expected: 编译失败或链接失败（尚无类型/API）。

- [x] **Step 3: 实现 PreviewTimeKey + PreviewSceneCache + MSDocument 字段 + C API**

`PreviewTimeKey.h`：

```cpp
#pragma once
#include <cstring>
#include <cstdint>
#include "MotionStudio/common/Time.h"

namespace motionstudio {
inline uint64_t PreviewTimeKey(motion::PreviewTime time) {
    static_assert(sizeof(motion::PreviewTime) == sizeof(uint64_t), "");
    uint64_t bits = 0;
    std::memcpy(&bits, &time, sizeof(bits));
    return bits;
}
}
```

`PreviewSceneCache`：镜像现有 `FrameCommandCache` LRU，但：
- map 值为 `std::unique_ptr<Entry>`
- key 为 `PreviewTimeKey(time)`
- `put` 接管 `unique_ptr`，同 key 覆盖，返回 `entry.get()`（存入后）
- `find` 返回 `const Entry*`（`ptr.get()`），并 `touch`

`MSDocument` 增加：

```cpp
#include "PreviewSceneCache.h"
// ...
uint64_t contentRevision = 0;
motionstudio::PreviewSceneCache previewSceneCache = {};
```

`ms_document_set/get_content_revision`：null 安全；set 时若 handle 非空写 `contentRevision`（**不**在此处 clear 缓存——由 `invalidateIfStale` 在 ensure 时处理）。set 可在持锁或不持锁：与现 canvas setter 一致（无 DocumentLock）；App 仅在主线程 `changed()` 调用。

声明放在 `motionstudio_bridge.h` document 段附近。

- [x] **Step 4: 跑测试确认通过**

Run: 同 Step 2  
Expected: PASS

- [x] **Step 5: 更新 plan 勾选并 commit**

```bash
git add bridge/src/common/PreviewTimeKey.h \
  bridge/src/common/PreviewSceneCache.h bridge/src/common/PreviewSceneCache.cpp \
  bridge/src/common/MSDocument.h bridge/include/motionstudio_bridge.h \
  bridge/src/common/motionstudio_bridge_document.cpp bridge/tests/BridgeTest.cpp \
  docs/superpowers/plans/2026-08-12-preview-scene-cache.md
git commit -m "$(cat <<'EOF'
Add document PreviewSceneCache and content revision API.

EOF
)"
```

---

### Task 2: EnsurePreviewScene + composition / hit-test 接入

**Status:** ✅ Done

**Files:**
- Create: `bridge/src/common/PreviewEnsure.h`
- Create: `bridge/src/common/PreviewEnsure.cpp`
- Modify: `bridge/src/common/BridgeInternals.h`（如需前向声明）
- Modify: `bridge/src/common/motionstudio_bridge_composition.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Consumes: `PreviewSceneCache`、`ResolvePointTextContainerSizes`、`SceneEvaluator::EvaluatePreview`
- Produces:
```cpp
namespace bridge {
motion::Expected<const motionstudio::PreviewSceneCache::Entry *, std::string>
EnsurePreviewScene(MSDocument *handle, uint64_t compositionId, motion::PreviewTime time);
}
```
调用方须已持 `DocumentLock`。`handle`/`document` 非空由调用方保证；composition 缺失返回 `Unexpected`。

- [x] **Step 1: 写失败集成测试**

```cpp
TEST(BridgePreviewSceneTest, HitTestReusesCachedSceneState) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ms_document_set_content_revision(document, 1);

    // 中心点命中（默认 rect 在 composition 中心 200x200）
    const float x = ms_composition_width(document, compositionId) * 0.5f;
    const float y = ms_composition_height(document, compositionId) * 0.5f;

    EXPECT_EQ(ms_composition_hit_test_layer(document, compositionId, 0.0, x, y, 0), layerId);
    EXPECT_EQ(document->previewSceneCache.size(), 1u);

    EXPECT_EQ(ms_composition_hit_test_layer(document, compositionId, 0.0, x, y, 0), layerId);
    EXPECT_EQ(document->previewSceneCache.size(), 1u);

    ms_document_set_content_revision(document, 2);
    EXPECT_EQ(ms_composition_hit_test_layer(document, compositionId, 0.0, x, y, 0), layerId);
    // invalidate + 重新 put：size 仍为 1，且能命中
    EXPECT_EQ(document->previewSceneCache.size(), 1u);
    EXPECT_EQ(document->previewSceneCache.revision(), 2u);

    ms_document_destroy(document);
}
```

（`BridgeTest` 已能 `#include "MSDocument.h"` / 访问内部；若 `ms_composition_size` 签名不同，改用现有 composition 宽高 API。）

- [x] **Step 2: 跑测试确认失败**

Run:
```bash
./build/bridge/bridge_test --gtest_filter='BridgePreviewSceneTest.*'
```
Expected: FAIL（仍直接 Evaluate，或 cache size 为 0）。

- [x] **Step 3: 实现 EnsurePreviewScene 并切换 composition API**

`PreviewEnsure.cpp` 按 spec 伪代码实现；**不要**调用 `BuildCommands`。

`motionstudio_bridge_composition.cpp` 中：

- `ms_composition_hit_test_layer`
- `ms_composition_layer_bounds`
- `ms_layer_local_bounds`
- `ms_composition_selection_handles`

改为在 `DocumentLock` 后：

```cpp
auto ensured = bridge::EnsurePreviewScene(document, compositionId, motion::PreviewTime(frameTime));
if (!ensured.hasValue()) {
    return /* 原失败返回值 */;
}
const motion::SceneState &state = ensured.value()->state;
// 使用 state（不再 ResolvePointText——ensure 内已做）
```

删除 hit-test 的 `ProfileClock` / `printf` 调试日志（及若仅为此引入且无其它引用的 include）。

- [x] **Step 4: 跑测试确认通过**

Run:
```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='BridgePreviewSceneTest.*:PreviewSceneCacheTest.*:BridgeDocumentTest.ContentRevisionApi'
```
Expected: PASS

另跑既有 hit-test 相关：
```bash
./build/bridge/bridge_test --gtest_filter='*HitTest*'
```

- [x] **Step 5: 更新 plan 勾选并 commit**

```bash
git commit -m "$(cat <<'EOF'
Reuse PreviewSceneCache in composition hit-test and bounds APIs.

EOF
)"
```

---

### Task 3: FrameCommandCache 演进 + EnsureSceneCommands + draw

**Status:** ✅ Done

**Files:**
- Modify: `bridge/src/common/FrameCommandCache.h`
- Modify: `bridge/src/common/FrameCommandCache.cpp`
- Modify: `bridge/src/common/PreviewEnsure.h` + `.cpp`（增加 `EnsureSceneCommands`）
- Modify: `bridge/src/common/MSCanvas.h`（删除 `contentRevision`）
- Modify: `bridge/include/motionstudio_bridge.h`（删除 canvas revision API）
- Modify: `bridge/src/common/motionstudio_bridge_canvas.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Produces:
```cpp
// FrameCommandCache
const Entry *find(motion::PreviewTime time) const;
Entry *put(motion::PreviewTime time, std::unique_ptr<Entry> entry);

const motion::DrawCommandList *
EnsureSceneCommands(MSCanvas *canvas, MSDocument *handle,
                    uint64_t compositionId, motion::PreviewTime time,
                    const motion::SceneState &state);
```

- [x] **Step 1: 改写 FrameCommandCache 单测为新 API**

将 `BridgeCanvasTest.FrameCommandCacheHitAndRevisionInvalidation` 改为：

```cpp
TEST(BridgeCanvasTest, FrameCommandCacheHitAndRevisionInvalidation) {
    motionstudio::FrameCommandCache cache;
    cache.invalidateIfStale(10, 1);
    auto entry = std::make_unique<motionstudio::FrameCommandCache::Entry>();
    entry->viewportWidth = 100;
    entry->viewportHeight = 50;
    entry->layerCount = 2;
    auto *stored = cache.put(3.0, std::move(entry));
    ASSERT_NE(stored, nullptr);

    const auto *found = cache.find(3.0);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->viewportWidth, 100);
    EXPECT_EQ(cache.find(3.1), nullptr);

    cache.invalidateIfStale(10, 2);
    EXPECT_EQ(cache.find(3.0), nullptr);
}
```

同步改 null-safe 测试：去掉 `ms_canvas_set/get_content_revision`，改为 Document revision（Task 1 已有）。

- [x] **Step 2: 跑测试确认失败**

Run:
```bash
./build/bridge/bridge_test --gtest_filter='BridgeCanvasTest.FrameCommandCache*'
```
Expected: 编译失败（旧 `put(int64_t, Entry)` 签名）。

- [x] **Step 3: 实现缓存演进 + draw 路径**

1. `FrameCommandCache`：键改为 `PreviewTimeKey`；`unique_ptr<Entry>`；`put` 返回裸指针；注释更新为 EDIT+PLAYBACK。
2. `EnsureSceneCommands`：`invalidateIfStale(compId, handle->contentRevision)`；hit 则返回 `&commands`；miss 则填 viewport 元数据 + `BuildCommands(state)` 后 `put`。
3. `ms_canvas_draw_frame_at_time_profiled`（及共用路径）：
   - 删除 `playbackMode` 门禁的 find/put 块与 `cacheFrame = floor(...)`。
   - `EnsurePreviewScene` → `EnsureSceneCommands` → `PlayCommands(*commands)`。
   - chrome 仍仅 `!playbackMode`，数据来自 `entry->state`。
4. 删除 `ms_canvas_set/get_content_revision` 实现与头文件声明；删除 `MSCanvas::contentRevision`。

- [x] **Step 4: 跑测试确认通过**

Run:
```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='BridgeCanvasTest.*:PreviewSceneCacheTest.*:BridgePreviewSceneTest.*'
```
Expected: PASS

- [x] **Step 5: 更新 plan 勾选并 commit**

```bash
git commit -m "$(cat <<'EOF'
Cache scene draw commands in edit and playback via document revision.

EOF
)"
```

---

### Task 4: Canvas 侧 path / gradient / motion-path hit 接入 EnsurePreviewScene

**Status:** ⏳ Pending

**Files:**
- Modify: `bridge/src/common/motionstudio_bridge_canvas.cpp`（约 `ms_canvas_hit_path_edit` / `ms_canvas_hit_gradient_edit` / `ms_canvas_hit_motion_path`）

**Interfaces:**
- Consumes: `bridge::EnsurePreviewScene`
- Produces: 无新 API；行为不变，少重复 evaluate

- [ ] **Step 1: 替换三处 EvaluatePreview**

在已有 `DocumentLock` 内：

```cpp
auto ensured = bridge::EnsurePreviewScene(document, compositionId, motion::PreviewTime(frameTime));
if (!ensured.hasValue()) {
    return /* 原空 hit */;
}
const motion::SceneState &state = ensured.value()->state;
```

勿对这三处调用 `EnsureSceneCommands`。

- [ ] **Step 2: 编译 bridge_test 并跑 canvas/composition 相关测试**

Run:
```bash
cmake --build build --target bridge_test
ctest --test-dir build -R 'Bridge' --output-on-failure
```
Expected: PASS

- [ ] **Step 3: 更新 plan 勾选并 commit**

```bash
git commit -m "$(cat <<'EOF'
Share PreviewSceneCache with canvas path and gradient hit tests.

EOF
)"
```

---

### Task 5: App — Document revision 推送 + 播放跳过 hit-test

**Status:** ⏳ Pending

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Canvas/CanvasViewController.swift`

**Interfaces:**
- Consumes: `ms_document_set_content_revision`
- Produces: 播放时不调用 `ms_composition_hit_test_layer`

- [ ] **Step 1: MotionDocumentCore.changed() 推送 revision**

```swift
private func changed() {
    revision += 1
    ms_document_set_content_revision(handle, UInt64(revision))
    onDidChange?()
}
```

确认 `handle` 非 nil（与其它 bridge 调用一致）。

- [ ] **Step 2: CanvasViewController 去掉 canvas revision；播放跳过 hit-test**

1. 删除所有 `ms_canvas_set_content_revision(...)`。
2. `hitTestLayer(at:)`：

```swift
private func hitTestLayer(at viewPoint: CGPoint) -> UInt64? {
    guard !isPlaying else { return nil }
    guard let scenePoint = scenePoint(fromViewPoint: viewPoint) else { return nil }
    return document.core.hitTestLayer(...)
}
```

3. `handleCanvasTap` / `handleCanvasDoubleTap` 入口增加 `guard !isPlaying else { return }`（双保险；与 pan 一致）。

- [ ] **Step 3: Xcode 编译**

优先 Xcode MCP `BuildProject`；失败则 `GetBuildLog`。  
Expected: BUILD SUCCEEDED

- [ ] **Step 4: 更新 plan 勾选并 commit**

```bash
git commit -m "$(cat <<'EOF'
Push content revision to the document and skip hit-test while playing.

EOF
)"
```

---

### Task 6: 收尾验证

**Status:** ⏳ Pending

**Files:**
- Modify: `docs/superpowers/specs/2026-08-12-preview-scene-cache-design.md`（状态改为已实现，可选）
- Modify: 本 plan 全部 Task `**Status:** ✅ Done`

- [ ] **Step 1: ASan 全量 bridge + 相关 core 测试**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build
ctest --test-dir build -R 'bridge_test|Bridge' --output-on-failure
```
Expected: PASS

- [ ] **Step 2: 手测清单（勾选记录）**

- 编辑静止：拖选/hover 流畅，无刷屏 evaluate 日志（printf 应已删）
- 播放中点击画布：不改变选中
- 暂停后点选：正常
- 循环播放：第二圈无明显 evaluate/build 尖峰（Instruments 或既有 frame profile 可选）

- [ ] **Step 3: 更新 plan/spec 状态并 commit**

```bash
git commit -m "$(cat <<'EOF'
Mark preview scene cache plan complete after verification.

EOF
)"
```

---

## Spec 覆盖自检

| Spec 要求 | Task |
|---|---|
| Document `PreviewSceneCache` + `unique_ptr` put | 1 |
| `contentRevision` C API；删 canvas revision | 1 + 3 + 5 |
| `EnsurePreviewScene`；composition API | 2 |
| FrameCommandCache 精确 time + unique_ptr；EDIT/PLAYBACK | 3 |
| `EnsureSceneCommands`；draw 路径 | 3 |
| canvas path/gradient/motion hit | 4 |
| App revision + 播放跳过 hit-test | 5 |
| 测试与验证 | 1–3、6 |
| 不做 evaluate atomic++ / Core 缓存 | 全局约束 |

## 类型一致性自检

- 两级缓存：`find(PreviewTime)` / `put(PreviewTime, unique_ptr<Entry>) -> Entry*`
- 键：`PreviewTimeKey`（`memcpy`）
- 失效：均 `invalidateIfStale(compositionId, document->contentRevision)`
- `EnsurePreviewScene` 返回 `Expected<const Entry*, string>`；draw/hit 用 `->state`
- `EnsureSceneCommands` 返回 `const DrawCommandList*`
