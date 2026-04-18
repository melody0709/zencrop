# ZenCrop v2.0.1

[English](README.md)

ZenCrop 是对 [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/) 的独立重构实现。

## 项目起源

PowerToys Crop And Lock 是微软 PowerToys 工具集中的一个模块，允许用户将任意窗口裁剪为子窗口并锁定在屏幕上。然而，原项目深度依赖 PowerToys 框架，难以独立使用和定制。

ZenCrop 从零开始重构，完全独立运行，不依赖 PowerToys，保持原有核心功能的同时提供更轻量的解决方案。

## 功能特性

- **Reparent 模式**: 通过重新父窗口化技术，将目标窗口裁剪为独立子窗口
- **Thumbnail 模式**: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容，带浅蓝色边框标识，支持拖拽移动和 ESC 关闭
- **Always On Top**: 按 `Alt+T` 将任意窗口置顶，支持自定义边框（颜色、透明度、粗细、圆角）
- **快捷键自定义**: 所有快捷键均可自定义——点击输入框后按下组合键即可录入，支持冲突检测
- **Crop On Top**: 可在设置中开启，裁剪窗口后自动置顶
- **智能窗口检测**: 裁剪覆盖层自动跟随鼠标，动态高亮鼠标下方的窗口，支持裁剪屏幕上任意窗口
- **智能内容区域检测**: 基于 UI Automation 的元素检测——覆盖层自动识别鼠标下方的 UI 元素（浏览器标题栏、地址栏、内容区域等），用红色虚线框建议裁剪区域
- **单击接受建议**: 单击即可接受智能建议，拖拽仍可手动绘制矩形
- **裁剪区域调整**: 绘制裁剪矩形后可拖拽边/角拉伸、拖拽内部移动、双击确认，避免误操作
- **Borderless / Titlebar 切换**: 默认无边框，可通过托盘菜单切换显示标题栏
- **失效窗口自动清理**: 目标窗口被外部关闭时，自动移除对应的 Reparent/Thumbnail 窗口
- **系统托盘**: 后台运行，右键托盘图标访问菜单

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Alt+X` | 启动 Reparent 裁剪模式 |
| `Ctrl+Alt+C` | 启动 Thumbnail 裁剪模式 |
| `Ctrl+Alt+Z` | 关闭所有 Reparent 窗口 |
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
├── main.cpp              # 入口点，系统托盘，消息循环
├── Utils.h/cpp           # 工具函数
├── SmartDetector.h/cpp   # UI Automation 智能内容检测
├── OverlayWindow.h/cpp   # 裁剪区域选择覆盖层
├── ReparentWindow.h/cpp  # Reparent 模式窗口
├── ThumbnailWindow.h/cpp # Thumbnail 模式窗口
├── AlwaysOnTop.h/cpp     # Always On Top 管理器与边框窗口
├── Settings.h/cpp        # 统一设置 (AOT, 覆盖层, 快捷键)
├── app.ico               # 应用图标
├── resources.rc          # 对话框模板与图标资源
├── app.manifest          # DPI 感知与兼容性
├── build.bat             # MSVC 构建脚本
├── CMakeLists.txt        # CMake 配置
├── CHANGELOG.md          # 更新日志
├── AGENTS.md             # AI 开发指南
└── README.md
```

## 许可证

MIT License
