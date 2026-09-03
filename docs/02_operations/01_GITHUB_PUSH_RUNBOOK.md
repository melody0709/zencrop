# ZenCrop GitHub 提交与推送操作手册

更新日期：2026-08-28

本文件用于后续将 ZenCrop 的公开代码安全提交并推送到 GitHub。后续可以把本文件交给 Codex，并明确说“按此文档检查、提交并 push”，由 Codex 在重新核对现场状态后执行。

## 1. 执行授权

- 仅提供或引用本文件，不代表允许 Git 写操作。
- 用户明确说“按文档提交并 push”“把这次修改推上去”等同类指令后，才授权本次任务所需的 `stage`、`commit` 和普通 `push`。
- 授权范围仅包含当前任务相关文件；不得夹带已有修改。
- `tag`、GitHub Release、资产上传、删分支、改历史和 force push 必须由用户另外明确提出。
- 不执行 `git commit --amend`、`git rebase`、`git push --force` 或 `git push --force-with-lease`，除非用户在当次任务中单独明确授权并已确认影响范围。

## 2. 仓库身份与公开基线

### 本地开发仓库

```text
D:\GITHUB_melody0709\zencrop_ocr_pxipin
```

### GitHub 公开仓库

```text
https://github.com/melody0709/zencrop
```

远端默认分支：`master`

2026-08-28 建立公开仓库时的单一根提交：

```text
fc70f1366d4152808371a55145ee9c02d73c1850
```

以上提交只用于确认首次连接的历史来源。后续执行时必须重新读取 GitHub `master`，不得假设它仍是最新提交。

### 文档建立时的本地状态

```text
本地分支：main
本地 HEAD：50854871d819c15f6d7cc0aa7e556ce36c07a8f4
Git remote：未配置
```

本地旧历史和 GitHub 的公开单根历史不相交，禁止直接把这个旧 `main` force push 到 GitHub。

文件树核对结果：GitHub 的 939 个公开文件与本地对应文件内容一致；本地旧提交额外跟踪了 11 个 `.opencode/` 文件。首次连接时必须保留这些文件供本地使用，但不能重新加入公开分支。

## 3. 私有研究资料边界

以下内容永远不得进入公开提交：

- 当前仓库中的 `.research/` 本地研究工作区或目录联接。
- 私有研究仓库中的二进制、逆向输出、分析资料和本机路径。
- `reverse/`、反编译输出、临时分析文件和第三方私有样本。
- `.plan/` 中的历史施工计划和内部目标记录。
- `.opencode/` 中仅供本地 IDE/Agent 使用的命令和技能文件。
- `build/`、`tmp/`、测试输出、诊断输出和其他生成文件。
- PFX、私钥、签名令牌 PIN、API Token、密码、Cookie、机器凭据和包含秘密的环境配置。

`.research/` 必须继续由 `.gitignore` 排除。`.opencode/` 在切换到公开分支后应写入本仓库本地的 `.git/info/exclude`，不要为了本机配置修改公开 `.gitignore`。

私有研究仓库仍可被本地开发和分析流程按其规则访问；公开仓库不复制其内容，只保留不含私有证据、对象名称和本机路径细节的生产实现。

## 4. 首次连接 GitHub 的一次性流程

此流程仅在当前仓库仍没有 `origin`、本地仍处于旧 `main` 历史时执行一次。

### 4.1 前置检查

1. 使用 PowerShell 7。
2. 运行 `git status --short`，工作树必须干净；若不干净，先报告用户，不能丢弃或自动整理修改。
3. 核对 `git remote -v`、当前分支、当前 HEAD。
4. 通过 GitHub CLI 核对 `melody0709/zencrop` 的默认分支和最新 `master` 提交。
5. 确认 GitHub 仓库是公开仓库，目标地址拼写准确。

### 4.2 保留旧本地历史

在修改分支关系前，为当前 HEAD 建立清晰的本地备份分支，例如：

```text
local-pre-github-link-20260828
```

这个分支只用于本地追溯，不设置 upstream，不推送到 GitHub。

### 4.3 添加并核对 origin

将以下地址添加为 `origin`：

```text
https://github.com/melody0709/zencrop.git
```

随后执行 fetch，包括 tags。不得在 fetch 后立即 push，必须先验证：

- `origin/master` 存在。
- 首次连接时，`origin/master` 的历史来源是公开单根提交 `fc70f136...` 或它的正常后继提交。
- 远端没有未知的强制改写、陌生提交或需要用户确认的并行修改。

### 4.4 建立公开跟踪分支

从 `origin/master` 建立本地 `master`，并设置 upstream 为 `origin/master`。不要将旧本地 `main` 合并进公开 `master`，也不要使用 `--allow-unrelated-histories`。

切换后，从本地备份分支仅恢复 `.opencode/` 到工作树，不加入 index；然后将 `.opencode/` 写入 `.git/info/exclude`。完成后必须确认：

