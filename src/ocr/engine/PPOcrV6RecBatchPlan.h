#pragma once

// Hermetic recognition batch planning for PP-OCRv6.
// Official pipeline sorts crops by aspect/width before batching, then restores
// original box order. ZenCrop keeps sourceBoxIndex on every crop and restores
// reading order by ascending sourceBoxIndex after recognition.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace PPOcrV6RecBatch {

// Sort indices of rec inputs by width ascending (stable for equal widths).
// widths[i] must be > 0 for usable crops.
inline std::vector<size_t> OrderByWidthAscending(const std::vector<int>& widths) {
    std::vector<size_t> order(widths.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return widths[a] < widths[b];
    });
    return order;
}

// Build consecutive batches from a width-sorted order.
// - maxBatchSize: hard cap per RunRecBatch call (1..N)
// - maxWidthRatio: if > 1 and batch non-empty, do not add item when
//   max(width)/min(width) in the prospective batch would exceed the ratio.
//   Pass <= 1.0f to disable ratio bucketing (only maxBatchSize applies).
inline std::vector<std::vector<size_t>> BuildBatches(
    const std::vector<size_t>& widthOrder,
    const std::vector<int>& widths,
    int maxBatchSize,
    float maxWidthRatio = 2.0f)
{
    std::vector<std::vector<size_t>> batches;
    if (widthOrder.empty() || maxBatchSize < 1) return batches;

    std::vector<size_t> current;
    current.reserve(static_cast<size_t>(maxBatchSize));
    int minW = 0;
    int maxW = 0;

    auto flush = [&]() {
        if (!current.empty()) {
            batches.push_back(current);
            current.clear();
            minW = 0;
            maxW = 0;
        }
    };

    for (size_t idx : widthOrder) {
        if (idx >= widths.size()) continue;
        const int w = widths[idx];
        if (w <= 0) continue;

        if (!current.empty()) {
            const bool full = static_cast<int>(current.size()) >= maxBatchSize;
            bool ratioBreak = false;
            if (maxWidthRatio > 1.0f) {
                const int newMin = (std::min)(minW, w);
                const int newMax = (std::max)(maxW, w);
                if (newMin > 0 &&
                    static_cast<float>(newMax) > maxWidthRatio * static_cast<float>(newMin)) {
                    ratioBreak = true;
                }
            }
            if (full || ratioBreak) flush();
        }

        if (current.empty()) {
            minW = w;
            maxW = w;
        } else {
            minW = (std::min)(minW, w);
            maxW = (std::max)(maxW, w);
        }
        current.push_back(idx);
    }
    flush();
    return batches;
}

// Sort accepted-line-like records by sourceBoxIndex ascending (reading order).
// Requires a projection: getIndex(item) -> size_t.
template <typename T, typename IndexFn>
inline void SortBySourceBoxIndex(std::vector<T>& items, IndexFn getIndex) {
    std::stable_sort(items.begin(), items.end(), [&](const T& a, const T& b) {
        return getIndex(a) < getIndex(b);
    });
}

// Convenience for vectors of structs with .sourceBoxIndex member.
template <typename T>
inline void SortBySourceBoxIndexMember(std::vector<T>& items) {
    SortBySourceBoxIndex(items, [](const T& x) { return x.sourceBoxIndex; });
}

// Resolve UI/settings batch size: 0 means Auto -> official-like 6.
inline int ResolveRecBatchSize(int configured) {
    if (configured <= 0) return 6;
    if (configured > 8) return 8;
    return configured;
}

// Estimate padded pixel waste: sum over batches of (batchMaxWidth * batchSize).
// Lower is better after width-aware packing vs reading-order packing.
inline long long TotalPaddedWidthUnits(
    const std::vector<std::vector<size_t>>& batches,
    const std::vector<int>& widths)
{
    long long total = 0;
    for (const auto& batch : batches) {
        int maxW = 0;
        for (size_t idx : batch) {
            if (idx < widths.size()) maxW = (std::max)(maxW, widths[idx]);
        }
        total += static_cast<long long>(maxW) * static_cast<long long>(batch.size());
    }
    return total;
}

} // namespace PPOcrV6RecBatch
