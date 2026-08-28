#pragma once

#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/annotation/AnnotationDocumentTextStyle.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h"

#include <algorithm>

// S-D/S-F-CLOSE-6: sole Text non-edit product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawTextAnnotationLocal.
// Preview/Export pass localRect already mapped to target HDC coords.
// Editing overlays use ScreenshotAnnotationRenderTextEditingLocal below.
// emptyPlaceholder: text shown when ann.text empty and not editing (e.g. L"Text").
// fallbackFontFamily: Host tool style when Document fontFamily missing.

inline RECT ScreenshotAnnotationRenderTextLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    RECT localRect,
    const std::wstring& emptyPlaceholder,
    const std::wstring& fallbackFontFamily)
{
    if (ann.type != ScreenshotToolbarCommand::ToolText) {
        return localRect;
    }
    const auto style = ScreenshotAnnotationResolveTextDrawStyle(document, ann);
    const int fontSize = style.fontSize > 0
        ? style.fontSize
        : TextAnnotationFontSizeLocal(ann);
    const std::wstring fontFamily = !style.fontFamily.empty()
        ? style.fontFamily
        : (fallbackFontFamily.empty() ? L"Microsoft YaHei" : fallbackFontFamily);
    const std::wstring visibleText =
        ann.text.empty() ? emptyPlaceholder : ann.text;
    const COLORREF textColor = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const double angle =
        IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    return ScreenshotDrawTextAnnotationLocal(
        hdc,
        localRect,
        visibleText,
        textColor,
        fontFamily,
        fontSize,
        style.bold,
        style.italics,
        style.background,
        style.backgroundColor,
        style.backgroundOpacity,
        style.backgroundPadding,
        style.backgroundRounded,
        style.outline,
        style.outlineColor,
        style.outlineSize,
        HasExplicitRectExtentLocal(ann),
        angle);
}

// S-H: Preview-only text editing overlay; caller supplies local geometry + immutable caret/selection.
inline void ScreenshotAnnotationRenderTextEditingLocal(
    HDC hdc, DWORD* pixels, int bitmapWidth, int bitmapHeight,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    RECT localRect,
    const std::wstring& fallbackFontFamily,
    int selectionAnchor, int caretIndex)
{
    const std::wstring fallbackFont = fallbackFontFamily.empty() ? L"Microsoft YaHei" : fallbackFontFamily;
    const RECT textRect = ScreenshotAnnotationRenderTextLocal(hdc, document, ann, localRect, L"", fallbackFont);

    const auto style = ScreenshotAnnotationResolveTextDrawStyle(document, ann);
    const int fontSize = style.fontSize > 0 ? style.fontSize : TextAnnotationFontSizeLocal(ann);
    const std::wstring fontFamily = !style.fontFamily.empty() ? style.fontFamily : fallbackFont;
    const COLORREF textColor = style.usesCustomColor ? style.customColor : ScreenshotPresetColorLocal(style.colorIndex);
    const double angle = IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;

    HFONT font = CreateFontW(
        -fontSize, 0, 0, 0, style.bold ? FW_SEMIBOLD : FW_NORMAL,
        style.italics ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, fontFamily.c_str());
    HFONT oldFont = font ? static_cast<HFONT>(SelectObject(hdc, font)) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    const int savedDc = SaveDC(hdc);
    ScreenshotApplyHdcRectRotationLocal(hdc, localRect, angle);

    int selectionStart = 0;
    int selectionEnd = 0;
    if (ScreenshotTextSelectionRangeLocal(
            selectionAnchor,
            caretIndex,
            static_cast<int>(ann.text.size()),
            selectionStart,
            selectionEnd)) {
        SIZE beforeSize = {};
        SIZE selectedSize = {};
        if (selectionStart > 0) {
            GetTextExtentPoint32W(hdc, ann.text.c_str(), selectionStart, &beforeSize);
        }
        if (selectionEnd > selectionStart) {
            GetTextExtentPoint32W(
                hdc,
                ann.text.c_str() + selectionStart,
                selectionEnd - selectionStart,
                &selectedSize);
        }
        const RECT selectionRect = {
            textRect.left + static_cast<int>(beforeSize.cx),
            textRect.top + 1,
            textRect.left + static_cast<int>(beforeSize.cx) +
                (std::max)(1, static_cast<int>(selectedSize.cx)),
            textRect.top + fontSize + 4
        };
        HBRUSH selectionBrush = CreateSolidBrush(RGB(0, 120, 215));
        if (selectionBrush) {
            FillRect(hdc, &selectionRect, selectionBrush);
            DeleteObject(selectionBrush);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT selectedTextRect = textRect;
            selectedTextRect.left += static_cast<int>(beforeSize.cx);
            DrawTextW(
                hdc,
                ann.text.c_str() + selectionStart,
                selectionEnd - selectionStart,
                &selectedTextRect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
            SetTextColor(hdc, textColor);
        }
    }

    SIZE textSize = {};
    const int clampedCaretIndex = (std::min)(
        (std::max)(caretIndex, 0), static_cast<int>(ann.text.size()));
    if (clampedCaretIndex > 0) {
        GetTextExtentPoint32W(hdc, ann.text.c_str(), clampedCaretIndex, &textSize);
    }
    const int caretX = textRect.left + static_cast<int>(textSize.cx) + 1;
    POINT caretTop = { caretX, textRect.top + 2 };
    POINT caretBottom = { caretX, textRect.top + fontSize + 2 };
    if (IsRotatableGeometryScreenshotAnnotationLocal(ann) &&
        !IsZeroAngleLocal(ann.angle)) {
        const POINT center = ScreenshotAnnotationRectCenter(localRect);
        caretTop = RotatePointAroundCenterLocal(caretTop, center, ann.angle);
        caretBottom = RotatePointAroundCenterLocal(caretBottom, center, ann.angle);
    }
    DrawLinePixelsLocal(
        pixels,
        bitmapWidth,
        bitmapHeight,
        caretTop.x,
        caretTop.y,
        caretBottom.x,
        caretBottom.y,
        PixelRgbLocal(GetRValue(textColor), GetGValue(textColor), GetBValue(textColor)),
        1);
    RestoreDC(hdc, savedDc);
    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
}
