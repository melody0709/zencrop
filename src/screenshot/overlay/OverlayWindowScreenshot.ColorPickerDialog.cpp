#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/WideStringUtils.h"
#include "screenshot/ScreenshotColorFormat.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <windows.h>
#include <windowsx.h>

// S-H-CLOSE-9: real translation unit (was OverlayWindowScreenshot.ColorPickerDialog.inl).
// Free-helper residual → real TU. ShowScreenshotColorPickerDialog already external (S-H-CLOSE-6).
// No product semantic change. User override of ADR-003 hard stop 120 authorized resume.
static const wchar_t* kScreenshotColorPickerDialogClass = L"ZenCrop.ScreenshotColorPickerDialog";
static std::once_flag s_screenshotColorPickerDialogClassReg;

struct ScreenshotColorPickerDialogLayout {
    RECT sv = {};
    RECT eye = {};
    RECT hue = {};
    RECT alpha = {};
    RECT combo = {};
    RECT edit[3] = {};
    RECT alphaEdit = {};
    RECT separator = {};
    RECT ok = {};
    RECT cancel = {};
};

struct ScreenshotColorPickerDialogState {
    COLORREF color = RGB(255, 0, 0);
    COLORREF beforePickColor = RGB(255, 0, 0);
    int alpha = 100;
    int beforePickAlpha = 100;
    int hue = 0;
    int saturation = 100;
    int value = 100;
    int mode = 0; // 0=Hex, 1=RGB, 2=HSV
    int dpi = 96;
    int initialX = 0;
    int initialY = 0;
    bool accepted = false;
    bool syncing = false;
    bool picking = false;
    int dragPart = 0; // 1=SV, 2=Hue, 3=Alpha
    HWND combo = nullptr;
    HWND edit[3] = {};
    HWND alphaEdit = nullptr;
    HFONT font = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH editBrush = nullptr;
    HBITMAP svBitmap = nullptr;
    HBITMAP hueBitmap = nullptr;
    HBITMAP alphaBitmap = nullptr;
    int svBitmapW = 0;
    int svBitmapH = 0;
    int hueBitmapW = 0;
    int hueBitmapH = 0;
    int alphaBitmapW = 0;
    int alphaBitmapH = 0;
    int svBitmapHue = -1;
    COLORREF alphaBitmapColor = CLR_INVALID;
    int alphaBitmapDpi = 0;
    DWORD lastDragRedrawTick = 0;
};

static int ScreenshotColorPickerScale(int value, int dpi) {
    return MulDiv(value, dpi, 96);
}

static int ScreenshotColorPickerClamp(int value, int minValue, int maxValue) {
    return (std::min)((std::max)(value, minValue), maxValue);
}

static DWORD ScreenshotColorPickerDibPixel(COLORREF color) {
    return (DWORD)WideUnpackB(static_cast<unsigned int>(color)) |
        ((DWORD)WideUnpackG(static_cast<unsigned int>(color)) << 8) |
        ((DWORD)WideUnpackR(static_cast<unsigned int>(color)) << 16);
}

static void ScreenshotColorPickerDeleteBitmap(HBITMAP& bitmap) {
    if (bitmap) {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }
}

static HBITMAP ScreenshotColorPickerCreateDib(int width, int height, DWORD** pixels) {
    if (pixels) *pixels = nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (pixels) *pixels = (DWORD*)bits;
    return bitmap;
}

