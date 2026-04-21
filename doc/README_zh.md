# ZenCrop v2.2.2

[English](../README.md)

ZenCrop 是对 [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock/) 的独立、**增强型**重构实现。

---
## 🔥 V2.2.0 & V2.2.1 震撼更新：终极 Thumbnail 缩略图模式

我们彻底重构了 **Thumbnail（缩略图）模式 (Ctrl+Alt+C)**，突破了 Windows DWM API 的底层限制，带来了史无前例的强大特性：

- **严格等比例缩放**: 无论是通过原生窗口边缘拖拽，还是使用 **AltSnap** 等第三方神器，ZenCrop 都会在底层数学级别强行锁定裁剪画面的原始宽高比。画面永远不会变形，也绝不产生黑边！
- **欺骗引擎级的无痕后台渲染**: 将庞大的原始窗口从任务栏和屏幕上完全隐藏！我们利用开创性的“1像素驻留 + 强制置顶” Hack 结合 COM 接口抹除技术，完美骗过现代浏览器（Chrome、Edge）和 Electron（VSCode）的遮挡追踪器，令其在后台毫无察觉地为您源源不断提供满血 60FPS 的实时渲染流。同时在 **V2.2.1** 中，*Thumbnail 窗口本身*恢复了任务栏独立图标显示，被遮挡时随时可以一键召唤回前台或右键关闭。

📖 *技术深度解析：[ZenCrop Thumbnail 缩放与隐身技术报告](thumbnail_scaling_hiding_technology_zh.md)*
---

## 🚀 为什么选择 ZenCrop 胜过官方 PowerToys？

