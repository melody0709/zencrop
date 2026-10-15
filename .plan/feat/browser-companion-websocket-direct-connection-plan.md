# 浏览器配套扩展 WebSocket 直连方案（Browser Companion 2.0）

- 状态：**已完成代码对照评审的实施交接方案；未实施、未验收**
- 日期：2026-10-14
- 触发：VoxType 已完成同类改造并实机验证（本机参考仓库 `D:\GITHUB_melody0709\VoxType`，提交 `a19193b feat(browser): connect companion extension via authenticated loopback WebSocket (v0.12.0)`）。ZenCrop v3.2.0 的浏览器配套链路与 VoxType 改造前的 1.x 形态**完全同构**：浏览器一开（扩展建立常驻连接），任务管理器就出现第二个 ZenCrop.exe。
- 参照物：VoxType `.plan/feat/browser-websocket-direct-connection-plan.md`（含实施与验收结果，其 §9 记录了已验证/待验收边界）。该仓库与本仓库同作者同机，实现细节可在本机参考仓库内直接核对；其证据文件不复制进本仓库。
- 本文件只做方案，不含实施记录；实施后按惯例另立验收记录。本文中的“必须”是后续实施约束；参考项目的通过记录不代表本项目通过。
- 评审补充：已核对 ZenCrop 的协议读写、Bridge、worker、数据目录、CMake 与守卫实际代码。关键修正见 §10；逐项测试见 §11；实施 AI 的交接与停止条件见 §12。

## 1. 目标与决策

### 1.1 问题

当前扩展用 `chrome.runtime.connectNative("com.zencrop.browser_companion")` 建立常驻连接（`extensions/browser-companion/worker.js:188`）。Chromium 为每条 native 连接**拉起并保活一个独立 host 进程**——即另一个 ZenCrop.exe（host 模式）。该 host 只做 stdio↔命名管道的双向转发（`src/net/BrowserCompanionBridge.cpp:460` `RunNativeHost`），却要付出一个完整进程的代价。

### 1.2 方案对比

| 方案 | 常驻进程 | 收益与代价 | 决策 |
| --- | --- | --- | --- |
| 保持现状（native messaging 转发） | 主程序 + 每浏览器一条连接一个 host | 官方机制，链路已验收 | 迁移前基线 |
| host 拆成独立小工具 exe | 进程数不变 | 名字更清楚，问题没解决 | 否 |
| 任务管理器折叠分组 | 进程数与开销不变 | 仅改善显示，不承诺可分组 | 否 |
| **本机 WebSocket 直连** | **一个常驻 ZenCrop.exe** | 省去 host；新增监听、鉴权、保活与连接管理 | **初步选择** |

"一个常驻进程"指 ZenCrop 自身；扩展仍运行在浏览器内部，浏览器的多进程架构不受影响。配对引导期间会**短暂**出现第二个进程（显式用户动作触发，完成后退出）——这与 VoxType 2.0 的承诺口径一致：**常驻单进程，不承诺生命周期内从不出现第二进程**。

### 1.3 VoxType 实测结论的可信度边界

直接引用其验收数据作为决策依据，但**不把它的数字当成 ZenCrop 的承诺**：

- VoxType 实测：常驻进程 2 → 1；全部 VoxType 进程平均私有提交仅少 ~1.75 MiB（38.0 → 36.3 MiB），工作集观测值含共享页不可加总；Chrome 侧增量完全无法归因于扩展。
- **本方案不承诺固定节省 MB、不承诺零空闲成本、不承诺全场景更快。** 新增成本：主进程监听/每连接 I/O 线程/缓冲区，扩展侧 20s 心跳唤醒。净收益须在 P0/P3 用同机同版本对照实测。

## 2. 当前链路与证据（ZenCrop v3.2.0 基线）

### 2.1 链路

```text
扩展 worker.js (MV3 1.0.4)
        │  chrome.runtime.connectNative() —— 浏览器拉起 host 进程（stdio）
        ▼
ZenCrop.exe（host 模式，RunNativeHost）
        │  命名管道 \\.\pipe\ZenCrop.BrowserCompanion.<SID>.<Session>
        ▼
ZenCrop.exe（主进程，Bridge::Start 管道服务）
```

- host 分发在单实例互斥体之前：`src/main.cpp:855-857`（`IsNativeInvocation` 判定 `args[1]` 以 `chrome-extension://` 开头 → `RunNativeHost`）。
- host 校验：argv origin 必须等于 `kExtensionOrigin`、stdio 必须是管道、必须有浏览器祖先、管道服务端必须同会话且同镜像（`src/net/BrowserCompanionBridge.cpp:460-524`）。
- 主进程侧：管道 DACL 仅当前用户 SID（`CurrentIdentity`，`SDDL D:P(A;;GA;;<sid>)`），客户端必须同会话、同镜像、有 chrome.exe/msedge.exe 祖先（`BrowserParent`，`:223`）；peer 上限 8。
- 管道服务由 `Bridge::Start`（`:159`）在 `main.cpp:873` 启动（DPI 设置之后、CoInitialize/WSAStartup 之前），`WM_DESTROY` 时 `Stop()`（`main.cpp:844`）。

### 2.2 协议与预算（必须原样保留的契约）

`src/net/BrowserCompanionProtocol.h`：

- v1 消息集：`hello`（含 connectionId 与窗口清单/屏幕度量）、`begin` / `inspect` / `end` / `navigate`（下行）、`result` / `unavailable` / `invalidated`（上行）。
- 帧上限 64 KiB、UTF-8、嵌套深度 32（`ParseMessage`）；候选上限 64；扩展 origin `chrome-extension://halonieocgkgbdogndcipmlpklljmoja/`（manifest `key` 钉死，ID 稳定）。
- 查询预算：`Bridge::Query` 总预算 100 ms、写超时 25 ms（`BrowserCompanionBridge.cpp:309/389`）；选区期间 10 s 租约续期（worker `renewal`）；冻结会话的几何失效推送（`invalidated`）双向存在。

### 2.3 消费者接口（不感知传输层，本次不动）

`Bridge::Instance().Query/End/IsValid/IsConnectedBrowser/Start/Stop` 的全部调用方：`src/detect/BrowserDomDetector.cpp`（`:75/135/157/212` 等）、`src/detect/SmartDetectorThread.cpp`（`:40/69`）。改造封闭在 `Bridge::State` + `RunNativeHost` 内部，`BrowserDomDetector` / `SmartDetector` / `SettingsSimplePages` 的既有调用形态不变。

### 2.4 注册与交付

- native manifest：`extensions/browser-companion/com.zencrop.browser_companion.json`（`path: "../ZenCrop.exe"`，stdio，allowed_origins 钉扩展 ID）。
- 安装：`cmake/ZenCropRuntime.cmake:95-114` 把扩展目录、native manifest、`scripts/register_browser_companion.ps1` 装进运行目录；MSI 经 `generate_wix_runtime_fragment.ps1` 从运行目录收获 payload，并精确拥有 HKLM native-host 注册组件。
- 设置页 General：`Register Bridge` 按钮 + 注册状态（`src/ocr/ui/SettingsSimplePages.cpp:74-95/612`；控件 ID `IDC_GEN_COMPANION_*`，`src/core/ResourceIds.h:43-50`，模板 `src/resources.rc:26-30`）。
- 测试：`tests/browser-companion/worker_contract.cjs`（node VM 契约）+ `tests/support/browser_companion_contracts.cpp`（编入既有目标 `test_screenshot_editor_state_contract`，`tests/CMakeLists.txt:652-665`，链接 `zencrop_platform`）。

## 3. VoxType 已验证、可直接复用的实现要点

