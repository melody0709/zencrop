#pragma once

#include "OcrBlockPresentation.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

// Runtime-only acceleration for the Dashboard canvas.  It intentionally owns
// no HWNDs and is rebuilt only when the block snapshot or issue image extent
// changes; zoom and pan remain presentation state and do not invalidate it.
class DashboardBlockRuntimeIndex {
public:
    template <typename Block>
    bool Rebuild(const std::vector<Block>& blocks, LONG imageArea) {
        ++m_generation;
        m_idToIndex.clear();
        m_bboxAreas.assign(blocks.size(), 0);
        m_issueCounts.assign(blocks.size(), 0);
        m_contentOwnerIndices.resize(blocks.size());
        std::iota(m_contentOwnerIndices.begin(), m_contentOwnerIndices.end(), size_t{0});
        m_readingOrderIndices.resize(blocks.size());
        std::iota(m_readingOrderIndices.begin(), m_readingOrderIndices.end(), size_t{0});
        m_hasDuplicateIds = false;

        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto& block = blocks[i];
            if (!block.id.empty()) {
                auto inserted = m_idToIndex.emplace(block.id, i);
                if (!inserted.second) m_hasDuplicateIds = true; // first ID wins deterministically
            }
            LONG area = RectArea(block.bbox);
            m_bboxAreas[i] = area;
        }

        struct OwnerCandidate {
            size_t index = npos;
            int order = 0;
            bool hasContent = false;
        };
        std::unordered_map<GroupKey, OwnerCandidate, GroupKeyHash> owners;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const std::wstring& groupId = BlockGroupId(blocks[i]);
            if (groupId.empty()) continue;
            GroupKey key{blocks[i].pageIndex, groupId};
            OwnerCandidate candidate{i, blocks[i].order, !IsBlank(blocks[i].content)};
            auto inserted = owners.emplace(key, candidate);
            if (!inserted.second) {
                auto& current = inserted.first->second;
                if ((candidate.hasContent && !current.hasContent) ||
                    (candidate.hasContent == current.hasContent &&
                        (candidate.order < current.order ||
                            (candidate.order == current.order && candidate.index < current.index)))) {
                    current = candidate;
                }
            }
        }
        for (size_t i = 0; i < blocks.size(); ++i) {
            const std::wstring& groupId = BlockGroupId(blocks[i]);
            if (groupId.empty()) continue;
            auto owner = owners.find(GroupKey{blocks[i].pageIndex, groupId});
            if (owner != owners.end()) m_contentOwnerIndices[i] = owner->second.index;
        }

        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto& block = blocks[i];
            LONG area = m_bboxAreas[i];
            int count = 0;
            if (block.edited) ++count;
            if (IsBlank(block.content)) {
                const size_t owner = m_contentOwnerIndices[i];
                if (owner == i && (owner >= blocks.size() || IsBlank(blocks[owner].content))) {
                    ++count;
                }
            }
            if (block.confidence >= 0.0 && block.confidence < 0.55) ++count;
            LONG width = (std::max)(0L, block.bbox.right - block.bbox.left);
            LONG height = (std::max)(0L, block.bbox.bottom - block.bbox.top);
            if (area > 0 && (width < 5 || height < 5 || area <= 64)) ++count;
            if (imageArea > 0 && area > imageArea * 8 / 10) ++count;
            else if (imageArea <= 0 && area > 2000000L) ++count;
            m_issueCounts[i] = count;
        }

        // Preserve the legacy overlap rule exactly while moving its O(n^2)
        // cost out of WM_PAINT and into the snapshot rebuild.
        // TextLine snapshots (pure ppocrv6_onnx/text) skip bbox-overlap issues:
        // rotated lines often have non-overlapping polygons but high AABB overlap.
        // Other issues (confidence/small/huge/edited) remain above.
        // Reuse the shared presentation policy so paint and issue index never drift.
        const bool textLineMode = OcrBlockPresentation::IsTextLineMode(blocks);
        if (!textLineMode) {
            for (size_t i = 0; i < blocks.size(); ++i) {
                LONG area = m_bboxAreas[i];
                if (area <= 0) continue;
                for (size_t j = 0; j < blocks.size(); ++j) {
                    if (i == j || blocks[j].id == blocks[i].id ||
                        blocks[j].pageIndex != blocks[i].pageIndex) continue;
                    const std::wstring& groupId = BlockGroupId(blocks[i]);
                    if (!groupId.empty() && groupId == BlockGroupId(blocks[j])) continue;
                    LONG otherArea = m_bboxAreas[j];
                    if (otherArea <= 0) continue;
                    LONG overlap = IntersectionArea(blocks[i].bbox, blocks[j].bbox);
                    LONG smaller = (std::min)(area, otherArea);
                    if (smaller > 0 && overlap > smaller * 45 / 100) {
                        ++m_issueCounts[i];
                        break;
                    }
                }
            }
        }

        std::stable_sort(m_readingOrderIndices.begin(), m_readingOrderIndices.end(),
            [&](size_t a, size_t b) { return blocks[a].order < blocks[b].order; });
        return !m_hasDuplicateIds;
    }

    void Clear() {
        ++m_generation;
        m_idToIndex.clear();
        m_bboxAreas.clear();
        m_issueCounts.clear();
        m_contentOwnerIndices.clear();
        m_readingOrderIndices.clear();
        m_hasDuplicateIds = false;
    }

    size_t FindById(const std::wstring& id) const {
        auto it = m_idToIndex.find(id);
        return it == m_idToIndex.end() ? npos : it->second;
    }
    int IssueCount(size_t index) const {
        return index < m_issueCounts.size() ? m_issueCounts[index] : 0;
    }
    LONG BboxArea(size_t index) const {
        return index < m_bboxAreas.size() ? m_bboxAreas[index] : 0;
    }
    template <typename Block>
    size_t ContentOwnerIndex(size_t selectedIndex, const std::vector<Block>& blocks) const {
        if (selectedIndex >= blocks.size()) return npos;
        if (BlockGroupId(blocks[selectedIndex]).empty() ||
            !IsBlank(blocks[selectedIndex].content)) {
            return selectedIndex;
        }
        size_t owner = selectedIndex < m_contentOwnerIndices.size()
            ? m_contentOwnerIndices[selectedIndex] : selectedIndex;
        return owner < blocks.size() ? owner : selectedIndex;
    }
    const std::vector<size_t>& ReadingOrderIndices() const { return m_readingOrderIndices; }
    uint64_t Generation() const { return m_generation; }
    bool HasDuplicateIds() const { return m_hasDuplicateIds; }
    static constexpr size_t npos = (std::numeric_limits<size_t>::max)();

