# 回滚锚点（Rollback Anchors）

> 规则：每个阶段（P0…P6）**开工前**把当时 `HEAD` 的 SHA 追加到本文件。阶段失败时用
> `git restore .` 或从锚点开新分支回退，**绝不使用 `git stash`**（本机 `.git` 原子写不可靠，
> 已有两次事故：`.pack` 被删剩孤儿 `.idx`、refs 丢失）。
>
> 记录格式：`<阶段> | <ISO 时间> | <HEAD SHA> | <说明>`

## 锚点记录

| 阶段 | 时间 | HEAD SHA | 说明 |
| --- | --- | --- | --- |
| 治理落地（本轮） | 2026-09-17 09:37 | `0eaa91d26ee28611baa1201a95bee1ef25caf776` | **该 SHA 是治理改动之前的 HEAD**；本轮 14 项治理改动（守卫、基线、AGENTS 规则、packages 布局、格式化配置、测量工具）当时**尚未提交**，因此这个 SHA **不是**本轮工作的回滚点 |

## 重要：当前缺少可用的回滚锚点

- 本轮全部治理改动都在**工作区未提交状态**（`git status --short` 显示 14 项）。
- **实施第一个阶段之前必须先提交一次**，让锚点真实存在：

```powershell
git add -A
git commit -m "chore(arch): land architecture guard, layer contract and release layout"
git rev-parse HEAD
```

- 提交后把新的 SHA 追加到上表，作为 P0 收尾的锚点。
- 另有一个独立的安全网：仓库根的 `.bak/`（gitignored，非 Git 快照），
  刷新方式 `python scripts\python\make_safety_backup.py`。它按"最近一次刷新时间"生效，
  不随仓库自动更新——动重要文件前先刷一次。

## 恢复套路（.git 受损时用过一次）

备份 diff 到 `%TEMP%` → 清失效 refs / 孤儿 `.idx` → `fetch origin --tags` → `printf` 直写 refs →
`read-tree <commit>`（不带 `-u`）→ 逐字节 diff 比对。`gc.auto` 已设 0，保持关闭。
