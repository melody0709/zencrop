#pragma once

#include <windows.h>

namespace Screenshot {

bool DrawToolbarIcon(HDC hdc, unsigned int codepoint, const RECT& rect, COLORREF color);
bool DrawDropdownArrow(HDC hdc, const RECT& rect, COLORREF color);

}
