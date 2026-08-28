#include "OcrDocumentAlignment.h"

#include "Sha256.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

// OWN-78: thin wrapper over pure WideStringUtils.
std::wstring NormalizeLabel(std::wstring label) {
    return WideNormalizeLabelToken(std::move(label));
}

bool IsLayoutOnlyLabel(const std::wstring& label) {
    static const std::set<std::wstring> kLabels = {
        L"header", L"footer", L"page_number", L"header_image", L"footer_image"
    };
    return kLabels.find(NormalizeLabel(label)) != kLabels.end();
}

bool IsImageLabel(const std::wstring& label) {
    const std::wstring normalized = NormalizeLabel(label);
    return normalized == L"image" || normalized == L"chart" ||
        normalized == L"seal" || normalized == L"figure";
}

bool IsFormulaLabel(const std::wstring& label) {
    const std::wstring normalized = NormalizeLabel(label);
    return normalized == L"formula" || normalized == L"display_formula" ||
        normalized == L"inline_formula" || normalized == L"equation";
}

bool IsFormulaNumberLabel(const std::wstring& label) {
    const std::wstring normalized = NormalizeLabel(label);
    return normalized == L"formula_number" || normalized == L"equation_number" ||
        normalized == L"eq_number" || normalized == L"formula_no" ||
        normalized == L"equation_no";
}

struct SourceSpan {
    size_t start = 0;
    size_t end = 0;
};

std::vector<SourceSpan> FindMarkdownImageSpans(const std::wstring& source) {
    std::vector<SourceSpan> spans;
    size_t cursor = 0;
    while (cursor < source.size()) {
        size_t markdownImage = source.find(L"![", cursor);
        size_t htmlImage = source.find(L"<img", cursor);
        size_t start = std::wstring::npos;
        bool html = false;
        if (markdownImage != std::wstring::npos &&
            (htmlImage == std::wstring::npos || markdownImage < htmlImage)) {
            start = markdownImage;
        } else if (htmlImage != std::wstring::npos) {
            start = htmlImage;
            html = true;
        }
        if (start == std::wstring::npos) break;

        size_t end = std::wstring::npos;
        if (html) {
            end = source.find(L'>', start + 4);
            if (end != std::wstring::npos) ++end;
        } else {
            size_t closeAlt = source.find(L']', start + 2);
            if (closeAlt != std::wstring::npos && closeAlt + 1 < source.size() &&
                source[closeAlt + 1] == L'(') {
                int depth = 1;
                bool escaped = false;
                for (size_t i = closeAlt + 2; i < source.size(); ++i) {
                    wchar_t ch = source[i];
                    if (escaped) {
                        escaped = false;
                        continue;
                    }
                    if (ch == L'\\') {
                        escaped = true;
                    } else if (ch == L'(') {
                        ++depth;
                    } else if (ch == L')') {
                        if (--depth == 0) {
                            end = i + 1;
                            break;
                        }
                    }
                }
            }
        }
        if (end == std::wstring::npos || end <= start) {
            cursor = start + 2;
            continue;
        }
        spans.push_back({start, end});
        cursor = end;
    }
    return spans;
}

std::vector<size_t> FindAllOccurrences(
    const std::wstring& source,
    const std::wstring& needle,
    size_t begin)
{
    std::vector<size_t> positions;
    if (needle.empty()) return positions;
    size_t cursor = begin;
    while (cursor <= source.size()) {
        size_t found = source.find(needle, cursor);
        if (found == std::wstring::npos) break;
        positions.push_back(found);
        if (positions.size() == 2) break;
        cursor = found + (std::max)(static_cast<size_t>(1), needle.size());
    }
    return positions;
}

OcrAlignmentState WorstSemanticState(const std::vector<OcrBlockSourceMapEntry>& sourceMap) {
    bool unresolved = false;
    for (const auto& entry : sourceMap) {
        if (entry.relation == OcrBlockSourceRelation::Ambiguous) {
            return OcrAlignmentState::Ambiguous;
        }
        if (entry.relation == OcrBlockSourceRelation::Unresolved) unresolved = true;
    }
    return unresolved ? OcrAlignmentState::Unresolved : OcrAlignmentState::Verified;
}

bool IsIdentityTransform(const std::array<double, 6>& transform) {
    static constexpr std::array<double, 6> kIdentity = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    for (size_t i = 0; i < transform.size(); ++i) {
        if (!std::isfinite(transform[i]) || std::abs(transform[i] - kIdentity[i]) > 1e-9) {
            return false;
        }
    }
    return true;
}

