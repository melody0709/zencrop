# 浏览器自动选框引发持续性能下降：源码核实与解决路线

> 研究更新：2026-10-12。状态：完成 ZenCrop 源码与上游公开资料核实；本文新增的恢复方法、对照实验和替代实现尚未进行本机性能验收。
>
> 本文承认用户已反复确认的性能回归，不要求重新证明问题存在。目标是解释持续开销，并找出保留网页元素选框能力的可靠路径。

## 1. 结论与证据边界

**当前最有依据的解释是：ZenCrop 的浏览器自动选框触发了 Chromium 的网页无障碍支持；客户端对象释放后，浏览器进程仍保留来自平台 API 的无障碍模式，因此页面继续承担相关维护开销。**

这不是单纯降低选框查询频率就能解决的问题。关闭 ZenCrop 后仍不恢复，已经说明需要同时处理浏览器内部状态，或者让浏览器选框完全绕开无障碍接口。

推荐区分两个目标：

1. **现有会话免重启恢复性能**：优先验证 Chrome 内部的“抑制系统接口自动启用无障碍”开关。它有明确源码依据，但会影响整个浏览器的系统无障碍能力，不能作为 ZenCrop 自动执行的常规清理操作。
2. **日常精确网页元素选框，退出后不留下这类持续开销**：浏览器扩展直接读取 DOM 几何是目前最稳妥的产品路线；推荐官方 Native Messaging 通信，并让网页检测只在选框会话内工作。
3. **不安装扩展，仍需要辅助吸附**：可研究冻结截图上的视觉候选框，但它不能提供真实 DOM 层级。若 DOM 精度是硬要求，视觉方案不能算完整替代。

本次没有找到一种受支持的通用外部 API，可以让 ZenCrop 在使用浏览器 UIA/MSAA 穿透后，精确撤销自己引发的浏览器无障碍模式，同时保留其他辅助工具所需的模式。

### 1.1 已知事实与尚未证明的部分

| 事项 | 证据与状态 |
|---|---|
| 自动选框后浏览器明显变慢，退出 ZenCrop 后仍不恢复 | 用户已反复实测；按已知问题处理 |
| Speedometer 3.1 从 52 降到 36，重启恢复 | 原研究记录的实测值；本次没有重新跑分，也没有补齐原始样本分布 |
| 当前本机 Chrome 为 154.0.8037.98 | 本次只读查询运行中的 chrome.exe 文件版本 |
| 当前本机为 Windows 11 Pro，Build 26200 | 本次只读查询系统版本；不代表原先跑分时的环境 |
| ZenCrop 明确执行浏览器 MSAA 预热、UIA 点查询和后续 MSAA 穿透 | 当前源码已核实 |
| Chrome 154 来自平台 API 的模式会通过进程级持有者累积 | 对应版本源码已核实 |
| 性能下降时确切启用了哪些 AXMode 位、各项开销比例 | 尚未取得运行时模式与性能 trace，不能直接断言一定是完整读屏模式 |
| 内部抑制开关能否在这台机器恢复到原基线 | 有源码依据，尚待定向实测 |
| 扩展的端到端延迟、退出后性能、复杂网页覆盖率 | 尚无实现与实测，不承诺零损耗或固定毫秒数 |

52 → 36 是跑分下降约 30.8%，不是“所有网页的 CPU 成本必然增加 30.8%”。原研究没有提供足以推广到所有网页和设备的证据。

## 2. ZenCrop 当前实现与生命周期

### 2.1 触发链

当前责任点集中在 src/detect/SmartDetector.cpp：

| 入口 | 当前源码位置 | 行为 |
|---|---|---|
| WarmBrowserMsaa | 452 行起 | 浏览器白名单命中后，向窗口发送 WM_GETOBJECT / OBJID_CLIENT |
| TouchUiaPointThroughOverlay | 175–217 行 | 创建 UIA 客户端，在遮罩上临时开孔，执行 ElementFromPoint |
| TouchUiaPointThroughOverlayMta | 220–228 行 | 在临时 MTA 线程执行上述 UIA 查询 |
| EnsureAccessibleRoot | 507–528 行 | 先预热，再用 AccessibleObjectFromWindow / OBJID_WINDOW 获取 MSAA 根 |
| ElementFromPoint | 565 行起 | 最多 31 层 accHitTest，获取命中元素 |
| GetElementRect / GetElementChildCount | 651–673 行 | accLocation 与 get_accChildCount |
| GetRectByPoint | 785–825 行 | 首次命中后可能再探测一次；耗时超过 300ms 后仅降级后续查询 |
| GetParentRect / GetChildRect | 相关滚轮导航路径 | 同样可能进入 EnsureAccessibleRoot，不能只修悬停入口 |

