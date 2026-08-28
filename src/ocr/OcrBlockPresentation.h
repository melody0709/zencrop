#pragma once

// Shared, pure presentation policy for OCR layout blocks.
// Used by Dashboard Canvas paint and Batch layout-preview export so live and
// derived overlays stay visually consistent. No HWND / Settings / engine route.

#include "OcrBlock.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
#include "core/WideStringUtils.h"

namespace OcrBlockPresentation {

inline std::wstring NormalizeLabel(const std::wstring& label) {
    if (label.empty()) return L"text";
    std::wstring out = label;
    out = WideNormalizeLabelToken(std::move(out)); // OWN-79
    return out.empty() ? L"text" : out;
}

// True when every block is a PP-OCRv6 text line. Mixed / Cloud / Doc snapshots
// stay on semantic-layout presentation even if all labels happen to be "text".
template <typename Block>
inline bool IsTextLineMode(const std::vector<Block>& blocks) {
    if (blocks.empty()) return false;
    if constexpr (!(requires(const Block& b) { b.source; b.label; })) {
        return false;
    } else {
        for (const auto& block : blocks) {
            if (block.source != L"ppocrv6_onnx") return false;
            if (NormalizeLabel(block.label) != L"text") return false;
        }
        return true;
    }
}

// Fill alpha for live Canvas (0..255). Semantic layout keeps historical values.
struct FillAlphas {
    BYTE normal = 58;
    BYTE hover = 104;
    BYTE selected = 124;
    BYTE groupSibling = 48;
    BYTE focusDimmed = 26;
};

inline FillAlphas CanvasFillAlphas(bool textLineMode) {
    if (!textLineMode) return {};
    FillAlphas a;
    a.normal = 0;
    a.hover = 60;
    a.selected = 90;
    a.groupSibling = 24;
    a.focusDimmed = 0;
    return a;
}

// Order badge for per-block corner markers. Semantic always (size-gated by
// caller). TextLine only on hover/selected — Reading Order mode should use the
// dedicated center-badge overlay, not corner badges on every line.
inline bool ShowOrderBadge(bool textLineMode, bool hovered, bool selected, bool readingOrderMode) {
    if (!textLineMode) return true;
    (void)readingOrderMode; // kept for API stability; callers should pass false for TextLine RO
    return hovered || selected;
}

// Layout-preview export fill alpha (static image, no hover).
inline BYTE LayoutPreviewFillAlpha(bool textLineMode) {
    return textLineMode ? BYTE{0} : BYTE{56};
}

// Layout-preview order badge: semantic keeps top-left badges; TextLine omits them
// so dense boarding-pass exports stay readable (matches live Canvas normal state).
inline bool LayoutPreviewDrawOrderBadge(bool textLineMode) {
    return !textLineMode;
}

// TextLine skips bbox-overlap quality issues; other issues stay engine-neutral.
inline bool SkipBboxOverlapIssue(bool textLineMode) {
    return textLineMode;
}

} // namespace OcrBlockPresentation