bool RectWithinBounds(const RECT& rect, uint32_t width, uint32_t height) {
    return rect.left >= 0 && rect.top >= 0 &&
        rect.right > rect.left && rect.bottom > rect.top &&
        static_cast<uint64_t>(rect.right) <= width &&
        static_cast<uint64_t>(rect.bottom) <= height;
}

bool PolygonWithinBounds(
    const std::vector<OcrBlockPoint>& polygon,
    uint32_t width,
    uint32_t height)
{
    if (!polygon.empty() && polygon.size() < 3) return false;
    for (const auto& point : polygon) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            point.x < 0.0f || point.y < 0.0f ||
            point.x > static_cast<float>(width) ||
            point.y > static_cast<float>(height)) {
            return false;
        }
    }
    return true;
}

OcrAlignmentState OverallFromLayers(const OcrPageAlignmentStatus& status) {
    if (status.pageIdentity == OcrAlignmentState::Failed ||
        status.geometry == OcrAlignmentState::Failed ||
        status.semantic == OcrAlignmentState::Failed) {
        return OcrAlignmentState::Failed;
    }
    if (status.pageIdentity == OcrAlignmentState::Ambiguous ||
        status.geometry == OcrAlignmentState::Ambiguous ||
        status.semantic == OcrAlignmentState::Ambiguous) {
        return OcrAlignmentState::TextOnlyWarning;
    }
    if (status.pageIdentity != OcrAlignmentState::Verified ||
        status.geometry != OcrAlignmentState::Verified ||
        status.semantic != OcrAlignmentState::Verified) {
        return OcrAlignmentState::TextOnlyWarning;
    }
    return OcrAlignmentState::Verified;
}

} // namespace

std::wstring CanonicalizeOcrMarkdownSource(const std::wstring& markdown) {
    std::wstring canonical;
    canonical.reserve(markdown.size());
    for (size_t i = 0; i < markdown.size(); ++i) {
        if (markdown[i] == L'\r') {
            if (i + 1 < markdown.size() && markdown[i + 1] == L'\n') ++i;
            canonical.push_back(L'\n');
        } else {
            canonical.push_back(markdown[i]);
        }
    }
    return canonical;
}

