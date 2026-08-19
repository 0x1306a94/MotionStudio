---
description: Git 操作规范
alwaysApply: true
---

## 分支命名

格式：`feature/{username}_模块名` 或 `bugfix/{username}_模块名`

- `{username}`：git config 用户名，全小写
- 模块名用下划线连接，最多两个单词

## Commit 信息

120 字符内的英语概括，以英文句号结尾，中间无其他标点，侧重描述用户可感知的变化。

## 提交

- 源码格式化由 git hook `pre-commit` 在提交时自动处理（仅暂存的 C++/ObjC/Swift）；无需在每次提交前手动跑 `./codeformat.sh`。`./codeformat.sh` 仅用于需要全量格式化时的手动执行
- **NEVER** 自动执行 `git stash`、`git reset`、`git checkout` 等改变暂存区或工作区状态的命令，除非**暂存区为空**或**用户明确要求**

### spec / plan 文档

撰写或修订 `docs/superpowers/specs/`、`docs/superpowers/plans/`（及仅为此改的 `docs/README.md` 链接）时：**写完后先给用户看，等用户明确确认后再 commit**。禁止写完就自动提交，也禁止把多次 spec/plan 修订拆成一串小 commit。

### 自动提交

完成任务后自动提交（仅 commit，**不自动推送**），无需用户额外指示。**上一条 spec/plan 例外优先。**

1. **NEVER** 在 master 分支直接提交，先按「分支命名」规范创建新分支；非 master 分支直接在当前分支提交即可
2. **NEVER** 使用 `--amend` 修改已有 commit，始终创建新 commit
3. 仅提交本次会话中由你的操作引起的变更（包括但不限于：代码编辑、文件删除/重命名、代码格式化、截图基准更新等），使用 `git commit --only <file1> <file2> ... -m "{Commit 信息}"`
4. 忽略非本次会话引起的变更：不恢复，保持原样

### 手动提交

用户执行 `/commit` 或主动要求提交时，**不受自动提交规则限制**：

- 允许在 master 分支直接提交
- 提交范围由用户决定或按 commit skill 流程处理工作区中的**所有变更**，不区分是否为本次会话引起

### 按 plan 实现时的状态同步

执行 `docs/superpowers/plans/`（或用户指定的同类实现计划）时：

1. **每完成一个 Step（或整个 Task）必须立刻**在 plan 文件中把对应 `- [ ]` 改为 `- [x]`，并更新该 Task 的 `**Status:**`（如 `✅ Done` / 进行中 / 阻塞原因）
2. **禁止**全部做完再一次性勾选；未同步更新 plan 视为该步未完成
3. plan 状态变更与该步代码变更一并 commit，或紧随其后单独 commit

## Worktree

创建 worktree 新分支时必须加 `--no-track`，避免继承源分支的 tracking 关系。创建完成后，将主仓库的 `tests/` 基线数据、`third_party/` 依赖拷贝到新 worktree 目录下（`tests/out/`、`tests/baseline/` 可能尚不存在，不存在则跳过）：

```bash
git worktree add <path> -b <branch> --no-track master
cp -R tests/out/ <path>/tests/out/ 2>/dev/null || true
cp -R tests/baseline/ <path>/tests/baseline/ 2>/dev/null || true
cp -R third_party/ <path>/third_party/
```