预热缓存属于当前检测器生命周期。它能减少重复唤醒请求，不能撤销已经开启的浏览器状态。300ms 慢查询降级也是事后保护；首次调用的副作用已经发生。

### 2.2 客户端释放基本完整

src/detect/SmartDetectorThread.cpp 中：

- Stop 请求停止并 join 探测线程。
- ThreadProc 的局部 SmartDetector 在 CoUninitialize 前析构。
- SmartDetector::Shutdown 清空当前元素、窗口快照和预热缓存；元素与窗口对象析构释放 IAccessible。
- 临时 MTA 的 std::jthread 在函数结束时 join，其线程执行 CoUninitialize。

OverlayWindow 的关闭路径会调用检测线程 Stop。

因此，从已读源码看，**优先怀疑浏览器内部模式保留，比重新设计客户端析构更有依据**。这不等同于已用 COM/RPC trace 证明全部系统代理资源消失；但用户退出整个 ZenCrop 后仍然变慢，也削弱了“应用仍持有一个对象是唯一根因”的解释。

### 2.3 还有独立的无障碍触发入口

划词翻译的 src/selection/SelectionTextAcquirer.cpp 也使用：

- ElementFromHandle；
- GetFocusedElement；
- ElementFromPoint；
- 后续文本选择读取。

它不属于本次自动选框实现改造，但未来做浏览器无障碍零触发验收时，需要记录是否同时使用了划词翻译。只改 SmartDetector，不能保证 ZenCrop 的其他功能也完全不触发浏览器无障碍。

## 3. 当前 Chromium 机制：应按版本分析

### 3.1 不再是“一次请求必然开启全部读屏支持”