bool BuildVerifiedBlockSourceMap(
    const std::wstring& canonicalMarkdown,
    const std::vector<OcrLayoutBlock>& blocks,
    std::vector<OcrBlockSourceMapEntry>& sourceMap,
    std::wstring& revisionSha256,
    OcrAlignmentState& semanticState,
    std::wstring& error)
{
    sourceMap.clear();
    revisionSha256.clear();
    semanticState = OcrAlignmentState::NotChecked;
    error.clear();

    const std::wstring source = CanonicalizeOcrMarkdownSource(canonicalMarkdown);
    constexpr size_t kMaxCanonicalSourceCodeUnits = 16ull * 1024ull * 1024ull;
    constexpr size_t kMaxBlocksPerPage = 100000;
    if (source.size() > kMaxCanonicalSourceCodeUnits || blocks.size() > kMaxBlocksPerPage) {
        semanticState = OcrAlignmentState::Failed;
        error = L"Canonical Markdown or page block count exceeds the source-map safety limit.";
        return false;
    }
    if (!ComputeUtf8Sha256Hex(source, revisionSha256, error)) {
        semanticState = OcrAlignmentState::Failed;
        return false;
    }

    sourceMap.reserve(blocks.size());
    const std::vector<SourceSpan> imageSpans = FindMarkdownImageSpans(source);
    size_t imageBlockCount = 0;
    for (const auto& block : blocks) {
        if (IsImageLabel(block.label) && block.content.empty()) ++imageBlockCount;
    }
    const bool imageSpansAreOneToOne = imageBlockCount == imageSpans.size();
    size_t imageSpanCursor = 0;
    size_t sourceCursor = 0;
    std::map<std::wstring, size_t> ownerEntryByGroup;
    std::set<std::wstring> seenBlockIds;

    for (size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex) {
        const OcrLayoutBlock& block = blocks[blockIndex];
        OcrBlockSourceMapEntry entry;
        entry.blockId = block.id;
        entry.contentOwnerId = block.id;
        entry.sourceRevisionSha256 = revisionSha256;

        if (entry.blockId.empty()) {
            entry.relation = OcrBlockSourceRelation::Unresolved;
            entry.reason = L"Block ID is empty.";
            sourceMap.push_back(std::move(entry));
            continue;
        }

        if (!seenBlockIds.insert(entry.blockId).second) {
            entry.relation = OcrBlockSourceRelation::Ambiguous;
            entry.reason = L"Block ID is duplicated within the page.";
            sourceMap.push_back(std::move(entry));
            continue;
        }

        if (IsLayoutOnlyLabel(block.label)) {
            entry.relation = OcrBlockSourceRelation::LayoutOnly;
            entry.reason = L"Layout decoration is intentionally omitted from canonical Markdown.";
            sourceMap.push_back(std::move(entry));
            continue;
        }

        if (IsFormulaNumberLabel(block.label) && blockIndex > 0 &&
            IsFormulaLabel(blocks[blockIndex - 1].label)) {
            entry.relation = OcrBlockSourceRelation::Alias;
            entry.contentOwnerId = blocks[blockIndex - 1].id;
            entry.reason = L"Formula number aliases the preceding formula owner.";
            sourceMap.push_back(std::move(entry));
            continue;
        }

        if (!block.groupId.empty()) {
            auto ownerIt = ownerEntryByGroup.find(block.groupId);
            if (ownerIt != ownerEntryByGroup.end()) {
                const auto& owner = sourceMap[ownerIt->second];
                entry.relation = OcrBlockSourceRelation::Alias;
                entry.contentOwnerId = owner.blockId;
                entry.sourceStart = owner.sourceStart;
                entry.sourceEnd = owner.sourceEnd;
                entry.reason = L"Recognition-group secondary aliases the verified content owner.";
                sourceMap.push_back(std::move(entry));
                continue;
            }
        }

        const std::wstring content = CanonicalizeOcrMarkdownSource(block.content);
        if (content.empty() && IsImageLabel(block.label)) {
            if (imageSpansAreOneToOne && imageSpanCursor < imageSpans.size()) {
                const SourceSpan span = imageSpans[imageSpanCursor++];
                entry.relation = OcrBlockSourceRelation::Direct;
                entry.sourceStart = static_cast<int64_t>(span.start);
                entry.sourceEnd = static_cast<int64_t>(span.end);
                entry.reason = L"Image block maps one-to-one to an ordered Markdown image token.";
                sourceCursor = span.end;
            } else {
                entry.relation = imageSpans.empty()
                    ? OcrBlockSourceRelation::Unresolved
                    : OcrBlockSourceRelation::Ambiguous;
                entry.reason = imageSpans.empty()
                    ? L"Image block has no Markdown image token."
                    : L"Image block count does not uniquely match Markdown image tokens.";
            }
        } else if (content.empty()) {
            entry.relation = OcrBlockSourceRelation::Unresolved;
            entry.reason = L"Content-bearing block has empty content.";
        } else {
            const std::vector<size_t> occurrences = FindAllOccurrences(source, content, sourceCursor);
            if (occurrences.size() == 1) {
                entry.relation = OcrBlockSourceRelation::Direct;
                entry.sourceStart = static_cast<int64_t>(occurrences.front());
                entry.sourceEnd = static_cast<int64_t>(occurrences.front() + content.size());
                entry.reason = L"Unique exact content match in canonical Markdown.";
                sourceCursor = occurrences.front() + content.size();
            } else if (occurrences.empty()) {
                entry.relation = OcrBlockSourceRelation::Unresolved;
                entry.reason = L"Block content has no exact canonical Markdown match.";
            } else {
                entry.relation = OcrBlockSourceRelation::Ambiguous;
                entry.reason = L"Block content has multiple canonical Markdown matches.";
            }
        }

        sourceMap.push_back(std::move(entry));
        if (!block.groupId.empty() &&
            sourceMap.back().relation == OcrBlockSourceRelation::Direct) {
            ownerEntryByGroup[block.groupId] = sourceMap.size() - 1;
        }
    }

    // Resolve aliases only when their owner is a verified direct mapping.
    std::map<std::wstring, size_t> entryById;
    for (size_t i = 0; i < sourceMap.size(); ++i) entryById[sourceMap[i].blockId] = i;
    for (auto& entry : sourceMap) {
        if (entry.relation != OcrBlockSourceRelation::Alias) continue;
        auto ownerIt = entryById.find(entry.contentOwnerId);
        if (ownerIt == entryById.end() ||
            sourceMap[ownerIt->second].relation != OcrBlockSourceRelation::Direct) {
            entry.relation = OcrBlockSourceRelation::Unresolved;
            entry.sourceStart = -1;
            entry.sourceEnd = -1;
            entry.reason = L"Alias owner does not have a verified direct source range.";
            continue;
        }
        const auto& owner = sourceMap[ownerIt->second];
        entry.sourceStart = owner.sourceStart;
        entry.sourceEnd = owner.sourceEnd;
    }

    semanticState = WorstSemanticState(sourceMap);
    return true;
}