以下参考提交 `a19193b`（v0.12.0）及其验收记录。复用设计和已修复的生命周期约束，不做逐字移植；ZenCrop 自己的通过证据必须重新取得：

1. **服务端选型**：Windows `websocket.dll` 协议组件（Win8+ 自带，`WebSocketCreateServerHandle` / `BeginServerHandshake` / `GetAction` / `CompleteAction`）负责握手、掩码、UTF-8、分片与控制帧——**零第三方依赖，不自写协议栈**。每 Channel 一个事件驱动 I/O 线程（WSAEventSelect FD_READ|FD_CLOSE）；应用层发送只入队（非阻塞）。
2. **监听纪律**：仅绑定 `127.0.0.1`；`SO_EXCLUSIVEADDRUSE` 独占端口；固定端口 + 固定路径 + 固定子协议；端口冲突直接 `port_unavailable` fail closed，**不扫描、不抢占**。VoxType 用 19226；ZenCrop 必须错开（本方案定 19227，见 §4.1）。
3. **握手校验**：请求行精确匹配路径、`Host == 127.0.0.1:<port>`、`Origin` 等于钉死的扩展 origin（**HTTP Origin 不带末尾 `/`，不能直接复用 native argv/manifest 字符串**）、子协议匹配、`Upgrade: websocket`、`Sec-WebSocket-Version: 13`、拒绝 `Content-Length`/`Transfer-Encoding`、重复 header 拒绝、header 数与请求字节数上限、2 s 握手预算。**Origin 不是认证**，本机程序可伪造握手——必须叠加凭据。
4. **对端身份**：`GetExtendedTcpTable(TCP_TABLE_OWNER_PID_ALL)` 反查 loopback TCP 对端 PID（扩展的 TCP 由 Chromium 网络服务进程持有，非扩展进程）；`BrowserRootPid` 沿**同镜像**祖先链上溯到浏览器根进程，逐级核对同用户/同会话与父子创建时间（防 PID 复用），并对根进程持 `SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION` 句柄防复用；选路时校验句柄未退出（`WaitForSingleObject(...,0)`）。
5. **认证**：每浏览器配置 256-bit 凭据 + 双向 HMAC-SHA256 随机挑战（`challenge(instance, serverNonce)` → `auth_start(profile, clientNonce)` → `auth_server` proof(`server:instance:clientNonce:serverNonce`) → `auth_client` proof(`client:...`) → `auth_ok`）。secret **不通过 WebSocket 传输**，仅在受控 bootstrap 通道发放；认证前不发窗口清单/正文；认证总预算 3 s。
6. **凭据存储**：`CryptProtectData`（DPAPI，`CRYPTPROTECT_UI_FORBIDDEN`）+ owner-only ACL + `MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH` 原子替换；文件名含安装路径哈希区分实例；损坏 fail closed（不静默覆盖），显式 RevokeAll 旋转身份并断开存量连接。
7. **配对引导**：native messaging 降级为**显式、有界、一次性**的引导——扩展连接页按钮触发 `connectNative`，首帧 `{type:"pair", ext, profile}`，host 转发到主进程管道，主进程回 `{type:"paired", wsVersion, port, instance, profile, secret}` 后 host 退出。预算：host 等就绪 1.5 s、等回复 3 s、扩展侧总超时 7 s；启动/重连/alarm **永不**拉起 host。1.x 常驻转发路径完整保留作回滚。
8. **扩展端**：`ws://127.0.0.1:<port>/<path>` + 子协议；`chrome.storage.local` 设 `TRUSTED_CONTEXTS`（content script 全站注入的 ZenCrop 更需要这道隔离）；20 s ping / 60 s pong 超时（同时是 MV3 SW 生命周期延长手段，**Chrome 116+**，manifest 钉 `minimum_chrome_version: "116"`）；断线 250 ms 快速重试一次再 0.5→5 min 退避；状态徽章 ON/PAIR/ERR/WAIT；action 变为 `default_popup` 连接页。
9. **有界性**：消息 64 KiB；Channel 收发队列各 ≤ 8；认证帧 ≤ 1 KiB；并发 peer ≤ 8；配对条目 ≤ 32；服务端 65 s 无读入断开。
10. **守卫**：其 `check_architecture.ps1` 新增一条"browser WebSocket authenticated and bounded"规则，用针线字符串断言（loopback/独占端口/origin/PID 上溯/互认证/DPAPI/有界队列/`connectNative` 恰好 1 处调用点/双 accept loop 在发布锁内启动 peer 线程）——规则必须能失败，与命中自检同步。

## 4. 目标架构（ZenCrop 2.0）

```text
扩展 worker.js (MV3 2.0.0, minimum_chrome_version 116)
        │  ws://127.0.0.1:19227/zencrop/browser/v2  (subprotocol zencrop-companion-v2)
        │  先双向 HMAC-SHA256 认证；认证后承载既有 v1 消息（hello/begin/inspect/end/navigate/result/unavailable/invalidated）
        ▼
ZenCrop.exe（唯一常驻主进程）
  ├─ WebSocket 服务（websocket.dll；仅 127.0.0.1:19227；SO_EXCLUSIVEADDRUSE）
  ├─ 配对引导：连接页显式触发 → 短暂 host（复用既有 native manifest 与命名管道）→ 发凭据 → 退出
  └─ 1.x 管道转发路径完整保留（回滚 + 旧扩展兼容）
```

### 4.1 常量

| 项 | 值 | 说明 |
| --- | --- | --- |
| 端口 | `19227` | 与 VoxType 的 19226 错开，两应用同机共存；独占绑定，冲突 = fail closed 并在设置页可见 |
| 路径 | `/zencrop/browser/v2` | 握手请求行精确匹配 |
| 子协议 | `zencrop-companion-v2` | 握手协商 |
| 认证/心跳消息 | `version: 2`（`challenge/auth_start/auth_server/auth_client/auth_ok/ping/pong`） | 与 v1 载荷显式分离 |
| 载荷消息 | `version: 1`，**逐字段不变** | hello/connectionId/sessionId/几何/候选全部复用 |

### 4.2 与 VoxType 的结构差异（ZenCrop 特有决策）

