# ZenCrop v1.0

ZenCrop 是对 [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/) 的独立重构实现。

## 项目起源

PowerToys Crop And Lock 是微软 PowerToys 工具集中的一个模块,允许用户将任意窗口裁剪为子窗口并锁定在屏幕上。然而,原项目深度依赖 PowerToys 框架,难以独立使用和定制。

ZenCrop 从零开始重构,完全独立运行,不依赖 PowerToys,保持原有核心功能的同时提供更轻量的解决方案。

## 功能特性

- **Reparent 模式**: 通过重新父窗口化技术,将目标窗口裁剪为独立子窗口
- **Thumbnail 模式**: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容
- **Borderless / Titlebar 切换**: 默认无边框,可通过托盘菜单切换显示标题栏
- **系统托盘**: 后台运行,右键托盘图标访问菜单

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Alt+X` | 启动 Reparent 裁剪模式 |
| `Ctrl+Alt+T` | 启动 Thumbnail 裁剪模式 |
| 右键托盘图标 | 打开菜单 (切换标题栏/退出) |

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
└── README.md
```

## 许可证

MIT License