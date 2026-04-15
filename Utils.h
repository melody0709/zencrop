#pragma once
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <algorithm>
#include <mutex>

RECT GetVirtualScreenRect();
RECT GetClientRectInScreenSpace(HWND hwnd);

// Check if window uses XAML Islands or DirectComposition
// These windows cannot be reparented properly due to DComp visual tree disconnect
bool IsXamlOrDCompWindow(HWND hwnd);