| # | 差异 | 决策 |
| --- | --- | --- |
| D1 | 分层与依赖 | `BrowserCompanionWebSocket.h/.cpp` → `src/net/`（L1，进 `zencrop_platform`，已链 `ws2_32`，补 `websocket`、`iphlpapi`）；`BrowserCompanionPairing.h/.cpp` → `src/core/`（L0，进 `zencrop_core`，只用 SDK/STL/既有 JSON）。core **已经**链接 `bcrypt`、`crypt32`、`shell32`、`advapi32`，不得重复追加。Registration 在 core、Protocol/Bridge 在 net；不新增目录，不引入依赖环 |
| D2 | JSON 与分帧不同 | 一律复用 nlohmann/json 并做类型/范围校验，**不照搬 VoxType 的私有 JSON 提取器**。既有 `ReadMessage`/`WriteMessage` 内部调用 v1 `ParseMessage`，会拒绝 `pair/paired` 及 v2 控制帧；须先按 §10.1 分离有界字节分帧与 v1 语义校验。v1 消息集、字段和 `ParseMessage` 接受/拒绝行为保持不变 |
| D3 | `Bridge::Start` 与传输接线 | 管道接收连接后，在受上限约束的 peer/引导线程读取并分类首帧；`pair` → `AnswerPairing` 后退出，`hello` → 既有 v1 路径。不得在 accept 主循环同步等待首帧而阻塞其它配对。WS：Accept → 身份核验 → 受上限约束的 peer 线程 Upgrade/Authenticate/PeerLoop。Query、End、断开、失效通知所有读写点统一适配；入队不等于已送达，须满足 §10.4 的超时/顺序/断连约束 |
| D4 | Winsock 初始化与析构 | Bridge 启动早于 main 的 WSAStartup。传输 owner 自持一次成功 WSAStartup，所有 listener/channel/I/O 线程排空后才执行匹配的 WSACleanup；Start/Stop 可重复且无引用泄漏。不得靠函数 static 析构顺序，亦不得依赖 main 提前初始化。头文件遵守本仓不透明 Winsock 类型边界，已有 PCH 顺序不动 |
| D5 | `IsConnectedBrowser(pid)` 语义 | 现值 = host 的浏览器祖先（浏览器主进程）。WS 下对端 TCP 属主是 Chromium 网络服务进程，`BrowserRootPid` 同镜像上溯到根 = 同一浏览器主进程（窗口 HWND 属主）。**语义不变**，`BrowserDomDetector::IsBrowserWindow` 无感 |
| D6 | 数据目录 | 配对存储由**主进程唯一拥有**：`ZenCropAppDataFilePath` + 规范化 exe 路径哈希文件名（`browser-websocket-pairing-<sha256(exe 路径)[:16]>.dat`）。host 只转发，不按自己的浏览器环境另开 store；主进程的 `ZENCROP_DATA_DIR`/portable 决策是权威。目录失败不得回退当前目录。**不进 Settings 六段持久化** |
| D7 | 设置页 | General 页保留 Register Bridge（配对引导依赖 native 注册），新增：配对状态（每浏览器 paired/connected/offline）+ `Unpair all`（旋转身份并断开）。控件 ID 延续 `IDC_GEN_COMPANION_*` 序号，字符串进 `src/core/Strings.*`，模板 `src/resources.rc`；沿用既有 DPI 布局机制，不做独立布局体系 |
| D8 | 扩展行为差异 | action 变 default_popup（状态 + Pair 按钮），移除 onClicked；加 storage 权限与 minimum_chrome_version 116。几何算法、候选顺序、租约和失效业务语义保持；允许且必须调整 hello/query/end/invalidate 的**连接代际与异步生命周期**，禁止以“业务零改动”为由让旧查询回复进入新连接。详见 §10.5 |
| D9 | 测试目标 | **复用既有目标**：WebSocket 契约（真实 TCP：正确/错误/反射/重放凭据、Origin/子协议/重复 header、掩码/分片/超量/畸形/深度/UTF-8、端口冲突、DPAPI 重启/损坏/撤销、退出取消）进 `tests/support/browser_companion_contracts.cpp` 所在的 `test_screenshot_editor_state_contract`；worker 契约扩展进 `tests/browser-companion/worker_contract.cjs`（认证前不读页面、冒充服务、显式配对、TRUSTED_CONTEXTS、心跳、纯 WS 重连、`connectNative` 恰好 1 处调用点）。不新建 test executable，不列 `src/**/*.cpp` |
| D10 | 版本与交付 | 扩展 1.0.4 → 2.0.0；程序拟 v3.3.0，实施前重读当前版本，避免覆盖后续版本。程序版本唯一权威为 CMake project(VERSION)，资源/MSI 随之派生。ZenCropRuntime.cmake 增装 connection.html/js；WiX 收获运行目录。保留本仓 MSI HKLM 与 GUI/脚本 HKCU 的已有所有权，**不得复制 VoxType 的 MSI 不注册策略**。注册脚本无需因换传输而改 |
| D11 | 守卫 | `scripts/check_architecture.ps1` 新增一条规则（针线断言照 §3.10 清单适配到 ZenCrop 文件路径），规则清单与命中自检同步 15 → 16，基线不抬高 |

### 4.3 认证状态机（复用已验证流程；常量与 JSON 边界按本仓适配）

```text
服务端                                      扩展（已配对，storage TRUSTED_CONTEXTS）
  │ ◄── WS 握手（Origin=chrome-extension://halonie…，无末尾斜杠）──────── │
  ├─ TcpPeerPid → BrowserRootPid → 持浏览器根进程句柄
  ├─ challenge {v2, instance, serverNonce} ────────────────────────────► │
  │ ◄──────────── auth_start {v2, profile, clientNonce} ──────────────── ├─ 校验 instance
  ├─ 查凭据(profile, browserImage) → HMAC("server:"+transcript) ───────► │ auth_server proof
  │ ◄──────────── auth_client proof = HMAC("client:"+transcript) ──────── ├─ crypto.subtle.verify 服务端证明
  ├─ 常数时间比对 → auth_ok ──────────────────────────────────────────► │ state=ready → 发 v1 hello
  └─ PeerLoop：v2 ping→pong；v1 消息走 ParseMessage 与现有分派
```

配对引导走既有管道（`pair` 首帧分支），凭据只在**配对那一刻**经本机 stdio+SID 管道发放一次；此后 secret 不通过 WebSocket 发出。配对失败可能需要用户再次 Pair，不把“发放一次”误解为禁止重新配对。

## 5. 实施范围（文件级）

| 范围 | 拟调整 |
| --- | --- |
| `src/net/BrowserCompanionWebSocket.h/.cpp`（新增） | Listener/Channel：websocket.dll 封装、loopback 独占监听、握手校验、每连接 I/O 线程、有界收发队列、`TcpPeerPid` |
| `src/core/BrowserCompanionPairing.h/.cpp`（新增） | 凭据存储：随机数/HMAC/常数时间比对、DPAPI+ACL+原子替换、实例 ID、Pair/Find/RevokeAll、损坏 fail closed |
| `src/net/BrowserCompanionProtocol.h/.cpp` | 最小拆分字节分帧与 v1 JSON 校验；ReadMessage/WriteMessage 保持原语义；新增的底层入口只服务 bootstrap 与既有封装，不建第二套 v1 协议 |
| `src/net/BrowserCompanionBridge.cpp` | Start 双 accept loop；管道连接的有界处理线程分类首帧/AnswerPairing；peer 增身份状态；Query/End/退出发送分派；Stop 双路取消排空；BrowserRootPid |
| `src/net/BrowserCompanionBridge.cpp` `RunNativeHost` | 首帧 `pair` → 有界配对引导分支（复用管道，1.5 s/3 s 预算）；`hello` 分支 = 既有 1.x 转发（原样保留） |
| `src/net/BrowserCompanionBridge.h` | 保留原消费者签名；仅增 UI 所需只读 Status 与 RevokeAll（具名状态/错误），不得将 State/Peer 暴露给设置页 |
| `src/main.cpp` | **不改**（`IsNativeInvocation`/`RunNativeHost` 分发与 `Bridge::Start/Stop` 接线原位） |
| `extensions/browser-companion/worker.js` | 传输/认证/心跳/配对；给 v1 业务加连接代际与迟到任务隔离，不改变几何算法和协议字段 |
| `extensions/browser-companion/connection.html/js`（新增） | 配对/状态弹窗页 |
| `extensions/browser-companion/manifest.json` | 2.0.0；`minimum_chrome_version: "116"`；`storage` 权限；action → default_popup |
| `src/ocr/ui/SettingsSimplePages.cpp` / `src/core/ResourceIds.h` / `src/resources.rc` / `src/core/Strings.*` | General 页配对状态 + Unpair all（沿用现有行式布局与 DPI 机制） |
| `tests/support/browser_companion_contracts.cpp`、可选同目录专用契约 TU；`tests/CMakeLists.txt` | 新增真实 TCP/配对/生命周期契约，**明确调用到新增测试函数**；只加入既有测试目标，链接既有库，不把产品 cpp 再编一次 |
| `tests/browser-companion/worker_contract.cjs` | 扩展 2.0 客户端契约 |
| `scripts/check_architecture.ps1` | 新增规则 + 命中自检（15 → 16） |
| `cmake/ZenCropRuntime.cmake` / `CMakeLists.txt` | 新源文件入目标；补链 `websocket`/`iphlpapi`（platform）、`bcrypt`/`crypt32`（core）；扩展新文件入安装清单 |
| `doc/CHANGELOG.md` / `AGENTS.md` / `extensions/browser-companion/README.md` / `doc/browser-companion-validation.md` | 版本段、直连流程、旧版兼容边界与回滚步骤、验收记录（属稳定架构契约变化，按仓库规则更新） |