static void ScreenshotColorPickerDrawBitmap(HDC hdc, HBITMAP bitmap, RECT rc) {
    if (!bitmap || rc.right <= rc.left || rc.bottom <= rc.top) return;
    HDC mem = CreateCompatibleDC(hdc);
    if (!mem) return;
    HGDIOBJ old = SelectObject(mem, bitmap);
    BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static ScreenshotColorPickerDialogLayout ScreenshotColorPickerGetLayout(const ScreenshotColorPickerDialogState& state) {
    int d = state.dpi;
    ScreenshotColorPickerDialogLayout l;
    l.sv = {
        ScreenshotColorPickerScale(12, d),
        ScreenshotColorPickerScale(12, d),
        ScreenshotColorPickerScale(248, d),
        ScreenshotColorPickerScale(172, d)
    };
    l.eye = {
        ScreenshotColorPickerScale(12, d),
        ScreenshotColorPickerScale(196, d),
        ScreenshotColorPickerScale(50, d),
        ScreenshotColorPickerScale(232, d)
    };
    l.hue = {
        ScreenshotColorPickerScale(72, d),
        ScreenshotColorPickerScale(198, d),
        ScreenshotColorPickerScale(248, d),
        ScreenshotColorPickerScale(210, d)
    };
    l.alpha = {
        ScreenshotColorPickerScale(72, d),
        ScreenshotColorPickerScale(222, d),
        ScreenshotColorPickerScale(248, d),
        ScreenshotColorPickerScale(234, d)
    };
    l.combo = {
        ScreenshotColorPickerScale(12, d),
        ScreenshotColorPickerScale(252, d),
        ScreenshotColorPickerScale(78, d),
        ScreenshotColorPickerScale(282, d)
    };
    l.edit[0] = {
        ScreenshotColorPickerScale(88, d),
        ScreenshotColorPickerScale(252, d),
        ScreenshotColorPickerScale(166, d),
        ScreenshotColorPickerScale(282, d)
    };
    l.edit[1] = {
        ScreenshotColorPickerScale(128, d),
        ScreenshotColorPickerScale(252, d),
        ScreenshotColorPickerScale(166, d),
        ScreenshotColorPickerScale(282, d)
    };
    l.edit[2] = {
        ScreenshotColorPickerScale(168, d),
        ScreenshotColorPickerScale(252, d),
        ScreenshotColorPickerScale(206, d),
        ScreenshotColorPickerScale(282, d)
    };
    l.alphaEdit = {
        ScreenshotColorPickerScale(208, d),
        ScreenshotColorPickerScale(252, d),
        ScreenshotColorPickerScale(248, d),
        ScreenshotColorPickerScale(282, d)
    };
    l.separator = {
        ScreenshotColorPickerScale(12, d),
        ScreenshotColorPickerScale(302, d),
        ScreenshotColorPickerScale(248, d),
        ScreenshotColorPickerScale(303, d)
    };
    l.ok = {
        ScreenshotColorPickerScale(120, d),
        ScreenshotColorPickerScale(326, d),
        ScreenshotColorPickerScale(188, d),
        ScreenshotColorPickerScale(358, d)
    };
    l.cancel = {
        ScreenshotColorPickerScale(196, d),
        ScreenshotColorPickerScale(326, d),
        ScreenshotColorPickerScale(248, d),
        ScreenshotColorPickerScale(358, d)
    };
    return l;
}

static void ScreenshotColorPickerInvalidatePadded(HWND hwnd, RECT rc, int dpi) {
    int pad = ScreenshotColorPickerScale(10, dpi);
    InflateRect(&rc, pad, pad);
    InvalidateRect(hwnd, &rc, FALSE);
}

static void ScreenshotColorPickerInvalidateInteractiveParts(HWND hwnd,
    ScreenshotColorPickerDialogState* state,
    bool sv,
    bool hue,
    bool alpha,
    bool eye) {
    if (!hwnd || !state) return;
    ScreenshotColorPickerDialogLayout l = ScreenshotColorPickerGetLayout(*state);
    if (sv) ScreenshotColorPickerInvalidatePadded(hwnd, l.sv, state->dpi);
    if (hue) ScreenshotColorPickerInvalidatePadded(hwnd, l.hue, state->dpi);
    if (alpha) ScreenshotColorPickerInvalidatePadded(hwnd, l.alpha, state->dpi);
    if (eye) ScreenshotColorPickerInvalidatePadded(hwnd, l.eye, state->dpi);
}

static void ScreenshotColorPickerInvalidateDragPart(HWND hwnd, ScreenshotColorPickerDialogState* state, int part) {
    if (part == 1) {
        ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, true, false, true, false);
    } else if (part == 2) {
        ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, true, true, true, false);
    } else if (part == 3) {
        ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, false, false, true, false);
    }
}

static bool ScreenshotColorPickerShouldRedrawDrag(ScreenshotColorPickerDialogState* state, DWORD intervalMs) {
    if (!state) return false;
    DWORD now = GetTickCount();
    if (state->lastDragRedrawTick != 0 &&
        (DWORD)(now - state->lastDragRedrawTick) < intervalMs) {
        return false;
    }
    state->lastDragRedrawTick = now;
    return true;
}

