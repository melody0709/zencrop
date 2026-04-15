# 修复计划：最大化窗口 Reparent 模式裁剪错位

## 问题描述

Chrome 最大化时 Ctrl+Alt+X 裁剪存在三个问题：
1. 裁剪内容与实际框选不一致
2. 裁剪后出现部分白色空白
3. Ctrl+Alt+Z 还原后 Chrome 内容错位

窗口化时裁剪正常。

## 根因分析

### 问题1&2：裁剪内容错位 + 白色空白

**核心原因：最大化窗口 reparent 时 `WS_MAXIMIZE` 样式未移除**

当窗口最大化时，`GetWindowRect` 返回的坐标会超出屏幕边界（如 `(-8, -8, 1928, 1048)`），这是 Windows 为了隐藏最大化窗口边框的设计。当前代码在 `SetParent` + 添加 `WS_CHILD` 后，窗口仍保留 `WS_MAXIMIZE` 样式。

`WS_MAXIMIZE + WS_CHILD` 组合会导致：
- Windows 可能尝试让子窗口"最大化"填充父窗口（m_childWindow），改变窗口尺寸
- `SWP_FRAMECHANGED` 触发样式重新计算时，最大化子窗口的行为不可预测
- 窗口的非客户区（边框）渲染方式改变，导致内容偏移

**对比 PowerToys 官方实现**：PowerToys 也未移除 `WS_MAXIMIZE`，其文档明确承认 "Cropping maximized or full-screen windows in Reparent mode might not work"。这是官方也存在的已知缺陷。

**PowerToys 的部分缓解措施**（ZenCrop 缺失）：
- 对最大化窗口，用 `GetMonitorInfo` → `mi.rcWork` 计算偏移，而非 `GetClientRect`
- 保存完整 `WINDOWPLACEMENT` 结构（含 normal 位置和 maximized 状态）

### 问题3：还原后内容错位

**核心原因：还原逻辑不正确**

当前 `RestoreOriginalState()` 的问题：
1. **`m_originalRect` 保存的是最大化时的窗口坐标**（含负值如 -8），不是还原位置
2. **操作顺序错误**：先 `SetParent` + `SetWindowLongPtrW` 改变窗口状态，再 `SetWindowPos` 用最大化坐标定位，最后 `ShowWindow(SW_MAXIMIZE)` —— 这导致窗口先被设到错误位置，再尝试最大化
3. **未使用 `SetWindowPlacement`**：这是恢复窗口位置+最大化状态的正确 API，它能同时处理 normal 位置和 maximized 状态

**PowerToys 的正确做法**：
```cpp
// 1. 先 SetWindowPos 恢复尺寸（窗口仍是 child）
SetWindowPos(m_currentTarget, nullptr, originalRect.left, originalRect.top, width, height, ...);
// 2. 断开父子关系
SetParent(m_currentTarget, nullptr);
// 3. 用 SetWindowPlacement 恢复完整状态（位置 + 最大化）
if (originalPlacement.showCmd != SW_SHOWMAXIMIZED)
    originalPlacement.showCmd = SW_RESTORE;
SetWindowPlacement(m_currentTarget, &originalPlacement);
// 4. 最后恢复样式（移除 WS_CHILD）
originalStyle &= ~WS_CHILD;
SetWindowLongPtrW(m_currentTarget, GWL_STYLE, originalStyle);
```

## 修复方案

### 修改1：构造函数 — reparent 前移除 WS_MAXIMIZE

在 `SetParent` 之前，如果窗口是最大化的：
1. 移除 `WS_MAXIMIZE` 样式
2. 用 `SetWindowPos` 将窗口设为工作区大小（保持视觉不变）
3. 然后再进行 reparent

