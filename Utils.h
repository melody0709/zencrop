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
