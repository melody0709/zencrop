# 翻译取消与关闭窗口响应方案（v2）

- **状态**：原生异步实现、自动化回归与产品构建完成；DNS／代理／TLS 及服务商页实机验收待完成。
- **日期**：2026-10-03（用户所在时区）。
- **优先级**：高，先于长结构化文本边界任务。
- **起点**：v3.1.7 加上一轮翻译引擎去重与诊断修正；本任务只调整既有 transport 的网络生命周期。
- **目标**：取消翻译、关闭结果窗、关闭服务商页时，UI 不再等待不可预测的网络退出；保持安全回收、一次终态回调和应用退出时的完整清理。

## 1. 实施前的事实与尚未证明的部分

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

## 6. 实施与自我审查（2026-10-03）

### 6.1 已选实现

`AsyncHttpTransport.cpp` 使用 `WINHTTP_FLAG_ASYNC`。发送、响应头与 body 读取由原生完成通知唤醒既有工作线程；Cancel 和 deadline 只设置停止事件。工作线程独占网络 API 与句柄关闭，消除取消线程关闭同步 API 正在使用的句柄这一竞争。

请求关闭后等待 `HANDLE_CLOSING` 和已进入的原生回调排空，再关闭父句柄、擦除 POST body／headers／读缓冲。读缓冲属于共享 execution state，不能在取消时提前释放；读取字节数在回调互斥锁内复制。异常退出也经局部 RAII 清理。依据 [WinHttpReadData 的异步缓冲要求](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpreaddata) 与前述 CloseHandle 契约。

现有 Cancel／Join、follow-up operation、终态 claim 与 UI owner 顺序保留。`complete` 仍先于用户回调设置，owner 继续用 Join 确认回调退出；未采用 detach、超时后释放状态或第二套后台任务管理器。

### 6.2 自动证据

- 复用 LoopbackHttpServer；为协议与真实窗口测试共用，将原夹具移至 `tests/LoopbackHttpServer.h`。没有新增测试 executable 或 production 测试接口。
- 等响应头、响应头后停 body、持续慢速 body：服务端收到请求后确认阶段，接收超时／deadline 为 30 秒；Cancel＋Join＋最后引用回收均在 250ms 内，一次终态回调，取消结果不带部分 body。
- 三个阶段分别通过真实结果窗的取消命令与 `WM_CLOSE`，随后排队 UI 探针消息。六例均在 250ms 内处理，取消状态／窗口销毁正确，一次终态回调。39.07 秒的最终 `test_translation_contract` 运行中，GetTickCount64 探针记录均为 0ms，属于计时粒度内完成，不表示零耗时。
- 最终 `test_deepseek_protocol_contract` PASS（4.76 秒），包括成功／取消／deadline 竞争、截断 body、响应上限、重定向与 follow-up 回收；`test_translation_contract` PASS（39.07 秒）。产品增量构建及安装布局 PASS（93 个 runtime 文件），架构守卫 PASS。

**基线没有复现确定的冻结**：新增 transport 三阶段测试在原同步实现上也通过。本次交付证明安全的异步所有权和上述场景的响应上界，不宣称测得旧版到新版的性能提升。

### 6.3 自我审查与未签收项

已核对请求 context／读写缓冲寿命、最后原生回调排空、异常清理、一次终态、旧 generation 抑制及 Shutdown 的 heap-owned 消息排空顺序；核对连接测试与模型抓取的 Cancel＋Join 调用链，页面状态仍活到 Join 完成。未发现本轮新增且需阻断交付的代码问题。

DNS、代理／PAC、连接与 TLS，以及真实服务商页关闭／立即重开，尚未做完整实机矩阵；保留 Join 意味着本任务没有承诺所有操作系统／网络环境中的绝对 250ms 上界。不得用 loopback 结果签收这些场景。当前实现与长文本修正分别记录，提交时仍须分开。

## 7. 外部审查后的保护补充（2026-10-03）

保留 `WINHTTP_OPTION_CONTEXT_VALUE`：请求可能在 `WinHttpSendRequest` 前因取消、选项失败或异常关闭。此时 SendRequest 的 dwContext 尚未安装，提前设值保证 `HANDLE_CLOSING` 回调能识别 execution state；对应理由已就近注释。本机直接 WinHTTP 对照确认，发送前关闭且未预设 context 时通知携带 0，预设后携带正确值；当前回调会忽略 0。

`AwaitHttpCompletion` 注明每个 request 同时只允许一个未完成 I/O；当前完成槽不是并发回调队列。没有增加“关闭等待超时后释放状态”的路径，仍保留安全排空契约。

新增既有协议目标中的启动阶段立即取消回归（20 次），以及非法请求头造成 SendRequest 同步失败的回归；全部经过真实 transport，验证一次终态、空部分 body、Join 与最后引用释放。超出 1 秒仍没有终态时测试直接失败终止进程，避免错误分支的析构 Join 无限等待隐藏问题。立即取消覆盖启动竞争，不声称能确定每次恰好落在“注册后、发送前”窗口；该具体顺序仍由源码所有权与 context 契约守住。

最终 `test_deepseek_protocol_contract` PASS（4.78 秒），`test_translation_contract` PASS（43.79 秒），产品构建与架构／布局门禁 PASS。自查未发现需扩大网络生命周期修改的阻断问题；§6.3 的实机未签收项保持。