bool ValidateOcrBlockSourceMap(
    const std::wstring& canonicalMarkdown,
    const std::vector<OcrLayoutBlock>& blocks,
    const std::vector<OcrBlockSourceMapEntry>& sourceMap,
    const std::wstring& revisionSha256,
    std::wstring& error)
{
    error.clear();
    const std::wstring source = CanonicalizeOcrMarkdownSource(canonicalMarkdown);
    if (source.size() > 16ull * 1024ull * 1024ull ||
        blocks.size() > 100000 || sourceMap.size() > 100000) {
        error = L"Persisted source-map input exceeds the validation safety limit.";
        return false;
    }
    std::wstring expectedRevision;
    if (!ComputeUtf8Sha256Hex(source, expectedRevision, error)) return false;
    if (!IsSha256Hex(revisionSha256) || revisionSha256 != expectedRevision) {
        error = L"Block source-map revision does not match canonical Markdown.";
        return false;
    }
    if (sourceMap.size() != blocks.size()) {
        error = L"Block source-map cardinality does not match page blocks.";
        return false;
    }

    std::map<std::wstring, const OcrLayoutBlock*> blockById;
    for (const auto& block : blocks) {
        if (block.id.empty() || !blockById.emplace(block.id, &block).second) {
            error = L"Page blocks contain an empty or duplicate stable ID.";
            return false;
        }
    }

    std::map<std::wstring, const OcrBlockSourceMapEntry*> entryById;
    for (const auto& entry : sourceMap) {
        if (entry.blockId.empty() || !entryById.emplace(entry.blockId, &entry).second ||
            blockById.find(entry.blockId) == blockById.end()) {
            error = L"Source map contains an empty, duplicate, or unknown block ID.";
            return false;
        }
        if (entry.sourceRevisionSha256 != revisionSha256) {
            error = L"Source-map row revision does not match the page revision.";
            return false;
        }
        if (entry.relation == OcrBlockSourceRelation::Direct ||
            entry.relation == OcrBlockSourceRelation::Alias) {
            if (entry.sourceStart < 0 || entry.sourceEnd <= entry.sourceStart ||
                static_cast<uint64_t>(entry.sourceEnd) > source.size()) {
                error = L"Interactive source-map row is outside canonical Markdown bounds.";
                return false;
            }
            if (entry.relation == OcrBlockSourceRelation::Direct) {
                if (entry.contentOwnerId != entry.blockId) {
                    error = L"Direct source-map row does not own its own content range.";
                    return false;
                }
                const std::wstring content = CanonicalizeOcrMarkdownSource(
                    blockById[entry.blockId]->content);
                if (!content.empty() &&
                    source.substr(
                        static_cast<size_t>(entry.sourceStart),
                        static_cast<size_t>(entry.sourceEnd - entry.sourceStart)) != content) {
                    error = L"Direct source-map range does not match block content.";
                    return false;
                }
            }
        } else if (entry.sourceStart != -1 || entry.sourceEnd != -1) {
            error = L"Non-interactive source-map row unexpectedly carries a source range.";
            return false;
        }
    }

    for (const auto& entry : sourceMap) {
        if (entry.relation != OcrBlockSourceRelation::Alias) continue;
        auto owner = entryById.find(entry.contentOwnerId);
        if (owner == entryById.end() ||
            owner->second->relation != OcrBlockSourceRelation::Direct ||
            owner->second->sourceStart != entry.sourceStart ||
            owner->second->sourceEnd != entry.sourceEnd) {
            error = L"Alias source-map row does not resolve to one verified direct owner.";
            return false;
        }
    }
    return true;
}