Chrome 官方于 2025-08-14 说明，从 Chrome 138 开始，Windows 原生 UIA 默认启用；此前 UIA 常经由 Windows 的 MSAA 兼容层。因此，旧版 UIA/MSAA 经验不能直接套用到 Chrome 154。[官方 UIA 更新说明](https://developer.chrome.com/blog/windows-uia-support-update)

Chrome 154 的 BrowserAccessibilityStateImpl 按 API 用途区分最小属性、网页属性、行内文本框和扩展属性；接口存在不表示本次选框必然启用了所有模式。[对应版本状态实现](https://github.com/chromium/chromium/blob/154.0.8037.98/content/browser/accessibility/browser_accessibility_state_impl.cc#L571)

重要的是其 EnableAXModeFromPlatform 实现仍把新请求与既有模式做 OR，并保存在进程级 platform_ax_mode_ 中。这个持有者属于浏览器，不属于 ZenCrop 的 COM 引用。[对应版本累积逻辑](https://github.com/chromium/chromium/blob/154.0.8037.98/content/browser/accessibility/browser_accessibility_state_impl.cc#L561)

因此，“客户端已释放”和“浏览器网页无障碍已关闭”是两个不同事实。进程级状态也能解释为什么仅刷新或更换页面未必恢复。

### 3.2 维护网页无障碍树会增加工作，但不能编造执行细节

启用网页无障碍后，浏览器需要维护额外的无障碍数据，处理页面变化并向消费者提供树与几何信息。高频页面更新的基准更容易放大这部分成本。

本次没有 trace 支持以下更强断言，故不采用：

- 每一次 appendChild 都同步生成整棵 AXTree；
- 每个微任务或每帧都必然发送完整树；
- 必然存在固定频率的 UIAutomationCore 心跳；
- 所有网页都会出现 30%–40% 的确定性降幅。

本机持续回归已有用户实测；精确的热点函数、批处理方式和成本比例仍需性能记录定位。

### 3.3 新的 UIA 断连通知确实存在，但 Chrome 尚未用它自动撤销模式

Windows Build 26100 起支持 IUIAutomationClientConnectionCallback::OnDisconnected，供 Provider 接收客户端断连通知。原研究“操作系统没有任何断连通知”的绝对说法已经不成立。[Microsoft 接口说明](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nn-uiautomationcore-iuiautomationclientconnectioncallback)

Chromium 在 2026-05-01 合入 UiaClientInfoSource 封装，可列出客户端并注册连接/断连回调。[官方实现提交](https://chromium.googlesource.com/chromium/src/+/514037ad02be11ddef5b8bb9d1785d6a0058349f)

但 Chrome 154 的 RecordUiaClientConnection 在断连分支仅记录统计，没有关闭网页模式；“已有断连检测”不能解释为“退出检测进程后自动恢复”。[对应版本 Windows 实现](https://github.com/chromium/chromium/blob/154.0.8037.98/content/browser/accessibility/browser_accessibility_state_impl_win.cc#L85)

这里仍不能把 UIA 的通知机制推广成全部 MSAA 客户端的可撤销租约。对混用 UIA/MSAA 的当前选框路径，独立进程退出也没有恢复保证。

### 3.4 旧自动关闭 flag 不是当前可靠解法

当前核实的 Chrome 154 状态实现中，已没有旧版 OnUserInputEvent / OnAccessibilityApiUsage 的自动关闭路径。历史实现确实曾基于跨越一段时间的用户输入和期间没有无障碍 API 使用进行判定。[历史实现](https://chromium.googlesource.com/chromium/src/+/38f224f404aad4f5eff9b24790ef52e8304250be/content/browser/accessibility/browser_accessibility_state_impl.cc)

因此，不再建议用户反复调整 chrome://flags/#enable-auto-disable-accessibility，也不把“DOM 变化让 30 秒静默永远失败”作为当前版本根因：DOM 更新与外部 API 使用不是同一件事。

现代实现另外存在隐藏标签页的延迟关闭策略，受功能开关、是否检测到读屏工具、隐藏页面保留数量等条件影响；重新显示页面还可能重新应用进程级模式。它不能满足“截图结束后，当前可见网页立即恢复”的要求。[当前隐藏/显示处理](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/accessibility/browser_accessibility_state_impl.cc)

## 4. 原研究遗漏的免重启恢复入口

### 4.1 Chrome 自己可以屏蔽来自平台 API 的模式

Chrome 的 chrome://accessibility 包含：

**Suppress automatic accessibility enablement from platform API consumers such as assistive tech.**

这是浏览器内部诊断开关。2025-04-29 的官方提交明确说明：ScopedAccessibilityMode 改造后，普通取消模式勾选不能直接移除外部辅助工具自动增加的模式，需使用这个抑制开关。[官方提交说明](https://chromium.googlesource.com/chromium/src/+/c7d7fd807032908a70d2b29f58d730bbe1e304dc)

Chrome 154 的处理函数调用 SetActivationFromPlatformEnabled(false)，状态层重新计算有效模式并过滤 kFromPlatform 来源。因此它有机会在不重启进程的情况下撤去这次选框触发的有效网页模式。[154 的页面处理函数](https://github.com/chromium/chromium/blob/154.0.8037.98/chrome/browser/ui/webui/accessibility/accessibility_ui.cc#L846)

这里是过滤来源，不是按 ZenCrop 进程撤销一笔独占租约；重新取消抑制后，之前累积的模式可能立即重新生效。

### 4.2 一次定向验证即可裁决

在已有“触发后降速”的浏览器会话中：

1. 退出选框，确保 ZenCrop 没有继续执行浏览器检测；记录其他辅助工具是否运行。
2. 手动打开 chrome://accessibility，先记录当前模式；不要点击 Show/Refresh tree，因为该动作本身会开启额外模式。
3. 勾选上述 Suppress automatic… 开关。
4. 确认网页无障碍模式变化，再在相同环境下复测原基准。
5. 检查结果：若模式关闭且分数回到基线波动范围，说明持续 AX 模式解释得到进一步支持；若模式未关闭，检查是否有其他来源仍强制开启；若模式关闭但性能未恢复，再追查其他浏览器热点。
6. 验证完显式取消抑制并确认所需辅助工具恢复；不要把关闭诊断标签页当作恢复操作。

**本次没有执行这些步骤，不能写成“已经免重启恢复 52 分”。**

页面源码说明，该抑制设置会持续到浏览器重启，并等价于 --disable-platform-accessibility-integration；它会影响浏览器的系统无障碍访问。[154 页面说明](https://github.com/chromium/chromium/blob/154.0.8037.98/chrome/browser/resources/accessibility/accessibility.html#L145)

### 4.3 为什么不把它写成截图结束自动清理

这个设置作用于整个浏览器，可能影响 NVDA、Narrator、语音控制或其他 UIA 工具，并且没有按客户端归属删除模式的能力。外部桌面应用也没有公开的稳定接口去操作该内部开关。

所以，它适合手动诊断和临时恢复；不适合通过模拟点击、注入私有消息或自动修改启动参数变成常规产品行为。普通扩展的 activeTab 也不允许访问 chrome:// 等受限页面。[activeTab 访问边界](https://developer.chrome.com/docs/extensions/develop/concepts/activeTab)

## 5. 替代方案比较

| 路线 | 保留精确网页元素选框 | 退出后消除持续开销的可信度 | 判断 |
|---|---|---|---|
| 改善 COM 释放、取消、缓存或限频 | 可保留当前能力 | 不能撤销浏览器保留的模式 | 可改善选框期成本，不是当前完整修复 |
| 去掉 UIA 预热，保留纯 MSAA | 是否仍有足够精度需实测 | MSAA 仍接入同一浏览器无障碍体系 | 只值得做一次范围明确的对照 |
| 把检测放进短期进程 | 可保留 | 当前 Chrome 不按退出自动清空平台模式 | 不为这次性能问题单独增加进程 |
| 手动使用内部抑制开关 | 开关启用期间当前无障碍穿透会受影响 | 有直接源码依据，尚待本机跑分 | 临时恢复/诊断，不是无缝产品解法 |
| 禁用 renderer 无障碍或 UIA Provider | 当前路径失效或改变 | 前者屏蔽能力；后者不等于关闭所有网页 AX | 不作为默认解决方案 |
| 外部 CDP 的 DOM 查询 | 可查询网页节点几何 | 可绕开 AX，但需要调试接入 | 特殊环境验证工具，不适合日常浏览器要求 |
| 扩展直接读取 DOM 几何 | 普通网页可保留；复杂表面需处理 | 不经 OS 无障碍即可取几何；退出清理可由自身控制 | 推荐产品路线 |
| 冻结截图的视觉候选框 | 仅视觉区域，不是 DOM | 无浏览器无障碍副作用 | 用户接受能力差异时的无扩展备选 |
| 仅窗口/客户区选框 | 不保留 DOM 穿透 | 可以完全避开浏览器 AX | 安全降级，不能称完整解决原需求 |

### 5.1 纯 MSAA：值得验证，不能预先宣称无损

UIA 预热是明显的主动触发点，删除它可能改变启用的模式和成本。但是：

- Chrome 的 MSAA/IA2 仍直接连接 Chromium 无障碍引擎。
- 当前路径不止 accLocation，还包括根对象获取、命中、子节点数量与父级导航。
- 网页内部精确元素如果仍依赖网页 AX 数据，就不能只凭“没有 UIA 调用”判定没有维护开销。

实验应一次区分：当前实现、仅去掉 UIA 预热、完全不查询浏览器无障碍。每组记录精度、有效 AX 模式和退出后的性能。

若仅去掉 UIA 后仍出现持续开销，或只能命中整页，就停止把纯 MSAA 当主路线。不能通过重新加入其他预热来偷偷恢复同一副作用。[Chrome 对 MSAA/UIA 连接关系的说明](https://developer.chrome.com/blog/windows-uia-support-update)

### 5.2 CDP：能绕开 AX，不能替外部 MSAA 清账

精确几何查询应考察 DOM.getNodeForLocation / DOM.getBoxModel 等 DOM 接口，而不是先开启 Accessibility 域，再期待 Accessibility.disable 关闭所有系统无障碍来源。[官方 DOM 协议](https://chromedevtools.github.io/devtools-protocol/tot/DOM/)

外部 CDP 还有明确的日常使用限制：Chrome 136 起，--remote-debugging-port 和 --remote-debugging-pipe 不再接受默认用户数据目录，需配合非默认 --user-data-dir；要求用户为截图改成专用调试浏览器不符合本需求。[2025-03-17 官方变更](https://developer.chrome.com/blog/remote-debugging-port)

扩展也可通过 debugger 权限接入部分 CDP，但会增加调试授权与使用负担；几何读取能用 content script 完成时，优先采用更窄的能力。

### 5.3 视觉方案：整张 4K 图每次重算不是唯一算法

ZenCrop 已在 src/screenshot/OverlayWindowScreenshot.cpp:74 捕获冻结截图，然后才启动探测线程。

因此更合理的探索是：对冻结图一次预处理、只处理浏览器区域或局部区域、缓存候选框，鼠标移动时只查候选并绘制。不能用“整张 4K 图反复做 Sobel/Canny 的耗时”证明所有视觉方案都无法跟手。

但是，无边框按钮、透明容器、内联文本、Canvas 内部控件与真实父子关系都可能难以从像素确定。视觉方案的几何质量与预处理时间尚未验证；**保留精确 DOM 层级的硬要求仍应优先扩展。**

## 6. 推荐扩展方案：会话内 DOM 几何 + Native Messaging

### 6.1 架构与最小责任范围

建议通信方向：

```text
网页 content script：命中、几何、候选父级
        ↕ 扩展消息
扩展 service worker：目标 tab/frame 路由与授权
        ↕ 官方 Native Messaging
本地 host：受限消息与桌面端 IPC
        ↕
ZenCrop 检测侧：会话、坐标、候选框、过期结果过滤
        ↕
现有选框遮罩：绘制与用户操作
```

Native Messaging 是 Chrome 官方支持的扩展与本地程序通道，可限制 allowed_origins 为确定的扩展 ID。content script 不能直接调用它，需经 service worker。推荐这个通道，是因为本任务只需要受限的查询/回复，不需要给所有网页开放一个通用本地服务器。[官方 Native Messaging 文档](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)

本地 host 仍需要安装注册及到 ZenCrop 的通信，并非“零配置”方案。是否复用现有 EXE 的受限 host 模式，或引入独立小 host，应在原型阶段按启动成本、隔离性与安装契约裁决。

当前 src/net/MiniHttpServer.cpp 只是串行 GET 图片服务，使用 Connection: close；没有 WebSocket 握手、帧协议、认证和消息分发。不能把“仓库已有 HTTP 服务”写成“现成 WebSocket 能直接复用”。WebSocket 并非不可用，但现在不是最小完整路线。

### 6.2 权限必须与启动体验一致

两种体验需要明确选择：

- **用户从扩展按钮/扩展快捷键启动**：可采用 activeTab + scripting，按次授予当前页面访问。
- **继续由 ZenCrop 全局热键直接启动**：桌面热键或本地消息本身不会授予 activeTab；需预先取得目标站点访问权并注册相应脚本，或先由用户在浏览器授权。

原方案同时写 activeTab 和匹配所有网页的静态 content_scripts，不能据此声称“仅按次授权、所有页面完全休眠”。广泛匹配脚本有自己的站点访问范围和加载成本。[activeTab 触发条件](https://developer.chrome.com/docs/extensions/develop/concepts/activeTab)、[content script 注入规则](https://developer.chrome.com/docs/extensions/develop/concepts/content-scripts)

第一版不必读取网页正文、密码框值、Cookie 或生成完整 CSS selector。满足选框的最小结果是矩形、候选层级及会话标识；页面数据不能顺便成为功能扩张。

### 6.3 平时被动，选框时有限查询

- 非会话期不遍历 DOM、不轮询几何、不监听全页面变更，也不让每个网页维持 WebSocket 心跳。
- 会话开始后仅向目标 tab/frame 查询 elementFromPoint 和矩形。
- 合并鼠标请求，只保留最新坐标；缓存命中与父级候选，并丢弃旧会话/旧文档回复。
- 固定截图模式下，候选应与截图时刻尽量一致；不能拿后台页面变化后的矩形无限匹配旧截图。
- 结束、取消、页面导航或目标关闭时撤销检测任务、定时器、监听和缓存；本地桥的待机状态与网页检测停止分别验收。
- 失败时回退窗口/手动框选，不得为了“兜底”再自动调用浏览器 UIA/MSAA。

这些是设计要求，还没有实现。IPC、浏览器主线程调度与脏布局都会影响延迟。几何读取可能迫使浏览器提前完成布局，因此不能承诺固定低于 0.5ms/2ms 或绝对零开销。[Chrome 团队对布局查询成本的说明](https://web.dev/articles/avoid-large-complex-layouts-and-layout-thrashing)

### 6.4 精确性边界要进入方案

| 场景 | 实现与验收要求 |
|---|---|
| 普通网页 | 原生命中与元素盒子几何；同矩形父级需要去重 |
| 跨域 iframe | 顶层查询通常只拿到 iframe；需有对应站点权限、frame 路由与坐标映射 |
| Shadow DOM | 不能仅查 document；Chrome 扩展提供 openOrClosedShadowRoot，但需实际递归处理 |
| 内联多行文本 | 一个包围矩形可能包含空白；按需求评估 getClientRects/文本范围，不声称等于实际可见像素 |
| CSS transform / 裁剪 / 滚动 | 处理可见范围、空矩形与截图边界，验证命中与返回框一致 |
| Chrome/Edge 内部页面、浏览器工具栏 | 普通网页脚本没有通用访问能力；明确降级 |
| PDF、Canvas、WebGL | 单独验收；不能承诺网页脚本识别内部全部视觉对象 |
| 双屏、负坐标、DPI 与网页缩放 | 桌面物理坐标、视口 CSS 坐标与页面缩放分别处理 |
| 切换标签、导航、后台窗口 | 校验当前目标及文档身份，拒绝旧回复 |

iframe 脚本需按匹配与权限注入不同 frame；Chrome 的 closed shadow root 访问也不是普通 document.elementFromPoint 一次调用免费完成的。[frame 注入说明](https://developer.chrome.com/docs/extensions/develop/concepts/content-scripts#specify-frames)、[Chrome 扩展 DOM API](https://developer.chrome.com/docs/extensions/reference/api/dom)

原文的“WindowFromPoint 一定直接返回 Chrome_RenderWidgetHostHWND，ScreenToClient / DPR 一定严丝合缝”不能作为契约。遮罩可能成为命中窗口，该类名又属于浏览器内部实现。视口原点可把现存渲染子窗口作为可验证的候选来源，但需校验与回退；不能通过 UIA 取原点，否则会重新引入问题。

多 DPI、网页缩放、侧栏/DevTools、iframe 和 visual viewport 的转换必须实测。不要假设所有偏移与比例都能由一个 DPR 解决。

### 6.5 与现有代码的边界

未来实施应覆盖：

- L1 检测侧的浏览器分流、悬停与父/子导航。
- 现有遮罩的会话开始、取消、结束，以及异步结果应用。
- L1 平台通信与扩展 target 路由；不把浏览器业务塞进图片 HTTP 服务。
- 扩展权限、frame 脚本、Native Messaging host 注册与升级载荷。
- 相关既有契约测试，以及真实浏览器性能/坐标验收。

Window/Host 仍负责生命周期和路由；候选策略与会话状态进入现有 owner 或专用责任单元。不新增跨层依赖，不重新打开架构重构。

**默认窗口降级是未安装扩展时的边界选择，不是“保留 DOM 穿透且问题已经解决”的交付。** 若要暂时保留旧无障碍兼容路径，必须明确说明它仍可能触发持续性能开销；不能无提示回落。

## 7. 验证顺序与停止条件

### 7.1 先裁决恢复机制

只需对已触发的会话验证一次内部抑制开关，记录开关前后模式与性能。不要反复重跑同一个“原实现会降速”的事实。

测试时不能同时使用 Inspect、Accessibility Insights、UIA 自动化或 DevTools 的无障碍树检查来证明“没有开启 AX”，这些工具自身可能触发无障碍。

chrome://accessibility 的 Show/Refresh tree 同样会改变状态。自动化跑分若使用 CDP，只操作普通 DOM/Runtime，不开启 Accessibility 域，并为所有对照使用相同启动条件。

### 7.2 再做一次纯 MSAA 对照

仅在需要判断是否能用极小改动保留现有能力时执行。记录：

- 冷态精确命中与滚轮层级是否保留；
- 有效网页 AX 模式；
- 退出选框、退出 ZenCrop 后是否恢复；
- 是否存在其他辅助工具。

无法同时满足精度和恢复就停止此路线，不继续用更多预热修补。

### 7.3 扩展原型先证明核心结果

在扩大覆盖前，先交付：单个普通网页、正确目标、准确矩形、正常结束/取消、无浏览器 UIA/MSAA 调用，以及退出后的性能回归检查。

核心通过后，再扩至缩放、多屏、iframe、Shadow DOM、页面导航和断连。不能先扩大协议、文本提取或翻译功能。

### 7.4 最终性能与正确性验收

性能比较至少区分：

1. 干净浏览器基线；
2. 扩展已安装但不工作；
3. 扩展选框工作期间；
4. 扩展结束/取消后的同一浏览器会话；
5. 当前无障碍路径的已知回归作为对照。

记录浏览器/系统版本、缩放与 DPI、其他扩展/辅助工具、AX 模式、重复跑分的中位数与离散范围，以及选框请求到呈现的 p50/p95 延迟。

退出后的跑分应回到预先测得的正常波动范围，不要求每次刚好 52。检测阶段延迟与遮罩绘制帧率分别测量，不能把 JS 函数耗时当成完整链路耗时。

几何矩阵需覆盖按钮、输入框、文本、多行内联、嵌套父级、滚动、缩放、跨域 iframe、Shadow DOM、多屏负坐标与混合 DPI；受限页面单独确认降级。

若模式已恢复而性能仍持续下降，就进入浏览器 CPU trace，确认剩余热点，不能把所有异常都归入同一个 AX 解释。

## 8. 本次修正的旧结论

| 原表述 | 本文裁决 |
|---|---|
| Chromium 一次探测必然开启 kComplete | 当前按用途分级；本机具体位仍需运行时证据 |
| Chromium 从来不能关闭，绝对只能重启 | 外部平台模式会保留，但内部抑制入口可以过滤它；恢复分数尚待实测 |
| Windows 没有任何客户端断连通知 | Build 26100 起 UIA 已有断连通知 |
| 客户端被杀后浏览器仍不恢复，因为系统不能知道退出 | 当前 Chrome 已能获知部分 UIA 断连，但对应分支仅记录统计 |
| DOM 变化持续触发心跳，导致旧 30 秒机制死锁 | 没有证据；旧逻辑也不能直接套用到 Chrome 154 |
| 删除 UIA 预热就能安全保留 DOM 穿透 | 纯 MSAA 仍需一次精度/模式/性能对照 |
| 扩展保证零损耗、固定 52 分、低于 2ms | 删除无测量的保证；按待机、工作、退出与尾延迟验收 |
| activeTab 加全网页静态脚本即可按次无感授权 | 权限与启动体验不匹配，必须重新设计 |
| MiniHttpServer 已能提供 WebSocket | 当前仅图片 GET 服务，没有现成 WebSocket 实现 |
| DOM API 100% 覆盖所有网页结构 | iframe、Shadow DOM、受限页、PDF/Canvas 与坐标均有明确边界 |
| 视觉在 4K 必然无法满足跟手 | 冻结图缓存/局部算法仍可评估，但不等价于 DOM 能力 |

## 9. 当前建议与未完成项

**先用浏览器内部开关裁决免重启恢复；完整产品方案优先采用扩展直接取 DOM 几何与 Native Messaging。** 纯 MSAA 是一次有限实验，冻结图视觉是接受精度差异后的备选。独立检测进程、加强 Release、旧 flag 与普通限频不作为完整修复路线。

本文完成的是研究与方案纠正。没有改动生产代码，没有控制或重启用户浏览器，没有安装扩展，没有提交 Git；内部开关恢复、纯 MSAA 对照、扩展性能与实机坐标矩阵均未签收。