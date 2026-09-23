# 回滚锚点（Rollback Anchors）

> 规则：每个阶段（P0…P6）**开工前**把当时 `HEAD` 的 SHA 追加到本文件。阶段失败时用
> `git restore .` 或从锚点开新分支回退，**绝不使用 `git stash`**（本机 `.git` 原子写不可靠，
> 已有两次事故：`.pack` 被删剩孤儿 `.idx`、refs 丢失）。
>
> 记录格式：`<阶段> | <ISO 时间> | <HEAD SHA> | <说明>`

## 锚点记录

| 阶段 | 时间 | HEAD SHA | 说明 |
| --- | --- | --- | --- |
| 基线（v2.9.30-baseline） | 2026-09-23 01:34 | `e3837ad769d1d24117b2bb99e8826c36b6d429d1` | 重构前基线（含 settings-ui 计划文档），tag baseline-v3.0.0 与 v2.9.30-baseline |
| P0 完成 | 2026-09-23 02:01 | `e751be2faeaae8f16b20499e9000570a25698b64` | C++23 语言标准、zencrop_build_flags、NOMINMAX 全量构建与测试通过 |
| P1 完成 | 2026-09-23 02:35 | `aec45a0afdae55db7c9a8815266d4ace4f3119ea` | 消除全部反向包含（59->0），消除跨模块双向依赖（26->10） |
| P2 完成 | 2026-09-23 02:44 | `9175beb06040c9406e289b59d6de21c61902248b` | 引入 7 个分层静态库与链接冒烟目标，产品目标直接列源收缩为 1 |
| P3 完成 | 2026-09-23 03:00 | `3d12a35368a73562a129d2b865582f3c30a846c2` | 测试全面切向链接分层静态库，testsCompilingProductCpp 降为 0，71 个全套密封测试 100% 通过 |
| P4 完成 | 2026-09-23 03:16 | `c464ab3f2780e90c8a514d8058dd53a3f552f4eb` | 拆 WideStringUtils 为聚焦域头，引入 PCH，hubHeaderDirectIncluders 降为 0，全量构建与 71 个测试通过 |
| P5 完成 | 2026-09-23 03:25 | `1ba100e932b6e8a05c3fcda1ef0fa3b3fe5848bb` | 引入 C++23 std::span、std::wstring_view、ComPtr RAII，71 个全套密封测试 100% 通过 |
| P6 完成 | 2026-09-23 03:26 | `500f711e967406e9ec1914ebfdab0f274cb7ebae` | 版本号全面升级至 v3.0.0，文档同步，P0–P6 全部 7 个阶段闸门全绿 |

## 状态说明

- 重构各阶段 P0–P6 均已完成并落地原子提交，回滚锚点已如上表全部固化。
- 如需回退某一阶段，请使用 `git restore .` 或从锚点 SHA 创建分支，**切勿使用 `git stash`**。
- 另有独立安全镜像：仓库根 `.bak/`（gitignored，非 Git 快照），刷新方式 `python scripts\python\make_safety_backup.py`。

## 恢复套路（.git 受损时用过一次）

备份 diff 到 `%TEMP%` → 清失效 refs / 孤儿 `.idx` → `fetch origin --tags` → `printf` 直写 refs →
`read-tree <commit>`（不带 `-u`）→ 逐字节 diff 比对。`gc.auto` 已设 0，保持关闭。
