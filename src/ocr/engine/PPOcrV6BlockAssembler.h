#pragma once

// Pure PP-OCRv6 text-line block assembly contract.
// No ONNX Runtime, UI, file I/O, or model dependencies. Safe for hermetic tests.

#include "OcrBlock.h"
#include "core/WideStringUtils.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace PPOcrV6Blocks {

// Geometry for one detection box already mapped to original image coordinates.
struct DetBoxGeometry {
    RECT rect = {};
    std::vector<OcrBlockPoint> points;
    float detScore = 0.0f;
};

// One recognition line that already passed the unified accept gate
// (!text.empty() && recScore >= recScoreThresh) and carries its source box identity.
struct AcceptedLine {
    size_t sourceBoxIndex = static_cast<size_t>(-1);
    std::wstring text;
    float recScore = 0.0f;
};

// Assembled plain OCR output fields (subset of OcrOutput used by PP-OCRv6).
struct AssembledOutput {
    std::wstring text;
    std::vector<OcrLayoutBlock> blocks;
    std::vector<RECT> bboxes;
    std::vector<std::wstring> bboxClasses;
};

// Production text join for PP-OCRv6 plain OCR lines (Windows CRLF).
// Tests must reuse this helper (or the same \r\n contract) when rebuilding text.
inline std::wstring JoinRecognizedLines(const std::vector<std::wstring>& lines) {
    std::wstring text;
    for (const auto& line : lines) {
        if (line.empty()) continue;
        if (!text.empty()) text += L"\r\n";
        text += line;
    }
    return text;
}

inline bool IsFinitePoint(const OcrBlockPoint& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

inline bool BboxLooksValid(const RECT& r) {
    const LONG w = r.right - r.left;
    const LONG h = r.bottom - r.top;
    return w > 1 && h > 1;
}

// Returns true when the polygon is usable by GDI+ as-is.
// Contract: any non-finite point OR fewer than 3 points → clear entire polygon
// (bbox fallback). Never drop bad vertices and keep a reshaped remainder
// (e.g. 1 NaN + 3 finite must not become a wrong triangle).
inline bool SanitizePolygon(std::vector<OcrBlockPoint>& polygon) {
    if (polygon.size() < 3) {
        polygon.clear();
        return false;
    }
    for (const auto& p : polygon) {
        if (!IsFinitePoint(p)) {
            polygon.clear();
            return false;
        }
    }
    return true;
}

// Unified accept gate for recognition results. Single source of truth for
// whether a line enters text/blocks/bboxes/classes/recCount.
inline bool IsAcceptedRecognition(const std::wstring& text, float recScore, float recScoreThresh) {
    return !text.empty() && recScore >= recScoreThresh;
}

// Assemble canonical text + blocks + bboxes + bboxClasses from sorted
// DetBoxGeometry and the accepted recognition sequence.
//
// Contract:
// - Does not write out until every accepted line is validated.
// - On any invalid/duplicate sourceBoxIndex or invalid bbox, leaves out unchanged and returns false.
// - Invalid polygon is cleared (bbox-only fallback); valid bbox still accepted.
// - order is dense 1..N in accepted order; id uses sourceBoxIndex (output-local stable).
// - confidence is recognition score only.
// - label is always "text"; source is always "ppocrv6_onnx".
// - out.text uses JoinRecognizedLines (CRLF).
// - sourceBoxIndex must be unique within this accepted sequence (no duplicate block IDs).
inline bool AssemblePPOcrV6Output(
    const std::vector<DetBoxGeometry>& boxes,
    const std::vector<AcceptedLine>& acceptedLines,
    AssembledOutput& out,
    int pageIndex = 0)
{
    // Pre-validate identity + geometry so we never leave a half-written result.
    // Track seen source indexes with a simple bitmap for O(N) uniqueness.
    std::vector<unsigned char> seenIndex(boxes.size(), 0);
    for (const auto& line : acceptedLines) {
        if (line.sourceBoxIndex >= boxes.size()) return false;
        if (line.text.empty()) return false;
        if (!BboxLooksValid(boxes[line.sourceBoxIndex].rect)) return false;
        if (seenIndex[line.sourceBoxIndex]) return false; // duplicate identity
        seenIndex[line.sourceBoxIndex] = 1;
    }

    AssembledOutput next;
    next.blocks.reserve(acceptedLines.size());
    next.bboxes.reserve(acceptedLines.size());
    next.bboxClasses.reserve(acceptedLines.size());
    std::vector<std::wstring> lines;
    lines.reserve(acceptedLines.size());

    for (size_t i = 0; i < acceptedLines.size(); ++i) {
        const auto& line = acceptedLines[i];
        const auto& box = boxes[line.sourceBoxIndex];

        OcrLayoutBlock block;
        // OWN-126: pure page-kind-order id (WideStringUtils).
        block.id = WideFormatPageKindOrderId(
            pageIndex + 1, L"ppocrv6_line", static_cast<int>(line.sourceBoxIndex + 1));
        block.pageIndex = pageIndex;
        block.order = static_cast<int>(i) + 1;
        block.label = L"text";
        block.content = line.text;
        block.bbox = box.rect;
        block.polygon = box.points;
        SanitizePolygon(block.polygon);
        block.confidence = static_cast<double>(line.recScore);
        block.source = L"ppocrv6_onnx";
        block.groupId.clear();
        block.edited = false;
        block.editBaseline.reset();

        next.bboxes.push_back(block.bbox);
        next.bboxClasses.push_back(L"text");
        lines.push_back(line.text);
        next.blocks.push_back(std::move(block));
    }

    next.text = JoinRecognizedLines(lines);
    out = std::move(next);
    return true;
}

// Helpers for batch identity decisions (no model I/O).

// True when a successful batch recognition may be accepted as a whole.
inline bool BatchCountsMatch(size_t inputCount, size_t resultCount) {
    return inputCount == resultCount;
}

// Single-image fallback may only accept exactly one result.
inline bool SingleFallbackCountOk(size_t resultCount) {
    return resultCount == 1;
}

} // namespace PPOcrV6Blocks