static void ScreenshotColorPickerSetChildFont(HWND hwnd, HFONT font) {
    if (hwnd && font) {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

static void ScreenshotColorPickerApplyDarkTitlebar(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto fn = (DwmSetWindowAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (fn) {
        BOOL enabled = TRUE;
        fn(hwnd, 20, &enabled, sizeof(enabled));
        fn(hwnd, 19, &enabled, sizeof(enabled));
    }
    FreeLibrary(dwm);
}

static void ScreenshotColorPickerSetColor(ScreenshotColorPickerDialogState* state, COLORREF color) {
    if (!state) return;
    state->color = color;
    ScreenshotRgbToHsvLocal(color, state->hue, state->saturation, state->value);
}

static void ScreenshotColorPickerSetFromHsv(ScreenshotColorPickerDialogState* state) {
    if (!state) return;
    state->hue = ((state->hue % 360) + 360) % 360;
    state->saturation = ScreenshotColorPickerClamp(state->saturation, 0, 100);
    state->value = ScreenshotColorPickerClamp(state->value, 0, 100);
    state->color = ScreenshotHsvToRgbLocal(state->hue, state->saturation, state->value);
}

static int ScreenshotColorPickerEditInt(HWND edit, int fallback, int minValue, int maxValue) {
    wchar_t text[32] = {};
    if (!edit) return fallback;
    GetWindowTextW(edit, text, (int)(sizeof(text) / sizeof(text[0])));
    if (text[0] == 0) return fallback;
    // OWN-77: pure int parse (WideStringUtils) then clamp.
    return ScreenshotColorPickerClamp(
        WideParseJsonIntToken(text, fallback), minValue, maxValue);
}

static bool ScreenshotColorPickerParseHex(HWND edit, COLORREF& color) {
    wchar_t text[32] = {};
    if (!edit) return false;
    GetWindowTextW(edit, text, (int)(sizeof(text) / sizeof(text[0])));
    // OWN-80: pure color hex try-parse (WideStringUtils); packed == COLORREF layout.
    unsigned int packed = 0;
    if (!WideTryParseColorHex(text, packed)) return false;
    color = static_cast<COLORREF>(packed);
    return true;
}

static void ScreenshotColorPickerUpdateEditVisibility(HWND hwnd, ScreenshotColorPickerDialogState* state) {
    if (!state || !state->combo) return;
    ScreenshotColorPickerDialogLayout l = ScreenshotColorPickerGetLayout(*state);
    int eh = l.edit[0].bottom - l.edit[0].top;
    SetWindowPos(state->combo, nullptr, l.combo.left, l.combo.top,
        l.combo.right - l.combo.left, ScreenshotColorPickerScale(160, state->dpi),
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (state->mode == 0) {
        RECT hexRc = {
            ScreenshotColorPickerScale(88, state->dpi),
            l.edit[0].top,
            ScreenshotColorPickerScale(202, state->dpi),
            l.edit[0].bottom
        };
        SetWindowPos(state->edit[0], nullptr, hexRc.left, hexRc.top,
            hexRc.right - hexRc.left, eh, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(state->edit[0], SW_SHOW);
        ShowWindow(state->edit[1], SW_HIDE);
        ShowWindow(state->edit[2], SW_HIDE);
    } else {
        for (int i = 0; i < 3; ++i) {
            SetWindowPos(state->edit[i], nullptr, l.edit[i].left, l.edit[i].top,
                l.edit[i].right - l.edit[i].left, eh, SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(state->edit[i], SW_SHOW);
        }
    }
    SetWindowPos(state->alphaEdit, nullptr, l.alphaEdit.left, l.alphaEdit.top,
        l.alphaEdit.right - l.alphaEdit.left, eh, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ScreenshotColorPickerSyncFields(HWND hwnd, ScreenshotColorPickerDialogState* state, bool invalidateParent = true) {
    if (!state || !state->combo || !state->edit[0]) return;
    state->syncing = true;
    state->mode = ScreenshotColorPickerClamp(state->mode, 0, 2);
    if ((int)SendMessageW(state->combo, CB_GETCURSEL, 0, 0) != state->mode) {
        SendMessageW(state->combo, CB_SETCURSEL, state->mode, 0);
    }
    ScreenshotColorPickerUpdateEditVisibility(hwnd, state);

    auto setTextIfChanged = [](HWND edit, const wchar_t* value) {
        if (!edit || !value) return;
        wchar_t current[32] = {};
        GetWindowTextW(edit, current, (int)(sizeof(current) / sizeof(current[0])));
        if (!WideEquals(current, value)) {
            SetWindowTextW(edit, value);
        }
    };

    if (state->mode == 0) {
        // OWN-112: pure lowercase hex (WideStringUtils).
        {
            std::wstring hex = WideColorToHexLower(static_cast<unsigned int>(state->color));
            setTextIfChanged(state->edit[0], hex.c_str());
        }
    } else if (state->mode == 1) {
        // OWN-112: pure int labels for RGB channels (WideStringUtils).
        setTextIfChanged(state->edit[0], WideFormatIntLabel(static_cast<int>(WideUnpackR(static_cast<unsigned int>(state->color)))).c_str());
        setTextIfChanged(state->edit[1], WideFormatIntLabel(static_cast<int>(WideUnpackG(static_cast<unsigned int>(state->color)))).c_str());
        setTextIfChanged(state->edit[2], WideFormatIntLabel(static_cast<int>(WideUnpackB(static_cast<unsigned int>(state->color)))).c_str());
    } else {
        // OWN-112: pure int labels for HSV channels (WideStringUtils).
        setTextIfChanged(state->edit[0], WideFormatIntLabel(state->hue).c_str());
        setTextIfChanged(state->edit[1], WideFormatIntLabel(state->saturation).c_str());
        setTextIfChanged(state->edit[2], WideFormatIntLabel(state->value).c_str());
    }
    // OWN-112: pure percent label for alpha (WideStringUtils).
    setTextIfChanged(state->alphaEdit,
        WideFormatPercentLabel(ScreenshotColorPickerClamp(state->alpha, 0, 100)).c_str());
    state->syncing = false;
    if (invalidateParent) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static void ScreenshotColorPickerApplyEditChange(HWND hwnd, ScreenshotColorPickerDialogState* state, int id) {
    if (!state || state->syncing) return;
    if (id == 4414) {
        state->alpha = ScreenshotColorPickerEditInt(state->alphaEdit, state->alpha, 0, 100);
        ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, false, false, true, false);
        return;
    }
    if (state->mode == 0) {
        COLORREF color = state->color;
        if (ScreenshotColorPickerParseHex(state->edit[0], color)) {
            ScreenshotColorPickerSetColor(state, color);
            ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, true, true, true, false);
        }
        return;
    }
    if (state->mode == 1) {
        int r = ScreenshotColorPickerEditInt(state->edit[0], WideUnpackR(static_cast<unsigned int>(state->color)), 0, 255);
        int g = ScreenshotColorPickerEditInt(state->edit[1], WideUnpackG(static_cast<unsigned int>(state->color)), 0, 255);
        int b = ScreenshotColorPickerEditInt(state->edit[2], WideUnpackB(static_cast<unsigned int>(state->color)), 0, 255);
        ScreenshotColorPickerSetColor(state, RGB(r, g, b));
        ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, true, true, true, false);
        return;
    }
    state->hue = ScreenshotColorPickerEditInt(state->edit[0], state->hue, 0, 360);
    state->saturation = ScreenshotColorPickerEditInt(state->edit[1], state->saturation, 0, 100);
    state->value = ScreenshotColorPickerEditInt(state->edit[2], state->value, 0, 100);
    ScreenshotColorPickerSetFromHsv(state);
    ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, true, true, true, false);
}

static void ScreenshotColorPickerDrawTextCentered(HDC hdc, RECT rc, const wchar_t* text, COLORREF color) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void ScreenshotColorPickerFillRect(HDC hdc, RECT rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
}

static void ScreenshotColorPickerStrokeRect(HDC hdc, RECT rc, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void ScreenshotColorPickerDrawButton(HDC hdc, RECT rc, const wchar_t* text, bool primary, int dpi) {
    COLORREF bg = primary ? RGB(52, 135, 245) : RGB(48, 48, 52);
    COLORREF border = primary ? RGB(52, 135, 245) : RGB(62, 62, 68);
    HBRUSH brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom,
        ScreenshotColorPickerScale(6, dpi),
        ScreenshotColorPickerScale(6, dpi));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    ScreenshotColorPickerDrawTextCentered(hdc, rc, text, RGB(255, 255, 255));
}

static void ScreenshotColorPickerDrawEyeDropper(HDC hdc, RECT rc, bool active, int dpi) {
    ScreenshotColorPickerFillRect(hdc, rc, active ? RGB(54, 68, 86) : RGB(42, 42, 48));
    ScreenshotColorPickerStrokeRect(hdc, rc, active ? RGB(78, 150, 255) : RGB(70, 70, 78));
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    int d = (std::min)(rc.right - rc.left, rc.bottom - rc.top);
    HPEN pen = CreatePen(PS_SOLID, ScreenshotColorPickerScale(2, dpi), RGB(245, 245, 245));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, cx - d / 5, cy + d / 5, nullptr);
    LineTo(hdc, cx + d / 5, cy - d / 5);
    Ellipse(hdc, cx + d / 9, cy - d / 3, cx + d / 3, cy - d / 9);
    MoveToEx(hdc, cx - d / 4, cy + d / 4, nullptr);
    LineTo(hdc, cx - d / 8, cy + d / 3);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static COLORREF ScreenshotColorPickerAlphaBlend(COLORREF fg, COLORREF bg, int alpha) {
    alpha = ScreenshotColorPickerClamp(alpha, 0, 100);
    int inv = 100 - alpha;
    return RGB(
        (WideUnpackR(static_cast<unsigned int>(fg)) * alpha + WideUnpackR(static_cast<unsigned int>(bg)) * inv) / 100,
        (WideUnpackG(static_cast<unsigned int>(fg)) * alpha + WideUnpackG(static_cast<unsigned int>(bg)) * inv) / 100,
        (WideUnpackB(static_cast<unsigned int>(fg)) * alpha + WideUnpackB(static_cast<unsigned int>(bg)) * inv) / 100);
}

static bool ScreenshotColorPickerEnsureSvBitmap(ScreenshotColorPickerDialogState* state, int width, int height) {
    if (!state || width <= 0 || height <= 0) return false;
    if (state->svBitmap &&
        state->svBitmapW == width &&
        state->svBitmapH == height &&
        state->svBitmapHue == state->hue) {
        return true;
    }

    DWORD* pixels = nullptr;
    HBITMAP bitmap = ScreenshotColorPickerCreateDib(width, height, &pixels);
    if (!bitmap || !pixels) {
        ScreenshotColorPickerDeleteBitmap(bitmap);
        return false;
    }

    int denomX = (std::max)(1, width - 1);
    int denomY = (std::max)(1, height - 1);
    for (int y = 0; y < height; ++y) {
        int value = 100 - MulDiv(y, 100, denomY);
        DWORD* row = pixels + y * width;
        for (int x = 0; x < width; ++x) {
            int sat = MulDiv(x, 100, denomX);
            row[x] = ScreenshotColorPickerDibPixel(ScreenshotHsvToRgbLocal(state->hue, sat, value));
        }
    }

    ScreenshotColorPickerDeleteBitmap(state->svBitmap);
    state->svBitmap = bitmap;
    state->svBitmapW = width;
    state->svBitmapH = height;
    state->svBitmapHue = state->hue;
    return true;
}

static bool ScreenshotColorPickerEnsureHueBitmap(ScreenshotColorPickerDialogState* state, int width, int height) {
    if (!state || width <= 0 || height <= 0) return false;
    if (state->hueBitmap &&
        state->hueBitmapW == width &&
        state->hueBitmapH == height) {
        return true;
    }

    DWORD* pixels = nullptr;
    HBITMAP bitmap = ScreenshotColorPickerCreateDib(width, height, &pixels);
    if (!bitmap || !pixels) {
        ScreenshotColorPickerDeleteBitmap(bitmap);
        return false;
    }

    int denomX = (std::max)(1, width - 1);
    for (int x = 0; x < width; ++x) {
        int hue = MulDiv(x, 359, denomX);
        DWORD pixel = ScreenshotColorPickerDibPixel(ScreenshotHsvToRgbLocal(hue, 100, 100));
        for (int y = 0; y < height; ++y) {
            pixels[y * width + x] = pixel;
        }
    }

    ScreenshotColorPickerDeleteBitmap(state->hueBitmap);
    state->hueBitmap = bitmap;
    state->hueBitmapW = width;
    state->hueBitmapH = height;
    return true;
}

// Build/cache the alpha gradient bar with a checkerboard background.
// NOTE: intentionally different from the inline color picker in
// OverlayWindowScreenshot.ToolbarRender.inl — see the comment there for
// why the inline version uses white background and 0-255 alpha.
static bool ScreenshotColorPickerEnsureAlphaBitmap(ScreenshotColorPickerDialogState* state, int width, int height) {
    if (!state || width <= 0 || height <= 0) return false;
    if (state->alphaBitmap &&
        state->alphaBitmapW == width &&
        state->alphaBitmapH == height &&
        state->alphaBitmapColor == state->color &&
        state->alphaBitmapDpi == state->dpi) {
        return true;
    }

    DWORD* pixels = nullptr;
    HBITMAP bitmap = ScreenshotColorPickerCreateDib(width, height, &pixels);
    if (!bitmap || !pixels) {
        ScreenshotColorPickerDeleteBitmap(bitmap);
        return false;
    }

    int checker = (std::max)(1, ScreenshotColorPickerScale(4, state->dpi));
    int denomX = (std::max)(1, width - 1);
    for (int y = 0; y < height; ++y) {
        DWORD* row = pixels + y * width;
        for (int x = 0; x < width; ++x) {
            bool dark = ((x / checker) + (y / checker)) % 2 == 0;
            COLORREF base = dark ? RGB(196, 196, 196) : RGB(238, 238, 238);
            int alpha = MulDiv(x, 100, denomX);
            row[x] = ScreenshotColorPickerDibPixel(ScreenshotColorPickerAlphaBlend(state->color, base, alpha));
        }
    }

    ScreenshotColorPickerDeleteBitmap(state->alphaBitmap);
    state->alphaBitmap = bitmap;
    state->alphaBitmapW = width;
    state->alphaBitmapH = height;
    state->alphaBitmapColor = state->color;
    state->alphaBitmapDpi = state->dpi;
    return true;
}

static void ScreenshotColorPickerDrawSliderKnob(HDC hdc, int x, int y, int dpi) {
    int r = ScreenshotColorPickerScale(7, dpi);
    HPEN pen = CreatePen(PS_SOLID, ScreenshotColorPickerScale(2, dpi), RGB(255, 255, 255));
    HBRUSH brush = CreateSolidBrush(RGB(70, 70, 76));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    Ellipse(hdc, x - r, y - r, x + r, y + r);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void ScreenshotColorPickerDrawContent(HWND hwnd, HDC hdc, ScreenshotColorPickerDialogState* state) {
    if (!state) return;
    RECT client = {};
    GetClientRect(hwnd, &client);
    ScreenshotColorPickerFillRect(hdc, client, RGB(31, 31, 35));
    ScreenshotColorPickerDialogLayout l = ScreenshotColorPickerGetLayout(*state);

    int svW = l.sv.right - l.sv.left;
    int svH = l.sv.bottom - l.sv.top;
    if (ScreenshotColorPickerEnsureSvBitmap(state, svW, svH)) {
        ScreenshotColorPickerDrawBitmap(hdc, state->svBitmap, l.sv);
    }
    ScreenshotColorPickerStrokeRect(hdc, l.sv, RGB(14, 14, 16));
    int svX = l.sv.left + MulDiv(state->saturation, svW, 100);
    int svY = l.sv.top + MulDiv(100 - state->value, svH, 100);
    ScreenshotColorPickerDrawSliderKnob(hdc, svX, svY, state->dpi);

    ScreenshotColorPickerDrawEyeDropper(hdc, l.eye, state->picking, state->dpi);

    int hueW = l.hue.right - l.hue.left;
    int hueH = l.hue.bottom - l.hue.top;
    if (ScreenshotColorPickerEnsureHueBitmap(state, hueW, hueH)) {
        ScreenshotColorPickerDrawBitmap(hdc, state->hueBitmap, l.hue);
    }
    int hueX = l.hue.left + MulDiv(state->hue, hueW, 359);
    ScreenshotColorPickerDrawSliderKnob(hdc, hueX, (l.hue.top + l.hue.bottom) / 2, state->dpi);

    int aW = l.alpha.right - l.alpha.left;
    int aH = l.alpha.bottom - l.alpha.top;
    if (ScreenshotColorPickerEnsureAlphaBitmap(state, aW, aH)) {
        ScreenshotColorPickerDrawBitmap(hdc, state->alphaBitmap, l.alpha);
    }
    int alphaX = l.alpha.left + MulDiv(state->alpha, aW, 100);
    ScreenshotColorPickerDrawSliderKnob(hdc, alphaX, (l.alpha.top + l.alpha.bottom) / 2, state->dpi);

    ScreenshotColorPickerFillRect(hdc, l.separator, RGB(55, 55, 60));
    ScreenshotColorPickerDrawButton(hdc, l.ok, L"\x786e\x8ba4", true, state->dpi);
    ScreenshotColorPickerDrawButton(hdc, l.cancel, L"\x53d6\x6d88", false, state->dpi);
}

static void ScreenshotColorPickerUpdateFromPoint(HWND hwnd, ScreenshotColorPickerDialogState* state, POINT pt, int part, bool syncFields) {
    if (!state) return;
    COLORREF oldColor = state->color;
    int oldAlpha = state->alpha;
    int oldHue = state->hue;
    int oldSaturation = state->saturation;
    int oldValue = state->value;

    ScreenshotColorPickerDialogLayout l = ScreenshotColorPickerGetLayout(*state);
    if (part == 1) {
        int w = (std::max)(1, (int)(l.sv.right - l.sv.left));
        int h = (std::max)(1, (int)(l.sv.bottom - l.sv.top));
        int x = ScreenshotColorPickerClamp(pt.x, l.sv.left, l.sv.right);
        int y = ScreenshotColorPickerClamp(pt.y, l.sv.top, l.sv.bottom);
        state->saturation = MulDiv(x - l.sv.left, 100, w);
        state->value = 100 - MulDiv(y - l.sv.top, 100, h);
        ScreenshotColorPickerSetFromHsv(state);
    } else if (part == 2) {
        int w = (std::max)(1, (int)(l.hue.right - l.hue.left));
        int x = ScreenshotColorPickerClamp(pt.x, l.hue.left, l.hue.right);
        state->hue = MulDiv(x - l.hue.left, 359, w);
        ScreenshotColorPickerSetFromHsv(state);
    } else if (part == 3) {
        int w = (std::max)(1, (int)(l.alpha.right - l.alpha.left));
        int x = ScreenshotColorPickerClamp(pt.x, l.alpha.left, l.alpha.right);
        state->alpha = MulDiv(x - l.alpha.left, 100, w);
    }

    bool changed = oldColor != state->color ||
        oldAlpha != state->alpha ||
        oldHue != state->hue ||
        oldSaturation != state->saturation ||
        oldValue != state->value;
    if (!changed) return;

    if (syncFields) {
        ScreenshotColorPickerSyncFields(hwnd, state);
    } else if (ScreenshotColorPickerShouldRedrawDrag(state, 40)) {
        ScreenshotColorPickerInvalidateDragPart(hwnd, state, part);
    }
}

static void ScreenshotColorPickerSampleScreen(HWND hwnd, ScreenshotColorPickerDialogState* state, bool syncFields) {
    if (!state || !state->picking) return;
    POINT pt = {};
    GetCursorPos(&pt);
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    COLORREF color = GetPixel(screen, pt.x, pt.y);
    ReleaseDC(nullptr, screen);
    if (color != CLR_INVALID && color != state->color) {
        ScreenshotColorPickerSetColor(state, color);
        if (syncFields) {
            ScreenshotColorPickerSyncFields(hwnd, state);
        } else {
            ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, true, true, true, false);
        }
    } else if (syncFields) {
        ScreenshotColorPickerSyncFields(hwnd, state);
    }
}

static void ScreenshotColorPickerBeginPick(HWND hwnd, ScreenshotColorPickerDialogState* state) {
    if (!state || state->picking) return;
    state->beforePickColor = state->color;
    state->beforePickAlpha = state->alpha;
    state->picking = true;
    SetCapture(hwnd);
    SetFocus(hwnd);
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    SetTimer(hwnd, 1, 100, nullptr);
    ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, false, false, false, true);
}

static void ScreenshotColorPickerEndPick(HWND hwnd, ScreenshotColorPickerDialogState* state, bool accept) {
    if (!state || !state->picking) return;
    KillTimer(hwnd, 1);
    if (!accept) {
        ScreenshotColorPickerSetColor(state, state->beforePickColor);
        state->alpha = state->beforePickAlpha;
        ScreenshotColorPickerSyncFields(hwnd, state, false);
    } else {
        ScreenshotColorPickerSyncFields(hwnd, state, false);
    }
    state->picking = false;
    ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    ScreenshotColorPickerInvalidateInteractiveParts(hwnd, state, true, true, true, true);
}

static LRESULT CALLBACK ScreenshotColorPickerDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    constexpr int kComboId = 4410;
    constexpr int kEdit0Id = 4411;
    constexpr int kEdit1Id = 4412;
    constexpr int kEdit2Id = 4413;
    constexpr int kAlphaEditId = 4414;

    auto* state = (ScreenshotColorPickerDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = (CREATESTRUCTW*)lParam;
        state = (ScreenshotColorPickerDialogState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    }
    case WM_CREATE: {
        if (!state) return -1;
        ScreenshotColorPickerApplyDarkTitlebar(hwnd);
        state->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        state->bgBrush = CreateSolidBrush(RGB(31, 31, 35));
        state->editBrush = CreateSolidBrush(RGB(42, 42, 46));

        ScreenshotColorPickerDialogLayout l = ScreenshotColorPickerGetLayout(*state);
        state->combo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            l.combo.left, l.combo.top, l.combo.right - l.combo.left, ScreenshotColorPickerScale(160, state->dpi),
            hwnd, (HMENU)(INT_PTR)kComboId, GetModuleHandleW(nullptr), nullptr);
        ScreenshotColorPickerSetChildFont(state->combo, state->font);
        SendMessageW(state->combo, CB_ADDSTRING, 0, (LPARAM)L"Hex");
        SendMessageW(state->combo, CB_ADDSTRING, 0, (LPARAM)L"RGB");
        SendMessageW(state->combo, CB_ADDSTRING, 0, (LPARAM)L"HSV");

        int ids[3] = { kEdit0Id, kEdit1Id, kEdit2Id };
        for (int i = 0; i < 3; ++i) {
            state->edit[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER,
                l.edit[i].left, l.edit[i].top, l.edit[i].right - l.edit[i].left, l.edit[i].bottom - l.edit[i].top,
                hwnd, (HMENU)(INT_PTR)ids[i], GetModuleHandleW(nullptr), nullptr);
            ScreenshotColorPickerSetChildFont(state->edit[i], state->font);
        }
        state->alphaEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER,
            l.alphaEdit.left, l.alphaEdit.top, l.alphaEdit.right - l.alphaEdit.left, l.alphaEdit.bottom - l.alphaEdit.top,
            hwnd, (HMENU)(INT_PTR)kAlphaEditId, GetModuleHandleW(nullptr), nullptr);
        ScreenshotColorPickerSetChildFont(state->alphaEdit, state->font);
        ScreenshotColorPickerSyncFields(hwnd, state);
        return 0;
    }
    case WM_SYSCOMMAND: {
        UINT cmd = (UINT)(wParam & 0xfff0);
        if (cmd == SC_SIZE) return 0;
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
        if (state) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(245, 245, 245));
            SetBkColor(hdc, RGB(42, 42, 46));
            return (LRESULT)state->editBrush;
        }
        break;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notify = HIWORD(wParam);
        if (!state) break;
        if (id == kComboId && notify == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(state->combo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel <= 2) {
                state->mode = sel;
                ScreenshotColorPickerSyncFields(hwnd, state);
            }
            return 0;
        }
        if ((id == kEdit0Id || id == kEdit1Id || id == kEdit2Id || id == kAlphaEditId) &&
            notify == EN_CHANGE) {
            ScreenshotColorPickerApplyEditChange(hwnd, state, id);
            return 0;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (!state) break;
        if (state->picking) {
            ScreenshotColorPickerSampleScreen(hwnd, state, false);
            ScreenshotColorPickerEndPick(hwnd, state, true);
            return 0;
        }
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenshotColorPickerDialogLayout l = ScreenshotColorPickerGetLayout(*state);
        if (PtInRect(&l.ok, pt)) {
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (PtInRect(&l.cancel, pt)) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (PtInRect(&l.eye, pt)) {
            ScreenshotColorPickerBeginPick(hwnd, state);
            return 0;
        }
        if (PtInRect(&l.sv, pt)) state->dragPart = 1;
        else if (PtInRect(&l.hue, pt)) state->dragPart = 2;
        else if (PtInRect(&l.alpha, pt)) state->dragPart = 3;
        if (state->dragPart != 0) {
            SetCapture(hwnd);
            state->lastDragRedrawTick = 0;
            ScreenshotColorPickerUpdateFromPoint(hwnd, state, pt, state->dragPart, false);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (state && state->dragPart != 0) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenshotColorPickerUpdateFromPoint(hwnd, state, pt, state->dragPart, false);
            return 0;
        }
        if (state && state->picking) {
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        }
        break;
    case WM_LBUTTONUP:
        if (state && state->dragPart != 0) {
            int part = state->dragPart;
            state->dragPart = 0;
            ReleaseCapture();
            state->lastDragRedrawTick = 0;
            ScreenshotColorPickerInvalidateDragPart(hwnd, state, part);
            ScreenshotColorPickerSyncFields(hwnd, state, false);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == 1 && state && state->picking) {
            ScreenshotColorPickerSampleScreen(hwnd, state, false);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (state && state->picking && wParam == VK_ESCAPE) {
            ScreenshotColorPickerEndPick(hwnd, state, false);
            return 0;
        }
        if (state && state->picking && wParam == VK_SPACE) {
            ScreenshotColorPickerEndPick(hwnd, state, true);
            return 0;
        }
        if (wParam == VK_RETURN && state) {
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client = {};
        GetClientRect(hwnd, &client);
        int clientW = client.right - client.left;
        int clientH = client.bottom - client.top;
        HDC mem = clientW > 0 && clientH > 0 ? CreateCompatibleDC(hdc) : nullptr;
        HBITMAP buffer = mem ? CreateCompatibleBitmap(hdc, clientW, clientH) : nullptr;
        if (mem && buffer) {
            HGDIOBJ oldBitmap = SelectObject(mem, buffer);
            HFONT oldFont = state && state->font ? (HFONT)SelectObject(mem, state->font) : nullptr;
            ScreenshotColorPickerDrawContent(hwnd, mem, state);
            BitBlt(hdc,
                ps.rcPaint.left,
                ps.rcPaint.top,
                ps.rcPaint.right - ps.rcPaint.left,
                ps.rcPaint.bottom - ps.rcPaint.top,
                mem,
                ps.rcPaint.left,
                ps.rcPaint.top,
                SRCCOPY);
            if (oldFont) SelectObject(mem, oldFont);
            SelectObject(mem, oldBitmap);
        } else {
            HFONT oldFont = state && state->font ? (HFONT)SelectObject(hdc, state->font) : nullptr;
            ScreenshotColorPickerDrawContent(hwnd, hdc, state);
            if (oldFont) SelectObject(hdc, oldFont);
        }
        if (buffer) DeleteObject(buffer);
        if (mem) DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (state) {
            if (state->picking) {
                KillTimer(hwnd, 1);
                ReleaseCapture();
            }
            if (state->bgBrush) {
                DeleteObject(state->bgBrush);
                state->bgBrush = nullptr;
            }
            if (state->editBrush) {
                DeleteObject(state->editBrush);
                state->editBrush = nullptr;
            }
            ScreenshotColorPickerDeleteBitmap(state->svBitmap);
            ScreenshotColorPickerDeleteBitmap(state->hueBitmap);
            ScreenshotColorPickerDeleteBitmap(state->alphaBitmap);
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// S-H-CLOSE-6: external linkage so ToolbarInteraction real TU can call this free helper.
bool ShowScreenshotColorPickerDialog(HWND owner, COLORREF& color, int& alpha, int& mode) {
    std::call_once(s_screenshotColorPickerDialogClassReg, []() {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.hInstance = GetModuleHandleW(nullptr);
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = nullptr;
        wcex.lpszClassName = kScreenshotColorPickerDialogClass;
        wcex.lpfnWndProc = ScreenshotColorPickerDialogProc;
        wcex.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcex.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wcex);
    });

    ScreenshotColorPickerDialogState state;
    state.color = color;
    state.alpha = ScreenshotColorPickerClamp(alpha, 0, 100);
    state.mode = ScreenshotColorPickerClamp(mode, 0, 2);
    state.dpi = owner ? (int)GetDpiForWindow(owner) : 96;
    ScreenshotRgbToHsvLocal(state.color, state.hue, state.saturation, state.value);

    int clientW = ScreenshotColorPickerScale(260, state.dpi);
    int clientH = ScreenshotColorPickerScale(370, state.dpi);
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    RECT windowRect = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
    int windowW = windowRect.right - windowRect.left;
    int windowH = windowRect.bottom - windowRect.top;

    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    state.initialX = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - windowW) / 2;
    state.initialY = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - windowH) / 2;

    HWND dialog = CreateWindowExW(exStyle,
        kScreenshotColorPickerDialogClass,
        L"\x9009\x62e9\x989c\x8272",
        style,
        state.initialX, state.initialY, windowW, windowH,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) return false;

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg;
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    mode = ScreenshotColorPickerClamp(state.mode, 0, 2);
    if (state.accepted) {
        color = state.color;
        alpha = ScreenshotColorPickerClamp(state.alpha, 0, 100);
    }
    return state.accepted;
}