```cpp
// 在 SetParent 之前
if (m_wasMaximized) {
    // 移除 WS_MAXIMIZE，防止子窗口"最大化"行为
    LONG_PTR style = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    style &= ~WS_MAXIMIZE;
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, style);

    // 设为工作区大小，保持视觉位置不变
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(m_targetWindow, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowPos(m_targetWindow, nullptr,
        mi.rcWork.left, mi.rcWork.top,
        mi.rcWork.right - mi.rcWork.left,
        mi.rcWork.bottom - mi.rcWork.top,
        SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

// 重新获取窗口位置（可能已改变）
RECT targetRect;
GetWindowRect(m_targetWindow, &targetRect);
int offsetX = cropRect.left - targetRect.left;
int offsetY = cropRect.top - targetRect.top;
```

### 修改2：SaveOriginalState — 保存完整 WINDOWPLACEMENT

```cpp
void ReparentWindow::SaveOriginalState() {
    m_originalParent = GetParent(m_targetWindow);
    m_originalStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    m_originalExStyle = GetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE);

    m_originalPlacement = { sizeof(WINDOWPLACEMENT) };
    GetWindowPlacement(m_targetWindow, &m_originalPlacement);

    GetWindowRect(m_targetWindow, &m_originalRect);
    m_wasMaximized = (m_originalPlacement.showCmd == SW_SHOWMAXIMIZED);
}
```

新增成员变量：
- `WINDOWPLACEMENT m_originalPlacement`
- `LONG_PTR m_originalExStyle`

### 修改3：RestoreOriginalState — 用 SetWindowPlacement 正确恢复

```cpp
void ReparentWindow::RestoreOriginalState() {
    if (!m_targetWindow || !IsWindow(m_targetWindow)) return;

    // 1. 先恢复尺寸（窗口仍是 child，坐标相对于 parent）
    int width = m_originalRect.right - m_originalRect.left;
    int height = m_originalRect.bottom - m_originalRect.top;
    SetWindowPos(m_targetWindow, nullptr, m_originalRect.left, m_originalRect.top,
        width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // 2. 断开父子关系
    SetParent(m_targetWindow, m_originalParent);

    // 3. 用 SetWindowPlacement 恢复完整状态
    if (m_originalPlacement.showCmd != SW_SHOWMAXIMIZED) {
        m_originalPlacement.showCmd = SW_RESTORE;
    }
    SetWindowPlacement(m_targetWindow, &m_originalPlacement);

    // 4. 恢复样式（移除 WS_CHILD）
    m_originalStyle &= ~WS_CHILD;
    SetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE, m_originalExStyle);
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, m_originalStyle);

    m_targetWindow = nullptr;
}
```

### 修改4：构造函数 — 最大化窗口的偏移量用工作区计算

参考 PowerToys，对最大化窗口使用 `mi.rcWork` 计算偏移：

```cpp
RECT targetRect;
GetWindowRect(m_targetWindow, &targetRect);

int diffX = 0, diffY = 0;
if (m_wasMaximized) {
    // 最大化窗口：用工作区偏移替代客户区偏移
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(m_targetWindow, MONITOR_DEFAULTTONEAREST), &mi);
    diffX = mi.rcWork.left - targetRect.left;
    diffY = mi.rcWork.top - targetRect.top;
}

// 调整 cropRect 到窗口坐标系
RECT adjustedCropRect = cropRect;
adjustedCropRect.left += diffX;
adjustedCropRect.top += diffY;
adjustedCropRect.right += diffX;
adjustedCropRect.bottom += diffY;

int offsetX = adjustedCropRect.left;
int offsetY = adjustedCropRect.top;
```

## 涉及文件

1. `ReparentWindow.h` — 新增 `m_originalPlacement`、`m_originalExStyle` 成员
2. `ReparentWindow.cpp` — 修改构造函数、SaveOriginalState、RestoreOriginalState

## 验证方法

1. Chrome 最大化 → Ctrl+Alt+X 框选区域 → 验证裁剪内容与框选一致
2. 裁剪后无白色空白
3. Ctrl+Alt+Z 还原 → Chrome 恢复最大化且内容位置正确
4. Chrome 窗口化 → 同样操作 → 行为不变（回归测试）
5. 多显示器场景测试