不改：v1 常量、`ParseMessage` 接受/拒绝语义、`BrowserDomDetector.*`、`SmartDetector*`、native manifest、注册脚本/Registration 所有权语义。必须保持浏览器根层 UIA/MSAA 隔离，断连/超时/认证失败一律走现有安全回退，不重新探测浏览器无障碍。只补分帧 API 不构成扩大 v1 消息集。

## 6. 分阶段实施与退出条件

### P0：冻结基线

记录两个仓库的 commit、工作区状态、程序/扩展/浏览器/OS 版本；保留用户修改并备份拟改文件。实测可用浏览器的进程数、私有提交、空闲 CPU、查询时延；未运行的 Edge 记待验收。跑绿既有 worker/原生契约与架构守卫（基线当前 15 条，实施时从脚本读取，不凭本文数字提高任何棘轮）。先完成 §10.1 的控制帧边界及 §11 对应失败用例，才移植服务端。

退出条件：有可复现对照步骤与数据；1.x 路径回归全绿；协议首帧/Origin/锁序/总预算/测试调用点已核对，不留给实施 AI 猜测。

### P1：最小直连原型

主进程内 WebSocket 服务 + 端口 + 认证 + 既有查询格式跑通（v1 载荷不经改动直接过线）；配对引导可用；Chrome 实机：配对后 OS 只见一个 ZenCrop.exe，扩展重载直连恢复，主程序重启后凭据仍有效；错误凭据/网页/伪造握手不能取得或提交任何窗口信息。

退出条件：稳定运行仅一个常驻进程；身份绑定（BrowserRootPid + 几何匹配）证明成立；断线不阻塞选框/UI；100 ms 查询预算实测不回退。

### P2：产品化与兼容迁移

设置页配对状态/Unpair all、扩展连接页与状态徽章；覆盖：程序先开/浏览器先开、双方重启、扩展重载、Chrome+Edge 并存、丢失凭据（显式 Unpair 与损坏存储两种）、目录移动（须重新 Register + 配对）；版本偏斜矩阵：旧扩展 1.0.4 + 新程序 = 仍走 1.x 管道（兼容）；新扩展 2.0 + 旧程序 = 配对有界失败并显示错误，**不静默降级**。

退出条件：状态区分"已配对/已连接/未连接"；上述矩阵行为可复现；守卫 16/16 与既有门禁全绿。

### P3：切换、清理与验收（清理项默认延期）

日常路径切换为直连后，1.x 常驻转发路径、旧资产与注册的**删除**延期（与 VoxType 同口径：保留兼容与回滚，旧扩展仍可能产生两个进程）。正式删除需另行方案。

退出条件：自动化项及声明完成的实机项均有记录；MSI 布局校验、升级/回滚步骤明确。未执行的 Edge/多 profile/真实多屏/VM 等逐项记“待验收”，**不得将 P3 或发布验收整体标为通过**。保留兼容路径不等于已证明整包降级成功。

## 7. 验收矩阵

- **进程**：仅所有参测 profile 都升级 2.0 且连到同一主进程时，单浏览器/Chrome+Edge 场景才要求一个常驻 ZenCrop.exe；旧扩展产生 host 是兼容预期，须分开计数。配对临时进程完成/失败都须有界退出。
- **资源**：同机同版本同场景多次对照：全部 ZenCrop 进程私有提交/工作集、浏览器侧增量、空闲 CPU 与 20 s 心跳唤醒次数；报告净收益，不承诺固定节省 MB。
- **功能**（选框全回归，对照 `doc/browser-companion-validation.md` 既有矩阵）：普通字段、授权 iframe 链、open/closed shadow root、冻结会话失效（导航/滚动/缩放/遮挡）、滚轮逐级、整页框、同位置多窗口前台消歧、25%–500% 页面缩放、多屏混合 DPI 显示器原点换算；`invalidated` 推送与 10 s 租约不受传输更换影响。
- **安全**：错误 Origin/子协议/凭据、反射/重放、认证前业务帧、畸形/超量/错误 PID/未认证 flood 均拒绝且资源有界；秘密只出现在受控 bootstrap 管道、受限 storage、进程内存与 DPAPI 密文文件，**不进 URL、日志、DOM、状态回复、测试证据**。
- **生命周期**：取消信号须立即传播；250 ms 是受控本机夹具的网络等待排空验收阈值，**不是所有磁盘/DPAPI/浏览器环境的无条件退出保证**。半握手/半认证/排队发送也能取消，不能等待各自 2 s/3 s 自然超时。测量 Stop 本身及主程序退出分别计时。
- **交付**：`build.bat` 全绿（含守卫 16/16）；`cmd.exe /d /c tests\build_and_run.bat test_screenshot_editor_state_contract` 与 node worker 契约通过；扩展 2.0 开发目录加载与运行目录安装一致；MSI payload/版本/布局校验通过。
- **边界外的实机项**（单列，不混入自动化门禁）：Edge、多 profile、真实多屏/负坐标、VM 升级矩阵——参照 VoxType 的延期口径，由用户逐项签收。

## 8. 风险、边界与不承诺项

1. **净资源收益未证明**：VoxType 同型实测私有提交差 ~1.75 MiB 量级；本方案只承诺进程数减少与 ZenCrop 侧观测，浏览器净收益、唤醒次数须实测。
2. **新增暴露面**：一个本机 TCP 监听端口。缓解：仅 loopback、独占绑定、握手逐字段校验、双向 HMAC、认证前零泄露、全程有界。同 Windows 用户的恶意软件本就能伪造配对引导或直接调管道——威胁模型与现状同级（README 已声明"非完整认证"），本方案把"能伪造握手"这一新增面补上了凭据证明。
3. **SW 生命周期换机制**：常开 native 端口保活 → 20 s 消息保活（Chrome 116+）。低于 116 的浏览器被 manifest 拒装（有意边界）；心跳唤醒是披露的空闲成本。选区期间既有 10 s 租约续期兼作保活流量。
4. **端口被占**：fail closed + 可见状态；不扫描、不抢占、不共享。19226/19227 仅避免两个产品彼此冲突，**不保证第三方不占用**。TCP 端口不按 SID/会话隔离：多 Windows 用户同时登录、RDP、两个 portable 实例并存时只有一个能占用；其余明确报告 port_unavailable。本轮不扩展多实例端口发现。
5. **版本偏斜**：见 P2 矩阵；不做静默降级。
6. **测试夹具纪律**：真实 TCP 契约占端口——测试须使用独立端口或串行执行，遵循本仓"夹具按 PID 隔离/共享句柄"的既有规则，不引入并发独占冲突。
7. **MSI/发布**：版本提升与 Major Upgrade 走既有契约；UpgradeCode/UUID 契约不动；不在用户工作站隐式安装。

## 9. 未决项（P0/P1 取证后再定，不在本文冒充已解决）

