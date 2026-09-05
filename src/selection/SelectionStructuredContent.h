#pragma once

#include "SelectionTypes.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace selection {

inline constexpr std::size_t kMaxSelectionHtmlBytes = 1024 * 1024;
inline constexpr std::size_t kMaxStructuredSelectionPlanChars = 1024 * 1024;

struct CfHtmlSelection {
    std::wstring markedHtml;
    std::wstring sourceUrl;
};

struct StructuredSelectionPart {
    std::wstring literal;
    std::wstring segmentId;
};

enum class StructuredSelectionProjection {
    Raw,
    Markdown,
    MarkdownTableCell,
    HtmlText,
};

struct StructuredSelectionLeaf {
    std::wstring id;
    std::wstring blockId;
    std::wstring text;
    StructuredSelectionProjection projection =
        StructuredSelectionProjection::Raw;
};

// A parsed plan is always retained through shared_ptr<const ...> by the
// workflow owner. Source and translated Markdown are projections of these
// same parts; provider output never becomes a second document authority.
struct StructuredSelectionPlan {
    int version = 1;
    uint64_t requestGeneration = 0;
    SelectionContentKind sourceKind = SelectionContentKind::Plain;
    SelectionFidelity fidelity = SelectionFidelity::Plain;
    std::wstring requestToken;
    std::wstring sourceMarkdown;
    std::vector<StructuredSelectionPart> parts;
    std::vector<StructuredSelectionLeaf> leaves;
};

std::wstring MakeSelectionRequestToken();
std::wstring BuildCodeSelectionMarkdown(
    const std::wstring& text, const std::wstring& language);
bool ParseCfHtmlSelection(
    const std::string& bytes,
    const std::wstring& requestToken,
    CfHtmlSelection& selection,
    std::wstring* diagnostic = nullptr);
bool ParseStructuredSelectionPlan(
    const std::wstring& planJson,
    const std::wstring& expectedToken,
    uint64_t expectedGeneration,
    SelectionContentKind sourceKind,
    SelectionFidelity fidelity,
    StructuredSelectionPlan& plan,
    std::wstring* diagnostic = nullptr);
std::wstring ProjectStructuredSelection(
    const StructuredSelectionPlan& plan,
    const std::unordered_map<std::wstring, std::wstring>& translations);

} // namespace selection
