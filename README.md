# ZenCrop v1.2

ZenCrop 是对 [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/) 的独立重构实现。

## 项目起源

PowerToys Crop And Lock 是微软 PowerToys 工具集中的一个模块,允许用户将任意窗口裁剪为子窗口并锁定在屏幕上。然而,原项目深度依赖 PowerToys 框架,难以独立使用和定制。

ZenCrop 从零开始重构,完全独立运行,不依赖 PowerToys,保持原有核心功能的同时提供更轻量的解决方案。

## 功能特性

- **Reparent 模式**: 通过重新父窗口化技术,将目标窗口裁剪为独立子窗口
- **Thumbnail 模式**: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容,带浅蓝色边框标识,支持拖拽移动和 ESC 关闭
- **智能窗口检测**: 裁剪覆盖层自动跟随鼠标,动态高亮鼠标下方的窗口,支持裁剪屏幕上任意窗口
- **重叠窗口处理**: 悬停窗口自动提到 Z 序顶部,确保重叠区域显示真实内容
- **桌面过滤**: 桌面背景不可被选为裁剪目标,避免空输出
- **Borderless / Titlebar 切换**: 默认无边框,可通过托盘菜单切换显示标题栏
- **系统托盘**: 后台运行,右键托盘图标访问菜单

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Alt+X` | 启动 Reparent 裁剪模式 |
| `Ctrl+Alt+C` | 启动 Thumbnail 裁剪模式 |
| `Ctrl+Alt+Z` | 关闭所有 Reparent 窗口 |
| `ESC` | 关闭当前 Thumbnail 窗口 / 取消裁剪 |
| 右键托盘图标 | 打开菜单 (切换标题栏/退出) |

## 使用方式

1. 按下 `Ctrl+Alt+X` 或 `Ctrl+Alt+C` 进入裁剪模式
2. 移动鼠标,覆盖层会自动高亮鼠标下方的窗口
3. 在目标窗口上按住左键拖拽,框选裁剪区域
4. 释放鼠标完成裁剪
5. 按 `ESC` 取消裁剪或关闭 Thumbnail 窗口
6. 按 `Ctrl+Alt+Z` 关闭所有 Reparent 窗口

> **注意**: 桌面背景无法被选为裁剪目标,鼠标移到桌面时点击将自动退出裁剪模式。

## 技术栈

- **语言**: C++17
- **框架**: Native Windows Win32 API
- **依赖**: user32, gdi32, dwmapi, shcore, shell32

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
├── main.cpp              # 入口点,系统托盘,消息循环
├── Utils.h/cpp           # 工具函数
├── OverlayWindow.h/cpp   # 裁剪区域选择覆盖层
├── ReparentWindow.h/cpp  # Reparent 模式窗口
├── ThumbnailWindow.h/cpp # Thumbnail 模式窗口
├── app.ico               # 应用图标
├── resources.rc          # 图标资源定义
├── build.bat             # MSVC 构建脚本
├── CMakeLists.txt        # CMake 配置
├── CHANGELOG.md          # 更新日志
└── README.md
```

## 许可证

MIT License