private:
    struct GroupKey {
        int pageIndex = 0;
        std::wstring groupId;
        bool operator==(const GroupKey& other) const {
            return pageIndex == other.pageIndex && groupId == other.groupId;
        }
    };
    struct GroupKeyHash {
        size_t operator()(const GroupKey& key) const {
            size_t first = std::hash<int>{}(key.pageIndex);
            size_t second = std::hash<std::wstring>{}(key.groupId);
            return first ^ (second + 0x9e3779b9u + (first << 6) + (first >> 2));
        }
    };
    template <typename Block>
    static const std::wstring& BlockGroupId(const Block& block) {
        if constexpr (requires { block.groupId; }) {
            return block.groupId;
        } else {
            static const std::wstring empty;
            return empty;
        }
    }
    static LONG RectArea(const RECT& r) {
        LONGLONG width = (std::max)(0L, r.right - r.left);
        LONGLONG height = (std::max)(0L, r.bottom - r.top);
        LONGLONG area = width * height;
        return area > LONG_MAX ? LONG_MAX : static_cast<LONG>(area);
    }
    static LONG IntersectionArea(const RECT& a, const RECT& b) {
        LONG width = (std::max)(0L, (std::min)(a.right, b.right) - (std::max)(a.left, b.left));
        LONG height = (std::max)(0L, (std::min)(a.bottom, b.bottom) - (std::max)(a.top, b.top));
        LONGLONG area = static_cast<LONGLONG>(width) * height;
        return area > LONG_MAX ? LONG_MAX : static_cast<LONG>(area);
    }
    static bool IsBlank(const std::wstring& text) {
        return std::all_of(text.begin(), text.end(), [](wchar_t ch) { return iswspace(ch) != 0; });
    }

    uint64_t m_generation = 0;
    bool m_hasDuplicateIds = false;
    std::unordered_map<std::wstring, size_t> m_idToIndex;
    std::vector<LONG> m_bboxAreas;
    std::vector<int> m_issueCounts;
    std::vector<size_t> m_contentOwnerIndices;
    std::vector<size_t> m_readingOrderIndices;
};