bool ValidateDocumentPageGeometry(
    const OcrCoordinateSpaceMetadata& coordinateSpace,
    const std::vector<OcrLayoutBlock>& blocks,
    OcrAlignmentState& geometryState,
    std::wstring& error)
{
    geometryState = OcrAlignmentState::NotChecked;
    error.clear();

    if (coordinateSpace.recognitionImageWidth == 0 ||
        coordinateSpace.recognitionImageHeight == 0) {
        geometryState = OcrAlignmentState::TextOnlyWarning;
        error = L"Recognition coordinate dimensions are missing.";
        return true;
    }

    for (const auto& block : blocks) {
        if (!RectWithinBounds(
                block.bbox,
                coordinateSpace.recognitionImageWidth,
                coordinateSpace.recognitionImageHeight)) {
            geometryState = OcrAlignmentState::Failed;
            error = L"Block bbox is empty or outside recognition image bounds: " + block.id;
            return false;
        }
        if (!PolygonWithinBounds(
                block.polygon,
                coordinateSpace.recognitionImageWidth,
                coordinateSpace.recognitionImageHeight)) {
            geometryState = OcrAlignmentState::Failed;
            error = L"Block polygon is outside recognition image bounds: " + block.id;
            return false;
        }
    }

    if (coordinateSpace.canonicalImagePath.empty() ||
        coordinateSpace.canonicalImageWidth == 0 ||
        coordinateSpace.canonicalImageHeight == 0 ||
        !IsSha256Hex(coordinateSpace.canonicalImageSha256)) {
        geometryState = OcrAlignmentState::TextOnlyWarning;
        error = L"Canonical image path, decoded dimensions, or SHA-256 is not materialized.";
        return true;
    }

    const bool exactDimensions =
        coordinateSpace.canonicalImageWidth == coordinateSpace.recognitionImageWidth &&
        coordinateSpace.canonicalImageHeight == coordinateSpace.recognitionImageHeight &&
        coordinateSpace.rotationDegrees == 0;
    if (!exactDimensions) {
        geometryState = OcrAlignmentState::TextOnlyWarning;
        error = coordinateSpace.transformVerified
            ? L"A recognition-to-canonical transform is recorded, but Canvas block transformation is not materialized."
            : L"Canonical and recognition dimensions differ without a verified transform.";
        return true;
    }

    if (coordinateSpace.transformVerified &&
        !IsIdentityTransform(coordinateSpace.recognitionToCanonical)) {
        geometryState = OcrAlignmentState::TextOnlyWarning;
        error = L"A non-identity transform cannot be marked interactive until blocks are materialized in canonical coordinates.";
        return true;
    }

    if (coordinateSpace.origin != L"top_left" ||
        coordinateSpace.bboxConvention != L"xyxy_half_open") {
        geometryState = OcrAlignmentState::TextOnlyWarning;
        error = L"Coordinate origin or bbox convention is not supported by Canvas.";
        return true;
    }

    geometryState = OcrAlignmentState::Verified;
    return true;
}

void RefreshDocumentPageOverallAlignment(DocumentOcrPageResult& page) {
    RefreshOcrPageOverallAlignment(page.alignment);
    if (page.alignment.overall == OcrAlignmentState::Verified) {
        page.alignment.reason.clear();
        return;
    }
    if (page.alignment.reason.empty()) {
        page.alignment.reason = L"One or more page/image/Preview alignment layers are not verified.";
    }
}

OcrAlignmentState ComputeOcrPageOverallAlignment(const OcrPageAlignmentStatus& status) {
    return OverallFromLayers(status);
}

void RefreshOcrPageOverallAlignment(OcrPageAlignmentStatus& status) {
    status.overall = OverallFromLayers(status);
    if (status.overall == OcrAlignmentState::Verified) status.reason.clear();
}

bool IsDocumentPageInteractiveAlignmentVerified(const DocumentOcrPageResult& page) {
    if (page.alignment.pageIdentity != OcrAlignmentState::Verified ||
        page.alignment.geometry != OcrAlignmentState::Verified ||
        page.alignment.semantic != OcrAlignmentState::Verified ||
        page.alignment.overall != OcrAlignmentState::Verified ||
        page.sourceRevisionSha256.empty()) {
        return false;
    }
    std::wstring error;
    if (!ValidateOcrBlockSourceMap(
            page.canonicalSourceMarkdown,
            page.blocks,
            page.blockSourceMap,
            page.sourceRevisionSha256,
            error)) {
        return false;
    }
    return std::all_of(
        page.blockSourceMap.begin(),
        page.blockSourceMap.end(),
        [](const OcrBlockSourceMapEntry& entry) {
            return entry.relation == OcrBlockSourceRelation::Direct ||
                entry.relation == OcrBlockSourceRelation::Alias ||
                entry.relation == OcrBlockSourceRelation::LayoutOnly;
        });
}