- 19227 端口在目标环境族的占用情况抽查（仅记录，不改发现策略）。
- Edge 的网络服务进程祖先链实际核对；WebView2/Electron 不因此获得配套扩展能力，保留本仓浏览器分类边界。
- 保活参数（20 s/60 s/65 s）与 8 peer/32 配对上限在 ZenCrop 选框时延预算下的实测微调。
- Pairing 默认放 core，传输放 net；仅在实际链接/分层门禁证明有冲突时调整，并写明证据，不为照搬参考项目重排架构。
- 连接页文案与设置页新增控件的具体布局行（实施时按 `SettingsPageLayout` 既有行式追加）。

## 10. 实施硬约束（本轮评审补齐，交接 AI 不得跳过）

### 10.1 先解决协议入口，而非直接复制服务端

**已核对的阻断点**：`BrowserCompanionProtocol.cpp` 的 ReadMessage 返回 ParseMessage(bytes)，WriteMessage 也先用 ParseMessage 检查；后者仅接受 v1 既定类型。现有 RunNativeHost 首帧亦要求 hello。直接插入 pair 分支永远到不了，paired 响应也发不出去。

最小拆分顺序：

1. 从现有 ReadMessage/WriteMessage 中提取有界 **ReadFrameBytes/WriteFrameBytes（示意名）**，共享既有 Transfer，不复制第二套 Windows I/O。它们只处理 4 字节小端长度 + UTF-8 JSON 字节、大小与总 deadline；不能把任意二进制当有效控制消息。
2. 保留 ReadMessage = 读字节后 ParseMessage，WriteMessage = 校验 v1 后写字节。旧调用者与 v1 测试结果不变；不向 ParseMessage 的 types 添加 pair、paired、ping 等。
3. host/管道首帧经有界 JSON 解析分类：hello 再走原 ParseMessage；pair 走独立 bootstrap schema。首帧传输上限仍 64 KiB（旧 hello 可能较大）；分类为 bootstrap 后要求整帧 ≤ 1 KiB。不能把新控制帧的 1 KiB 限制套到所有旧 hello 上。
4. WS 收到的文本先完成 UTF-8/大小/深度校验，按状态与 version 分流；v2 控制消息走严格 schema，v1 业务走原 ParseMessage。不手写引号/字段搜索器，不用默认值掩盖类型错误；整数、布尔、hex 字符串须校验真实类型及范围。控制消息重复键拒绝；JSON 异常必须在连接边界收口，不能穿出线程入口导致 terminate。
5. framing、握手、认证的 timeout 都是**整个阶段共用的绝对截止时间**，头部/正文或两轮 proof 不能各获一份完整预算；循环内即使不断读到字节也要检查 deadline、取消和最大字节数，避免慢滴/满速输入逃逸预算。

### 10.2 常量、身份与状态判定

| 用途 | 精确值/要求 |
| --- | --- |
| 扩展 ID | `halonieocgkgbdogndcipmlpklljmoja`，由现 manifest key 派生并在契约中核对 |
| Native argv / manifest allowed_origins | `chrome-extension://halonieocgkgbdogndcipmlpklljmoja/`，保留现末尾 `/` |
| HTTP WebSocket Origin | `chrome-extension://halonieocgkgbdogndcipmlpklljmoja`，**无末尾 `/`**；不得用 starts_with 放宽 |
| native host 名 | `com.zencrop.browser_companion`；VoxType 的名称、ID、端口、存储 key 不得残留 |
| profile / instance | 16 随机字节的小写 hex（32 字符）；profile 每浏览器 profile 持久化，不能用临时 connectionId 代替 |
| secret / nonce / proof | 分别 32 字节 key、32 字节随机挑战、32 字节 HMAC，hex 均 64 字符；CNG RNG，常数时间比较 |
| proof 转录 | 精确 UTF-8 `server:<instance>:<clientNonce>:<serverNonce>` 与 `client:...`；双方钉住编码、分隔符与角色，不对 JSON 原文直接签名 |
| 身份绑定 | TCP 内核属主 → 浏览器根进程（完整镜像路径、SID、session、创建时间）→ 对端 profile 凭据 → hello 窗口几何 → 原有 connection/session/request/document 身份链 |

WS 接收前将原始 socket 的 PID 核对失败视为拒绝；不信任客户端自报 PID。认证完成前，peer 不可被 Query/IsConnectedBrowser 选中；认证完成但尚无有效 hello 也不算业务就绪。浏览器进程句柄退出、撤销、断连、hello 不合法均同步使其不可用。

保留当前 Chrome/Edge 支持范围及 Firefox 的浏览器无障碍隔离语义；不能因扩展不支持就重新走浏览器 UIA/MSAA。TCP 归属核验无法防御同用户管理员/注入/调试器，也不能证明浏览器二进制未被篡改；本方案不作该承诺。

控制消息的线格式固定如下；下面列出的 version/type 是真实字段，不发送状态图中的 v2/serverNonce 说明性别名。字段均须通过类型与长度检查；未知控制类型/状态顺序错误直接拒绝。

| 通道/方向 | JSON 必需字段 |
| --- | --- |
| bootstrap 扩展→host→桌面 | `version:1, type:"pair", ext:<扩展ID>, profile:<hex16>` |
| bootstrap 桌面→host→扩展 | `version:1, type:"paired", wsVersion:2, port:19227, instance:<hex16>, profile:<原请求profile>, secret:<hex32>` |
| bootstrap 失败（能够回复时） | `version:1, type:"pairing_error", reason:<脱敏错误码>`；旧桌面直接关管道亦须被扩展有界识别 |
| WS 服务端→扩展 | `version:2, type:"challenge", instance:<hex16>, nonce:<hex32>` |
| WS 扩展→服务端 | `version:2, type:"auth_start", profile:<hex16>, nonce:<hex32>` |
| WS 双向证明 | 分别 `version:2, type:"auth_server", proof:<hex32>` 与 `version:2, type:"auth_client", proof:<hex32>` |
| WS 确认/心跳 | `version:2, type:"auth_ok"`；ready 后 `version:2, type:"ping"/"pong"` |

hex16/hex32 表示解码后 16/32 字节；JSON 均为字符串。bootstrap 回包的 port 必须等于产品常量，不接受任意端口、主机、路径重定向；连接 URL 固定 loopback，不能把配对回包当不受约束的连接指令。v1 业务仍使用原 connectionId/sessionId/requestId，不能把 profile 填进去替代。

### 10.3 线程、取消、队列与资源所有权

- Channel I/O 线程唯一操作 websocket.dll 协议句柄；其它线程只入队或发取消信号，析构必须等 I/O 退出后才能删除协议句柄及其缓冲区。自动 pong 等组件动作也须按 SDK 完成，不能提前清掉仍在发送的用户缓冲。
- **关闭谓词与接收等待使用同一 mutex**：持锁修改 closed，再通知 cv。atomic 本身不能防止“判定 false → notify → 开始等待”之间丢唤醒。关闭前/期间/之后开始 Receive 都须及时返回。
- **双 accept loop 发布纪律**：在同一个 state mutex 内完成 peer 加入共享集合与 thread 初始化；其它清理线程在释放此锁前不能看到未初始化的 thread。兼容管道入口也必须修；peer 线程允许构造后立即失败。
- 锁序为 state → peer；不要在持有 state/peer/凭据快照锁时做网络 I/O、等待线程、关闭会 join 的 Channel 或写磁盘。Store 写操作由单一写 owner 串行化，查找/状态读取只访问短锁内的内存快照，不被 DPAPI/落盘阻塞。持久化成功后短临界区发布新 revision，再进入 bridge 的状态提交/断开步骤；不为此引入通用事务框架。
- Stop 先禁止新发布、发 stop 信号，取消 accept 与所有已接受的握手/认证/业务等待，再 join。不能先等 accept join，而 accept 正在 join 一个尚未被取消的 peer。半握手 socket、bootstrap 首帧和 legacy idle read 都须登记并可取消。
- 对 overlapped I/O，取消后必须取回完成结果再释放 OVERLAPPED/event；对同步 stdio 的 bootstrap deadline，必须有真实取消/排空机制，不能仅给同步 ReadFile 传一个实际不起效的超时数字。
- 最多 8 个已接受活动连接，**未认证、半握手、bootstrap 也计入资源上限**；另有有限 listen backlog。不要在身份失败/超限时先创建无限线程。失败 peer 的回收不能只等下一条成功连接；反复失败后线程/句柄数应回落至基线。
- 收发队列各最多 8 条，每条 ≤ 64 KiB；溢出显式失败/断连，不静默丢 result、invalidated、end。协议控制消息与业务均受总量限制。慢客户端不拖住 UI；不能一帧一帧复制无限待处理 JSON。
- 网络握手 2 s，服务端认证**总计 3 s**，扩展认证 5 s，bootstrap 整体 7 s；各阶段都可被取消。65 s idle 判定以实际收到的有效流量为依据；心跳周期 20 s 不是精确调度保证，睡眠后允许断开并恢复。
- 自持 Winsock 引用属于传输 owner：所有 channel 关闭、线程排空后才能 cleanup。Start 重复、Start 失败、Stop 重复、销毁未启动对象不得泄漏或过度 cleanup；不要把这些行为交给未知 static 析构顺序。