```text
git status --short
```

没有输出，并且：

- 当前分支为 `master`。
- upstream 为 `origin/master`。
- `.opencode/` 文件仍在本机，但不受公开分支跟踪。
- `.research/` junction 仍可访问私有研究仓库，但保持 ignored。

首次连接只建立跟踪关系，不生成无意义提交，不 push 空提交。

## 5. 日常提交与 push 流程

用户明确授权本次 push 后，按以下顺序执行。

### 5.1 核对现场

```text
git status --short
git branch --show-current
git branch -vv
git remote -v
git fetch origin --prune --tags
```

要求：

- 当前公开开发分支应为 `master`，upstream 为 `origin/master`。
- remote URL 必须是 `melody0709/zencrop`。
- 不得将私有研究仓库配置成此仓库的 push remote。
- 检查远端是否领先或发生分叉。

如果远端存在本地未知的新提交，停止 push 并报告用户；不得自行 merge、rebase 或 force push。

### 5.2 审查修改范围

1. 阅读 `git status --short`。
2. 阅读当前任务相关的 `git diff`。
3. 检查是否包含大文件、二进制、私有路径、逆向资料、凭据或生成目录。
4. 使用搜索检查新增文本是否意外出现私有对象名称、本机研究路径、私有仓库文件名、证书密码或 Token。
5. 只 stage 当前任务文件，优先使用精确路径；禁止无审查地使用 `git add -A` 或 `git add .`。

### 5.3 验证

- 按修改风险运行增量构建和直接相关的既有测试。
- 发布前按项目发布规则执行更完整的测试和打包验证。
- 源码不再变化后运行一次 `git diff --check`。
- 检查 staged diff，确认提交中没有任务外文件。

### 5.4 提交

提交信息应描述实际产品变化，不写入私有研究来源或不必要的逆向对象名称。例如：

```text
fix: correct OCR result layout
feat: add configurable capture behavior
docs: update GitHub push workflow
```

提交后再次检查：

```text
git status --short
git show --stat --oneline HEAD
```

### 5.5 推送与远端确认

只执行普通 push：

```text
git push origin master
```

如果 push 被拒绝，停止并读取远端状态；不得自动 force push。

push 成功后，通过 GitHub CLI 重新读取远端 `master`，确认：

- 远端 HEAD 等于刚提交的 commit。
- 提交文件列表与本次任务一致。
- GitHub 页面没有出现私有研究资料或意外生成资产。

## 6. Tag 与 Release 是独立操作

普通代码 push 不自动创建 tag 或 Release。只有用户明确要求发布时才执行。

当前已发布的版本基线：

```text
Tag：v2.9.15
Release：Unsigned Engineering Pre-release
资产：MSI、MSI SHA-256、Portable 7z、Portable SHA-256
```

同名 Release 资产不得覆盖。新增正式 payload、签名语义或安装器变化时必须先提升三段版本号。未签名产物继续使用 `-unsigned` 文件名，并明确标记为 engineering/pre-release。

发布操作至少包括：

1. 核对并提升版本号。
2. 构建和测试。
3. 生成请求的 MSI 和/或 Portable 包。
4. 核对本地 SHA-256 sidecar。
5. 创建用户明确要求的 tag。
6. 创建或更新对应 Release；不得覆盖同名资产。
7. 上传后比较 GitHub 资产 digest、大小和本地文件。

## 7. 后续交给 Codex 的推荐指令

仅提交代码：

```text
请读取 docs/02_operations/01_GITHUB_PUSH_RUNBOOK.md，按文档检查当前修改，只提交并 push 本次任务相关文件，不创建 tag 或 Release。
```

提交并发布：

```text
请读取 docs/02_operations/01_GITHUB_PUSH_RUNBOOK.md，按文档提交并 push 本次修改；版本升级为 x.y.z，同时生成 unsigned MSI 和 Portable，创建 tag 并发布 GitHub pre-release。
```

首次连接 IDE：

```text
请读取 docs/02_operations/01_GITHUB_PUSH_RUNBOOK.md，执行首次 GitHub 连接流程。保留旧本地历史和本机 .opencode 文件，不提交、不 push。
```

## 8. 异常时必须停止的情况

遇到以下任一情况，不继续 push：

- remote 指向的不是 `melody0709/zencrop`。
- 远端分支出现未知提交、历史分叉或 force-update。
- staged diff 包含私有研究资料、`.research/`、`.opencode/`、`build/` 或凭据。
- 需要 merge unrelated histories、rebase、amend 或 force push 才能继续。
- 版本 Tag 或同名 Release 资产已经存在且内容不同。
- 构建、测试、布局校验、checksum 或 GitHub digest 验证失败。
- 用户已有修改与当前任务重叠，无法安全区分。

停止后应报告具体 commit、文件或失败检查，并等待用户决定，不通过删除历史或覆盖远端来绕过问题。
