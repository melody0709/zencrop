# 翻译取消与关闭窗口响应方案（v1）

- **状态**：下一轮独立任务，已完成源码定位；尚未实施或实机签收。
- **日期**：2026-10-03（用户所在时区）。
- **优先级**：高，先于长结构化文本边界任务。
- **起点**：v3.1.7 加本轮翻译引擎去重与诊断修正；本轮不修改 transport 生命周期。
- **目标**：取消翻译、关闭结果窗、关闭服务商页时，UI 不再等待不可预测的网络退出；保持安全回收、一次终态回调和应用退出时的完整清理。

## 1. 已知事实与尚未证明的部分

| 事实 | 当前入口 | 含义 |
|:---|:---|:---|
| WinHttpOpen 的最后一个参数为 0 | `src/translation/AsyncHttpTransport.cpp:288-289` | 当前网络调用是同步 WinHTTP，不因类名含 Async 而变为原生异步 |
| Cancel 与 deadline watchdog 都调用 CloseActiveHandles | 同文件 `:558-578` | 取消线程会关闭工作线程正在使用的网络句柄 |
| Join 对工作线程执行 INFINITE 等待；析构调用 Cancel、Join | 同文件 `:581-615/647-654` | 删掉 UI 的显式 Join 仍可能在最后一个 shared_ptr 析构时阻塞 |
| 协调器取消、窗口关闭与 Shutdown 都同步回收 operation | `TranslationCoordinator.cpp:1253-1258/1895-1908/1180-1204` | UI 消息处理与网络退出耦合 |
| Test connection 与 Fetch models 的取消也会 Join | `TranslationProviderSettingsPage.cpp:1533-1534/1835-1836` | 修复范围需要覆盖同一 operation 的其他 UI owner |
| complete 与 completionEvent 在用户回调之前设置 | `AsyncHttpTransport.cpp:184-198` | IsComplete 不等于回调已经退出，不能单凭该标志释放 owner 或 drain 最后一轮消息 |

同步句柄仍在 API 调用期间被关闭会产生竞争；若改为原生异步，请求关闭返回后仍可能收到回调，关联状态需保留至 HANDLE_CLOSING 或等价同步完成。依据 [Microsoft WinHttpCloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle) 与 [HINTERNET 句柄说明](https://learn.microsoft.com/en-us/windows/win32/winhttp/hinternet-handles-in-winhttp)。

这证明存在结构风险，尚不能证明用户现场必然冻结，也不能推断每次取消耗时。既有 `TestWinHttpCancelAndShutdown` 覆盖约 300ms 的 loopback 延迟，允许 Cancel+Join 最长 2000ms；它验证了基本回收，却没有证明 UI 消息泵持续响应或 DNS/代理/TLS 阶段可取消。

## 2. 第一阶段：复现与量化

复用 `tests/test_deepseek_protocol_contract.cpp` 的 LoopbackHttpServer 与既有测试目标，不建新 executable 或 test-only production API。

1. 用事件确认服务端已收到请求，再进入“迟迟不返回响应头”“响应头后停止 body”“持续小块 body”三个可控阶段；不用固定 sleep 假定请求已经到达指定阶段。
2. 分开测量 Cancel 调用、UI 关闭命令返回、最终回调退出及 operation 回收时间；长接收超时用于证明 UI 不依赖它结束。
3. 在现有翻译 UI 测试宿主中发取消/关闭命令，同时投递探针消息，记录 UI 能否继续处理；通过注入既有 transport 控制网络故障。
4. 手动覆盖 DNS、代理/PAC、连接与 TLS 等 loopback 无法完整模拟的阶段。测试证据存入 `build/artifacts/tests/`，诊断存入 `build/artifacts/diagnostics/`，不得记录凭据或正文。

建议自动验收目标：故障请求期间，取消/关闭命令返回和后续探针消息均在 250ms 内完成。该数值是待下一轮验证的目标，不是现有性能结论；若本机调度噪声影响稳定性，先解释测量误差，不能把上界改为网络超时来掩盖阻塞。

## 3. 候选修复与选择门槛

**优先评估原生 WinHTTP 异步请求**，局限在已有 `AsyncHttpTransport` owner 中，复用现有 callback、HttpRequestOptions 与精确一次终态契约，不新增 HTTP 库。下一轮先用最小原型证明 send/receive/read 和取消的句柄所有权，再确定最终实现。

- 状态必须明确区分“逻辑结果已完成”“回调仍在运行”“请求句柄已关闭”“资源可回收”。终态依旧由一个原子 claim 决定；success/cancel/deadline 竞争只交付一次。
- 关闭后禁止再发起使用该句柄的 API 调用；request 关闭完成之前保留必要的 callback context 和读写缓冲，父句柄最后回收。
- 禁止在 WinHTTP 回调内等待本请求的回调结束，也不能把 WinHttpSetStatusCallback(NULL) 当成已排空回调。见 [Microsoft 回调说明](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsetstatuscallback)。
- deadline、取消和网络失败均丢弃部分 body；保留代理、HTTPS/loopback 边界、禁止重定向、响应上限及凭据清理。

若 UI 仍会在 final-reference 析构时等待，须让现有 owner 持有已取消 operation 至真正安全回收，并先关闭结果投递通道；窗口关闭可以返回，应用退出仍必须等待有界清理完成。不得以 detach、强杀线程、仅删 Join 或把 INFINITE 换成超时后释放正在使用的状态来交付。

“保留同步 WinHTTP，只由工作线程关闭句柄”可作为对照方案；它只有在取消响应与资源回收均有可证明上界时才可采用。短轮询不能中断尚在进行的同步 API，也不能单靠缩短接收超时证明 DNS/代理阶段已安全退出。

## 4. 必须保持的行为

- generation/requestId、重复结果抑制及成功段保护保持原样，旧请求不能更新新窗口或复用后的 HWND。
- 逻辑失效、禁止投递、后台回收、排空 heap-owned WM_APP payload 的顺序明确；关闭主窗口后不能再泄漏 lParam 或引用已析构的页面状态。
- 审查 coordinator、Test connection、Fetch models 和析构全部调用链，避免只修一个按钮。
- `StartTask` 的确定性 fake 仍遵守现有 Cancel/Join 契约，不能把普通 task 当作 WinHTTP HANDLE_CLOSING 路径。
- 重试次数、单批 deadline、配置格式、模型策略、发布版本与本轮引擎合并结果均不顺手改变。

## 5. 验证与结束条件

| 场景 | 必须证明 |
|:---|:---|
| 等响应头、停 body、慢速 body 时取消 | UI 及时返回；最终回调一次；部分 body 不交付 |
| 成功与取消、deadline 与取消竞争 | 无重复回调、重复关闭、死锁；资源最终回收 |
| 关闭后立即再翻译或重新打开设置页 | 新请求正确；旧回调不碰新 owner |
| 请求创建失败、无内存/线程启动失败 | 同步失败契约保留，无遗留 context 或句柄 |
| 主应用退出与 follow-up operation | 退出清理完成，不遗留线程、句柄、缓冲或消息 payload |

实施后运行既有 `test_deepseek_protocol_contract`、`test_translation_contract` 和一次产品增量构建；通过 `build.bat` 的架构与安装布局门禁。最终 diff check 后停止，不扩大到通用任务调度框架。实机 DNS/代理/TLS 证据不足时明确列出未签收项，不能拿 loopback PASS 替代。

回滚只涉及本任务代码及对应测试，不迁移用户数据。独立提交，不与下一个 [长结构化文本边界方案](translation-oversized-structured-leaf-plan.md) 混合实施。