### 10.4 ZenCrop 特有的 100 ms 查询与几何会话

`Send()` 从“25 ms 内完成管道写入”变为“返回已入队”，不自动保证 Query 预算更稳；排队中的旧 inspect 仍可能在调用方超时之后抵达扩展。必须明确：

1. Query 在入口建立现有 100 ms 单一 deadline，锁等待、序列化、入队与回包等待都计入，不能每层再加 100 ms。仅使用剩余预算，不在 UI/检测线程等待握手、认证、配对或网络重连。
2. 同一 peer 的业务命令按现有调用次序有序，不为每帧新建任务。现有 peer 仅有一个 reply/session 槽，**保持一个 in-flight 查询的语义**；若调用方可并发，用覆盖整个 Query 事务的可取消/有界门控，不能只锁 Send 后误以为安全。不得顺手设计通用多路 RPC 框架。
3. 超时/取消/队列已满即返回既有失败路径；未发送的过期 begin/inspect 不得无限积压。采用最小且有测试的策略：使对应会话失效并结束，必要时关闭该 peer 清空传输队列。已经过线的操作靠 connectionId/sessionId/requestId 与会话状态拒绝迟到结果，不能假装能撤回网络字节。
4. End 也必须走 WS 分派，且在本地立即清除 valid/reply/session，再发送原 v1 end；不只改 Query 的 WriteMessage。end 发不出去时关闭该连接以令扩展清理租约，不能让它永远续租已结束选区。
5. invalidated 是异步推送，不能等下一次 Query 才处理；断线/撤销立刻使 IsValid=false 并唤醒 Query。重连必须新 connectionId，不可复活旧 frozen 会话，用户重启选框后才建立新会话。
6. 原有候选顺序、整页标记、25%–500% 页面 zoom、DPR/visualViewport、前台消歧、混合 DPI 原点换算保持。**不要照搬 VoxType 的字段上下文几何算法替换本仓 DOM 候选几何**。传输认证不代替原几何/文档身份校验。

### 10.5 扩展异步代际、配对失败恢复与消息来源

本仓 `post()` 使用全局 port 与 connectionId，query 在多处 await 后才发结果，sequence 也是全局链。只换 WebSocket 会把旧任务包装成新 connectionId 发给新连接，旧 sequence 还可能阻塞新连接。因此“原函数完全不改”是错误边界。

- 每次连接尝试分配独立 generation、connectionId、ready 状态与串行任务链；hello、query、unavailable、invalidate、end、续租回调捕获所属代际。每次 await 后、改 active 前、向页面发起下一步请求前、发送最终结果前核对代际仍有效。
- 旧连接的迟到 hello/result/unavailable/invalidated 不进入新连接；旧 query 不能在新连接创建 active，也不能清除新 active/renewal。end 仅清理其捕获的会话对象，保留现有“旧 end 完成不能结束新会话”的契约。
- 断线立即撤掉当前业务发送能力、清心跳并使旧会话失效，再异步给其参与 frame 发 end；结束后不允许旧任务重新启动 lease。10 s 页面租约保留原业务，不被拿来代替空闲 20 s WS 心跳。
- 尚未配对/未验证服务端 proof/未 auth_ok 时，hello 及任何 worker 发起的页面 metrics/inspect 请求均短路；窗口事件不能因最后 post 被丢弃就提前扫描页面。此约束不意味着移除原有静态 content script 注入。
- 心跳发送前检查当前连接身份及 readyState===OPEN；发送失败走统一 disconnect/退避。不能把 CLOSING/CLOSED 的 send 描述为必然异常（规范中该状态可静默丢弃）。周期较长的 connection 页面刷新仅在弹窗活着时存在。
- 启动、alarm、断连只创建 WS；**只有受信连接页的显式 Pair 请求能 connectNative**。onMessage 必须核对 sender.id、精确 sender.url===runtime.getURL('connection.html') 且无 sender.tab，再按 schema 分派；现有 content invalidated 消息处理器保留独立来源校验，不允许页面/content script 触发 Pair/Revoke 或取得秘密。
- storage.local 在读取/写入凭据前 await setAccessLevel(TRUSTED_CONTEXTS)；失败则拒绝配对/连接，不能吞异常后继续。凭据不进 sync、DOM、badge/title、日志、connection_status 或证据文件；状态接口只回具名状态与脱敏原因。
- connect/pair 去重；手动配对关闭旧 socket 时不得触发第二条自动配对链。重新配对失败时，若原 storage 凭据有效，恢复**纯 WS**连接/退避；从未配对的失败只报错误，不循环拉 host。
- 一次配对可能已让桌面旋转 key 但响应丢失/storage 保存失败；旧凭据恢复尝试将认证失败，此时明确提示再次 Pair，不能无声覆盖桌面文件、退回 legacy 或伪报连接成功。
- 未认证 challenge 的 instance 不符只证明“当前连接不匹配”，不能据此删除凭据或永久停止自动恢复。保留退避到最多每 5 min 一次，允许正确实例稍后回来；不自动配对、不扫描其它端口。状态文案提示 Pair，资源成本如实披露。
- timer、alarm、socket.onclose 均核对代际；任一时刻最多一个 socket 尝试，同一代际只保留一次快速 timer 与一个后续 alarm（成功/Pair/新代际替换时清除）。两种唤醒共享 connect 去重，防止陈旧 250 ms timer 在手动配对后另建连接。官方从 **Chrome 120** 起支持 30 s alarm；本方案最低 116，所以 116–119 的后台 alarm 按至少 1 min 兼容，120+ 可请求 0.5 min，之后退避至 5 min；不得用永久 timer 模拟精确 30 s 唤醒。SW 重启重新读凭据并重建需要的 alarm，不能依赖内存状态或 alarm 必然保留。

### 10.6 配对、撤销、持久化与旧版路径

