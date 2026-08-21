---
description: Wait for user confirmation, then commit spec/plan before implementing
alwaysApply: true
---

# spec / plan 提交

撰写或修订 `docs/superpowers/specs/`、`docs/superpowers/plans/`（及仅为此改的 `docs/README.md` 链接）时，按下面时序，不要跳步。

## 1. 写完先给用户看，确认前不 commit

- 不要写完就自动提交
- 确认前同一份 spec/plan 的多次修订攒一次，不要拆成一串小 commit

## 2. 用户确认后：先单独提交，再开始实现

以下任一表述都视为内容确认，并授权提交 spec/plan（不必再问一遍）：「确认」「可以」「可以了」「OK」「LGTM」「开始实现」。

确认后立刻单独 commit，再动手写实现代码。禁止：

- 未提交就开始实现
- 把 spec/plan 和实现代码混进同一个 commit

同一特性的 spec + plan（及仅为此改的 `docs/README.md` 链接）合成 **一个** commit。

此步提交是对「完成后不自动 commit / 等用户手动提交」偏好的例外，不必等 `/commit`。

## 3. 实现中的 plan 状态更新不是撰写

按 plan 勾选 checkbox、改 `Status:` 不算本节的「撰写或修订」。仍随该步代码一并 commit（或紧随其后单独 commit），规则见 `git-workflow.md`「按 plan 实现时的状态同步」。