微软官方的 PowerToys 模块在尝试裁剪现代 Windows 应用（UWP/WinUI/XAML 应用，如计算器、系统设置等）时，会触发底层的渲染断连，导致严重的[“全白/黑屏”已知缺陷](https://learn.microsoft.com/en-us/windows/powertoys/crop-and-lock#known-issues)。

**ZenCrop 彻底攻克了这一技术壁垒！** 官方 PowerToys Crop And Lock **明确无法支持**、**一剪就崩溃或全白**的应用，ZenCrop 如今皆能通过独创的双引擎架构完美交互式裁剪：

**1. 原生 Viewport (视口) 裁剪技术:**
- **Windows 计算器、设置、Microsoft To Do (微软待办)** 等现代 UWP 沙盒应用
- 通过视口区域操作而非跨进程 DWM 挂载，彻底避免了传统 Reparent 机制引发的底层渲染管线断连 Bug，完美保留交互能力。
📖 *技术深度解析：[ZenCrop Viewport 技术实现报告](viewport_technology_report.md)*

**2. 深度视觉树雷达检测与高级重父化 (Reparenting):**
- **Win11 画图 (Paint)** (采用现代 `DesktopChildSiteBridge` WinUI 3 架构)
- **Magpie (麦皮)** 等内嵌现代 XAML 组件的传统 Win32 程序 (`DesktopWindowContentBridge`)
- 毫秒级智能识别底层架构，动态分支其重父化逻辑：通过智能背景色伪装（修复深色模式 Mica 材质丢失问题）、差异化的标题栏剥夺技术以及精密的逆向坐标推移补偿算法，安全绕过了 Windows 脆弱的组合机制，彻底消除了崩溃与标题栏错位 Bug，实现无缝视觉融合。
📖 *技术深度解析：[攻克现代应用裁剪难题：WinUI 3 Reparenting 技术实现报告](WinUI3_Reparenting_Fix_zh.md)*

## 项目起源

PowerToys Crop And Lock 是微软 PowerToys 工具集中的一个模块，允许用户将任意窗口裁剪为子窗口并锁定在屏幕上。然而，原项目深度依赖 PowerToys 框架，难以独立使用和定制。

ZenCrop 从零开始重构，完全独立运行，不依赖 PowerToys，不仅保持了原有核心功能，更在兼容性上实现了对官方的全面超越。

## 功能特性

- **智能 Reparent 模式**: 将目标窗口裁剪为独立子窗口。ZenCrop 会自动检测现代 UWP/WinUI 应用（如计算器或设置），并无缝回退到特殊的 **Viewport (视口) 模式**。这彻底解决了传统 Reparent 模式导致的现代应用“全白”渲染 Bug，确保所有应用都能被完美裁剪并保持交互。
- **Thumbnail 模式**: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容，带浅蓝色边框标识。*在 V2.2.0 和 V2.2.1 中全新升级:* 目标窗口能够自动从屏幕和任务栏完全隐藏，并且保证 Chromium/Electron 等引擎维持满血 60FPS 渲染；同时 Thumbnail 窗口自身拥有了独立的任务栏图标，方便找回。完美支持原生拖拽以及 AltSnap 等工具的严格等比例缩放操作。
📖 *技术深度解析：[ZenCrop Thumbnail 缩放与隐身技术](thumbnail_scaling_hiding_technology_zh.md)*
- **Always On Top**: 按 `Alt+T` 将任意窗口置顶，支持自定义边框（颜色、透明度、粗细、圆角）
- **快捷键自定义**: 所有快捷键均可自定义——点击输入框后按下组合键即可录入，支持冲突检测
- **Crop On Top**: 可在设置中开启，裁剪窗口后自动置顶
- **智能窗口检测**: 裁剪覆盖层自动跟随鼠标，动态高亮鼠标下方的窗口，支持裁剪屏幕上任意窗口
- **智能内容区域检测**: 基于 UI Automation 的元素检测——覆盖层自动识别鼠标下方的 UI 元素（浏览器标题栏、地址栏、内容区域等），用红色虚线框建议裁剪区域
- **单击接受建议**: 单击即可接受智能建议，拖拽仍可手动绘制矩形
- **裁剪区域调整**: 绘制裁剪矩形后可拖拽边/角拉伸、拖拽内部移动、双击确认，避免误操作
  - **方向键控制**: 调整模式下支持键盘精确操控裁剪框
    - 方向键 ↑↓←→：整体移动 1px
    - Ctrl+方向键：对应边扩大 1px
    - Shift+方向键：对应边缩小 1px（受最小尺寸保护）
    - Enter 键：确认裁剪（等同双击）
  - **坐标尺寸标注**: 调整模式下裁剪框左上角动态显示顶点坐标和框选尺寸，格式如 `1077, 864 · 320 x 240 px`，空间不足时自动移至下方
- **Borderless / Titlebar 切换**: 默认无边框，可通过托盘菜单切换显示标题栏
- **失效窗口自动清理**: 目标窗口被外部关闭时，自动移除对应的裁剪窗口
- **系统托盘**: 后台运行，右键托盘图标访问菜单

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Alt+X` | 启动智能 Reparent 裁剪模式 |
| `Ctrl+Alt+C` | 启动 Thumbnail 裁剪模式 |
| `Ctrl+Alt+V` | 强制使用 Viewport 裁剪模式（手动降级回退） |
| `Ctrl+Alt+Z` | 一键关闭所有正在生效的裁剪窗口 |
| `Alt+T` | 切换前台窗口的 Always On Top 状态 |
| `ESC` | 取消当前裁剪矩形 / 取消整个裁剪模式 / 关闭当前 Thumbnail 窗口 |
| 右键托盘图标 | 打开菜单 (切换标题栏 / 设置 / 退出) |

> 所有快捷键均可在设置中自定义（右键托盘 → Settings → ZenCrop / Always On Top 标签页）。

## 使用方式

1. 按下 `Ctrl+Alt+X` 或 `Ctrl+Alt+C` 进入裁剪模式
2. 移动鼠标——红色虚线框自动高亮检测到的 UI 元素（基于 UI Automation 智能检测）
3. **单击**接受智能建议进入调整模式，或**拖拽**手动绘制裁剪矩形
4. 在**调整模式**中：
   - 拖拽边/角 → 拉伸矩形
   - 拖拽矩形内部 → 移动矩形
   - 双击矩形内部 → 确认裁剪，生成窗口
   - **方向键** (↑↓←→) 移动裁剪框 1px
   - **Ctrl+方向键** 扩大对应边 1px
   - **Shift+方向键** 缩小对应边 1px
   - **Enter** 确认裁剪（等同双击）
   - 按 `ESC` 取消当前矩形可重新绘制，再按 `ESC` 退出
   - 点击矩形外部 → 取消当前矩形，可重新绘制
5. 按 `Ctrl+Alt+Z` 关闭所有 Reparent 窗口
6. 按 `Alt+T` 切换任意窗口的 Always On Top 状态

> **注意**: 桌面背景无法被选为裁剪目标，鼠标移到桌面时点击将自动退出裁剪模式。

## 设置

右键托盘图标 → **Settings** 打开标签式设置对话框：

- **ZenCrop 标签页**: 裁剪覆盖层颜色和粗细、Crop On Top 开关、Reparent/Thumbnail/Close Reparent 快捷键自定义
- **Always On Top 标签页**: 边框显示开关、颜色（系统强调色或自定义）、透明度、粗细、圆角、AOT 快捷键自定义

## 技术栈

- **语言**: C++17
- **框架**: Native Windows Win32 API
- **依赖**: user32, gdi32, dwmapi, shcore, shell32, ole32, oleaut32, shlwapi, comctl32, comdlg32, advapi32

## 构建

### 前提条件

- Visual Studio 2022 (含 vcvars64)
- Windows SDK

### 编译

```bash
# 使用 build.bat (推荐)
build.bat

# 或使用 CMake
cmake -B build && cmake --build build
```

编译完成后生成 `ZenCrop.exe`。

## 项目结构

```
zencrop/
├── main.cpp              # 主入口，系统托盘，消息循环
├── Utils.h/cpp           # 工具函数
├── SmartDetector.h/cpp   # UI Automation 智能内容区域检测
├── OverlayWindow.h/cpp   # 裁剪区域选择覆盖层
├── ReparentWindow.h/cpp  # Reparent 模式窗口
├── ThumbnailWindow.h/cpp # Thumbnail 模式窗口
├── ViewportWindow.h/cpp  # Viewport 模式窗口（专为现代应用设计）
├── AlwaysOnTop.h/cpp     # Always On Top 管理器与边框窗口
├── Settings.h/cpp        # 统一设置对话框 (AOT, 覆盖层, 快捷键)
├── app.ico               # 应用图标
├── resources.rc          # 对话框模板与图标资源
├── app.manifest          # DPI 感知与兼容性配置
├── build.bat             # MSVC 构建脚本
├── CMakeLists.txt        # CMake 配置
├── AGENTS.md             # AI 开发指导与踩坑记录
├── README.md             # 英文文档
└── doc/
    ├── CHANGELOG.md                   # 更新日志
    ├── README_zh.md                   # 中文文档
    ├── viewport_technology_report.md  # Viewport 模式技术报告 (中文)
    └── viewport_technology_report_en.md # Viewport 模式技术报告 (英文)
```

## ☕ 请作者喝杯咖啡

如果这个项目对您有帮助，欢迎打赏支持，您的每一份支持都是我持续更新的动力 ❤️

<table>
<tr>
<td align="center" width="33%">
<img src="../assets/wechat.png" width="250" alt="微信赞赏"><br>
<b>微信赞赏</b>
</td>
<td align="center" width="33%">
<img src="../assets/alipay.jpg" width="250" alt="支付宝"><br>
<b>支付宝</b>
</td>
<td align="center" width="33%">
<a href="https://buymeacoffee.com/relakkes" target="_blank">
<img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" width="250" alt="Buy Me a Coffee">
</a><br>
<b>Buy Me a Coffee</b>
</td>
</tr>
</table>

---

## 许可证

MIT License