- 主进程是 store 唯一写入者；host 不能复制浏览器环境下的数据目录决策。先保证 `ZenCropAppDataFilePath` 有效，再读写；磁盘/权限/DPAPI/RNG 失败显式返回，不生成空 secret/instance，不静默重建损坏文件。
- 新建 DPAPI 文件 owner-only ACL，临时文件与目标同目录，写完整、flush、原子替换成功后才发布新内存状态；失败保留原字节与原凭据。使用 per-install 规范路径哈希，勿用文件版本或可变 connectionId 作文件名。文件大小/条目数上限在解密/解析前后各自限制。
- RevokeAll/重新 Pair 必须有清晰提交点：持久化成功后旋转 instance/对应 profile secret、使相关已认证及**在认证途中的**旧代际不可选、清会话和回包、唤醒等待并关闭连接。不能只在握手第一次 Find 时检查凭据，否则撤销前取出的旧 key 能在撤销后成为 ready。用最小的凭据 revision/快照复核及状态锁提交处理竞态，不持有存储锁 join。
- RevokeAll 持久化失败必须显示失败；不得清磁盘/内存一半后展示“已撤销”。成功返回之后，旧连接的缓冲 reply 不可再被 Query 接受；已到调用方手里的旧结果不会被追溯撤回。
- Pair 同 profile 旋转 key 时只断开该 profile 旧直连，不影响其它 profile；RevokeAll 才作用全部直连。最多 32 个配对记录；损坏存储只允许显式重置恢复，错误原因可诊断但不含解密正文。
- 新扩展 + 旧桌面首帧 pair 被拒/管道关闭，7 s 内报告不兼容或不可用；不进入旧版 host 的无限 ready 等待。bootstrap host 的 stdio 首帧、命名管道就绪、回复、输出都纳入有界生命周期，stdout 只写 framed JSON，不夹带日志。
- 保留旧扩展 hello → 管道转发；旧通道没有新凭据，**Unpair all 不能被描述为撤销 legacy 权限**。状态区单列 legacy；按钮/说明明确撤销范围。此轮不移除旧通道或顺手改注册表所有权。
- 安装目录/数据目录移动后需要重新注册及配对；固定端口只允许一个活动实例。不能出现“已注册 A、连接 B、显示 A 已就绪”；成功状态要求本实例 listener/credential/实际连接一致。

### 10.7 UI、构建、交付和不做项

- 设置页只读注册状态、配对数、ready WS 数、legacy 数、listener 错误；Chrome/Edge 分开。注册 Ready、已配对、WS已认证、hello就绪四者不可混用。页面只在可见时低频刷新，关闭停 timer；不要用高频磁盘/注册表扫描判断连接。
- Store 初始读取、配对/撤销的磁盘与 DPAPI 可能阻塞；不在 100 ms 查询线程做持久化，也不让 GUI 同步等待网络。耗时撤销按现有异步 UI 结果投递方式处理，窗口销毁时不能回调悬空 HWND。
- UI 用 Strings.* 本地化、现有 ResourceIds 与 SettingsPageLayout；General 页是 **96 DPI 行式基准**，不要复制 VoxType 的 144 DPI UiStyle 常量。测试 96/144/192/288 DPI 的文字、按钮、滚动与键盘导航；没有实机证据时只声明静态布局通过。
- WebSocket 的公共头用 Pimpl/不透明系统类型，Winsock/WebSocket SDK 留在 cpp；现 CMake PCH 中 winsock2.h 在 windows.h 前，不能破坏。新增库依赖同时验证 smoke_zencrop_core/platform 独立链接，不能靠产品 exe 偶然补齐。
- 新测试进入既有 executable，并从实际 main/契约调度函数调用；改 tests/CMakeLists.txt 不等于已运行。保留原测试，不调高棘轮；新增守卫按 RuleIds/ExpectedRuleCount/每轮命中自检一并接线，当前拟 15→16，若并发开发改变数量则以脚本实测为准。
- 打包验证新增连接页、worker、manifest 与实际运行目录一致；MSI HKLM 及 GUI/脚本 HKCU 各自边界保持。卸载是否保留凭据须验证 payload 收获不会把用户 dat 文件带进安装器；运行目录更新不能清除用户配对数据。
- 此次实施不加第三方 WS 依赖、不新建浏览器实例、不新增通用 RPC/插件框架、不重写几何算法、不删除 1.x、不自动登录/确认 Allow、不在用户工作站隐式安装 MSI。未获明确发布指令不上传、不打发布完成标签；本轮规划任务更不实施任何产品代码。

## 11. 回归矩阵（用例必须明确接入；通过记录另立）

“旧代码先失败”在经济可行时执行；并发难稳定重现时提供锁/状态证据 + 有界交错测试，不能靠 Sleep(20) 让竞态消失。异步测试用事件/屏障控制关键顺序，等待有上限；64 次随机交错只是补充，不等于数学证明。

| ID | 场景与刺激 | 必须观察到的结果 |
| --- | --- | --- |
| T01 | v1 hello/begin/inspect/end/invalidated 原 fixture 逐项过旧封装；含较大 hello | 原接受/拒绝/大小/候选/几何行为相同；v2/pair 不混入 ParseMessage |
| T02 | bootstrap pair→paired 真实 framed stdio/管道；错字段/错版本/超 1 KiB/部分长度头 | 可完成配对；非法/慢帧有界拒绝；原 ReadMessage/WriteMessage 不被误用 |
| T03 | WS Origin 无斜杠 vs native argv/allowed_origins 有斜杠；伪造 prefix/重复 header | 正确各自通过；错形态拒绝；manifest key 推导 ID 一致 |
| T04 | 握手/认证慢滴、持续分片、错类型、嵌套 33、非法 UTF-8/掩码/超量 | 阶段总 deadline、深度 32、64 KiB/1 KiB 上限有效；异常不逃线程 |
| T05 | 正确 proof、错 key/版本/profile/instance、反射、旧 proof 重放 | 仅正确双方进入 ready；认证前无窗口信息、metrics/inspect、业务结果 |
| T06 | TCP 表匹配失败、非浏览器 PID、跨 SID/session、PID 时间错、根进程退出 | 不可路由；不回退 UIA/MSAA；持有句柄的生命周期安全 |
| T07 | Receive 判定前/等待入口/已等待/关闭后取消；0/1 ms 交错至少 64 次 | 每次有界退出，无 lost wake；不靠已睡眠的 waiter 当唯一证明 |
| T08 | 两 accept 同时收失败连接，peer 启动即结束，回收与 Stop 竞争 | 未初始化 thread 从不被清理；无并发 join/自 join/崩溃 |
| T09 | 停止时存在 idle peer、半 HTTP、半 proof、bootstrap、队列待发 | 所有等待能取消；受控夹具网络排空 ≤250 ms，报告最坏值；无遗留 host/句柄 |
| T10 | listener Start/Stop/失败/重启，未启动析构，独立 smoke 链接 | WSA引用匹配；无依赖 main 初始化；端口释放；无 SDK 链接遗漏 |
| T11 | 端口冲突、8 个含未认证连接、慢消费者、队列第 9 条、重复失败连接 | 拒绝有界；无端口扫描/抢占；内存线程句柄回落；主界面可响应 |
| T12 | WS 与 legacy 的 Query/End/IsValid；同 peer 两查询；100 ms 超时后旧 reply | 不串 request/session；失败有界；不重置预算；end 两路均送达或断连清理 |
| T13 | A 的 hello/query/inspectFrame 等 await 未完即断开连 B，随后释放 A 的 Promise | A 不向 B 发消息，不创建/清除 B active，不复活租约；B 不等 A 的 sequence |
| T14 | 导航/滚动/缩放/遮挡/切页 invalidated；断连后 frozen Query；重连 | 即时失效；旧冻结会话不可复活；原几何/候选/lease/整页回归仍通过 |
| T15 | 已配对后重配失败、从未配对失败、保存 storage 失败、Pair 重复点 | 旧有效凭据可恢复纯 WS；没有自动 host 重试；不能伪报成功 |
| T16 | challenge instance 错后正确实例回来；250 ms timer 与 Pair/alarm/SW 重启并发 | 保留凭据和退避恢复；最多一个尝试；无永久停重连或 native 降级 |
| T17 | 心跳时 OPEN/CLOSING/CLOSED、断线后旧 timer、服务端无 pong、睡眠恢复 | 仅当前 OPEN 发；超时断线有界；不会在旧连接续心跳或高频空转 |
| T18 | content/page/错误扩展页伪造 Pair/connection_status；setAccessLevel 失败 | 拒绝敏感命令；不拉 host/不返 secret；storage 隔离失败即停止 |
| T19 | DPAPI 重启、损坏、权限/写入/替换失败、RNG 失败、32条上限、路径变化 | 原凭据不丢；错误显式；损坏不覆盖；仅显式 Reset 恢复 |
| T20 | RevokeAll/重配与 proof 计算/ready 提交/排队 reply 同时发生 | 旧 revision 不能复活；成功撤销后旧结果不被消费；失败不假报已撤销 |
| T21 | 单 profile/Chrome+Edge/多 profile/旧新版本混用；状态区与进程数 | 按已认证 profile/legacy 分开计数，不把兼容 host 当单进程回归失败 |
| T22 | installed/portable/ZENCROP_DATA_DIR/主进程与浏览器环境不同、同机 VoxType、RDP 占端口 | 主进程唯一 store；不连错实例；同产品实例冲突明确；互不覆盖注册/凭据 |
| T23 | payload 安装、设置页四档 DPI、MSI HKLM/HKCU 优先级与卸载/升级 | 新资产完整；原注册所有权不变；用户 dat 不入包不误删；实机缺项逐项列出 |

测试位置：T01–T12/T19–T20 的原生与存储部分进既有原生目标；T05/T13–T18 的扩展部分进 worker VM；T21–T23 分离可离线项与明确标注的实机/VM 项。mock 只能证明已模拟路径，不能替代 TCP 属主、真实 Origin、浏览器 SW 生命周期或 MSI 安装证据。

建议执行顺序（从仓库根，PowerShell 7；遵守本机 RTK 规则）：

```powershell
rtk git status --short
rtk node tests/browser-companion/worker_contract.cjs
rtk pwsh -NoProfile -File scripts/check_architecture.ps1
rtk cmd.exe /d /c tests\build_and_run.bat test_screenshot_editor_state_contract
rtk cmd.exe /d /c build.bat
```

原生测试、smoke 和打包的具体输出路径与 target 从当时 CMake/测试脚本确认，不运行猜出的生成目录 exe；新增 CTest 的存在、实际测试函数被调用、日志与退出码均记录。C++库与 UI 改动完成后再跑相关门禁，不为本文纯文档修改构建或操作浏览器。以后编写实机工具时路径从用户目录/配置解析，不写死个人账户；连接已有浏览器严格走本机 chrome-cdp skill 的唯一入口，不能另开浏览器/调试端口或自动点击 Allow。

## 12. 实施 AI 交接、回滚与完成判定

### 12.1 开工检查与建议拆分

1. 重读本仓 AGENTS、MEMORY、本方案；git status 确认未提交变更，拟改文件先放本仓 `.bak/`，不 stash、不覆写用户修改。记录基线版本/commit 和参考 VoxType 的 commit；已有源路径/行号仅用于定位，代码事实优先。
2. 第一段只做协议分帧拆分与 bootstrap schema，旧 v1 回归通过。第二段接入 WS/身份/DPAPI/取消，先验证 T02–T12/T19–T20。第三段换 worker 并验证代际/配对失败/心跳。最后才接 UI、交付、文档与版本。
3. 每段保留可审查 diff 与明确失败/通过证据；不允许“主链能连上”后跳过 legacy/End/撤销/退出。既有单一 API 不足时，只补 owner 必需方法，不把 private 暴露给测试。
4. 若现实与本方案冲突（版本已前进、guard 数变化、测试目标移动），先核对并记录合理适配；不得为符合纸面数字抬基线、删门禁或放宽信任边界。需要新能力/破坏兼容的选择留为单独决策。

### 12.2 可执行回滚契约

| 状态 | 回退动作与期望 |
| --- | --- |
| 新桌面 + 旧扩展 | 保留 v1 native host 通路；仍可能多一个常驻 host，这是兼容成本 |
| 旧桌面 + 新扩展 | 显式 Pair 在总预算内失败、显示不兼容/不可用；不会无限 host 等待或静默 legacy |
| 同时回退桌面与扩展 | 用已保存的完整旧运行载荷与旧扩展包，经正常部署步骤恢复；registry 指向该运行载荷；不手抄零散 DLL |
| 取消本轮升级但保留用户数据 | 不删除 DPAPI文件、词表、设置或第三方注册；旧版忽略新增配对文件，恢复新版后可重新核对连接 |
| MSI 降级/升级失败 | 按既有 Windows Installer 契约在 VM 验证。源码有旧分支不等于 MSI 允许降级；禁止在本机强制覆盖产品版本来冒充回滚测试 |

备份/回滚不把凭据明文带入 git 或证据；恢复旧扩展时保存 manifest key/ID，避免换 ID 导致注册 allowed_origins 失配。更换安装目录后按新实例重新注册/配对，不复制旧 secret 强行绑定。

### 12.3 最终报告必须回答

- 改了哪些 owner/入口、v1哪些行为保持、Query/End/断开/撤销如何同路生效；实际程序与扩展版本、commit、守卫实测数量。
- 哪些用例先失败后通过；自动化运行命令、退出码、受控取消最大耗时、资源与时延观测方法。凭据正文、用户网页内容、本机私人词表不得进入报告/证据。
- Chrome、Edge、多 profile、真实多屏、睡眠/RDP、UI、MSI分别写“通过/失败/未执行”，不把参考项目证据或用户以前允许的延期继承为本项目已签收。
- 区分“代码/离线验证完成”“本机 Chrome 验证完成”“完整发布验收完成”。本方案编写完成本身不代表任一实施阶段完成。
- 未明确要求发布时，交付本地可审查结果及未验项后停止，不自行上传、安装、打发布 tag 或删除兼容路径。

## 13. 依据与复核入口

- 本仓代码：`src/net/BrowserCompanionProtocol.h/.cpp`（v1解析与分帧）、`BrowserCompanionBridge.h/.cpp`（线程/查询/host）、`extensions/browser-companion/worker.js`（全局 port/connectionId/sequence）、`src/core/AppDataPaths.cpp`（主进程数据目录）、`CMakeLists.txt`（已有 crypto 链接与 PCH）、`scripts/check_architecture.ps1`（RuleIds/ExpectedRuleCount/命中自检）。这些是本轮已对照的实现事实，实施时须复查。
- [Chrome Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)：native host、framing、manifest 和消息来源边界；不将 API 的容量上限误当本产品必须开放的上限。
- [Chrome WebSocket 与 service worker](https://developer.chrome.com/docs/extensions/how-to/web-platform/websockets)、[service worker 生命周期](https://developer.chrome.com/docs/extensions/develop/concepts/service-workers/lifecycle)：116+ 的消息交换延长 SW 生命周期；不能仅凭 socket 已建立就承诺常驻。
- [Chrome alarms](https://developer.chrome.com/docs/extensions/reference/api/alarms)：后台唤醒时间并非准时保证，跨会话保留行为须兼容旧版本；30 s 最小周期的 120 版本边界见上列生命周期文档。
- [Microsoft WSAStartup](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup)：每次成功初始化须有对应 cleanup；具体对象销毁顺序由本项目负责。
- [WebSocket 标准](https://websockets.spec.whatwg.org/#dom-websocket-send)：send 的状态规则；Origin 的序列化与 native argv 的 URI 形式分别核对，不能用字符串前缀绕过精确比较。
