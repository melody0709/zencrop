#include "PaddleDocRegionGrouping.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace {

int Width(const RECT& box) {
    return (std::max)(0L, box.right - box.left);
}

int Height(const RECT& box) {
    return (std::max)(0L, box.bottom - box.top);
}

RECT EnclosingBox(const RECT& first, const RECT& second) {
    return {
        (std::min)(first.left, second.left),
        (std::min)(first.top, second.top),
        (std::max)(first.right, second.right),
        (std::max)(first.bottom, second.bottom),
    };
}

bool HasPositiveOverlap(const RECT& first, const RECT& second) {
    const LONG width = (std::min)(first.right, second.right) -
        (std::max)(first.left, second.left);
    const LONG height = (std::min)(first.bottom, second.bottom) -
        (std::max)(first.top, second.top);
    return width > 0 && height > 0;
}

double HorizontalProjectionOverlapUnion(const RECT& first, const RECT& second) {
    const LONG overlap = (std::min)(first.right, second.right) -
        (std::max)(first.left, second.left);
    if (overlap <= 0) return 0.0;
    const LONG unionWidth = (std::max)(first.right, second.right) -
        (std::min)(first.left, second.left);
    return unionWidth > 0 ? (double)overlap / unionWidth : 0.0;
}

bool IsAligned(LONG first, LONG second) {
    return std::abs((long long)first - second) <= 5;
}

bool IsNonMergeLabel(
    const std::wstring& label,
    const std::set<std::wstring>& nonMergeLabels)
{
    return nonMergeLabels.find(label) != nonMergeLabels.end();
}

bool OverlapsNonMergeObstacle(
    size_t currentIndex,
    size_t previousIndex,
    const std::vector<LayoutRegion>& regions,
    const std::set<std::wstring>& nonMergeLabels)
{
    const RECT enclosing = EnclosingBox(
        regions[previousIndex].bbox, regions[currentIndex].bbox);
    for (size_t index = 0; index < regions.size(); ++index) {
        if (index == currentIndex || index == previousIndex) continue;
        if (!IsNonMergeLabel(regions[index].className, nonMergeLabels)) continue;
        if (HasPositiveOverlap(enclosing, regions[index].bbox)) return true;
    }
    return false;
}

bool ShouldCrossMerge(const LayoutRegion& current, const LayoutRegion& previous) {
    const RECT& box = current.bbox;
    const RECT& previousBox = previous.bbox;
    if (HorizontalProjectionOverlapUnion(box, previousBox) != 0.0) return false;
    if (current.className != L"text" || current.className != previous.className) {
        return false;
    }
    if (current.vlmPrompt != previous.vlmPrompt) return false;
    if (box.left <= previousBox.right || box.top >= previousBox.bottom) return false;
    const LONG gap = box.left - previousBox.right;
    const LONG maxWidth = (std::max)(Width(previousBox), Width(box));
    return (double)gap < (double)maxWidth * 0.3;
}

bool ShouldUpDownMerge(
    size_t currentIndex,
    size_t previousIndex,
    const std::vector<LayoutRegion>& regions,
    const std::set<std::wstring>& nonMergeLabels,
    PaddleDocGroupAlignment& alignment)
{
    const auto& current = regions[currentIndex];
    const auto& previous = regions[previousIndex];
    const RECT& box = current.bbox;
    const RECT& previousBox = previous.bbox;
    if (HorizontalProjectionOverlapUnion(box, previousBox) <= 0.0) return false;
    if (current.className != L"text" || current.className != previous.className) {
        return false;
    }
    if (current.vlmPrompt != previous.vlmPrompt) return false;
    if (box.bottom < previousBox.top) return false;
    const LONG gap = (LONG)std::abs((long long)box.top - previousBox.bottom);
    const LONG maxHeight = (std::max)(Height(previousBox), Height(box));
    if ((double)gap >= (double)maxHeight * 0.5) return false;
    const bool leftAligned = IsAligned(box.left, previousBox.left);
    const bool rightAligned = IsAligned(box.right, previousBox.right);
    if (leftAligned == rightAligned) return false;
    if (!OverlapsNonMergeObstacle(
        currentIndex, previousIndex, regions, nonMergeLabels)) {
        return false;
    }
    alignment = leftAligned
        ? PaddleDocGroupAlignment::Left
        : PaddleDocGroupAlignment::Right;
    return true;
}

void SetGroupDimensions(
    PaddleDocRecognitionGroup& group,
    const std::vector<LayoutRegion>& regions)
{
    long long composedWidth = 0;
    long long composedHeight = 0;
    for (size_t index : group.regionIndices) {
        composedWidth = (std::max)(
            composedWidth, static_cast<long long>(Width(regions[index].bbox)));
        composedHeight += Height(regions[index].bbox);
        if (composedHeight > INT_MAX) composedHeight = INT_MAX;
    }
    group.composedWidth = static_cast<int>((std::min)(composedWidth, (long long)INT_MAX));
    group.composedHeight = static_cast<int>((std::min)(composedHeight, (long long)INT_MAX));
}

bool ExceedsGroupLimits(
    const PaddleDocRecognitionGroup& group,
    const PaddleDocGroupingOptions& options)
{
    if (options.maxGroupMembers > 0 &&
        group.regionIndices.size() > options.maxGroupMembers) {
        return true;
    }
    if (options.maxComposedPixels == 0) return false;
    const uint64_t width = static_cast<uint64_t>((std::max)(0, group.composedWidth));
    const uint64_t height = static_cast<uint64_t>((std::max)(0, group.composedHeight));
    return width > 0 && height > options.maxComposedPixels / width;
}

PaddleDocRecognitionGroup SingletonGroup(
    size_t index,
    const std::vector<LayoutRegion>& regions,
    bool legacyUnion = false)
{
    PaddleDocRecognitionGroup group;
    group.regionIndices = { index };
    group.contentOwnerIndex = index;
    group.prompt = regions[index].vlmPrompt.empty() ? L"OCR:" : regions[index].vlmPrompt;
    group.useLegacyUnionCrop = legacyUnion;
    SetGroupDimensions(group, regions);
    return group;
}

std::vector<PaddleDocRecognitionGroup> BuildSingletonGroups(
    const std::vector<LayoutRegion>& regions)
{
    std::vector<PaddleDocRecognitionGroup> groups;
    groups.reserve(regions.size());
    for (size_t index = 0; index < regions.size(); ++index) {
        groups.push_back(SingletonGroup(index, regions));
    }
    return groups;
}

bool IsLegacyBodyText(const LayoutRegion& region) {
    return (region.classId == 22 || region.classId == 19 || region.classId == 24) &&
        region.vlmPrompt == L"OCR:" && region.headingLevel == 0 &&
        region.className != L"algorithm" && region.className != L"image" &&
        region.className != L"chart" && region.className != L"seal";
}

std::vector<PaddleDocRecognitionGroup> BuildLegacyGroups(
    const std::vector<LayoutRegion>& regions,
    int threshold)
{
    std::vector<PaddleDocRecognitionGroup> groups;
    std::vector<bool> used(regions.size(), false);
    for (size_t first = 0; first < regions.size(); ++first) {
        if (used[first]) continue;
        PaddleDocRecognitionGroup group = SingletonGroup(first, regions, true);
        used[first] = true;
        RECT mergedBox = regions[first].bbox;
        if (IsLegacyBodyText(regions[first])) {
            for (size_t next = first + 1; next < regions.size(); ++next) {
                if (used[next] || !IsLegacyBodyText(regions[next])) continue;
                LONG gap = regions[next].bbox.top - mergedBox.bottom;
                if (gap < 0) gap = 0;
                if (gap > threshold) continue;
                const LONG overlap = (std::min)(mergedBox.right, regions[next].bbox.right) -
                    (std::max)(mergedBox.left, regions[next].bbox.left);
                const LONG minWidth = (std::min)(Width(mergedBox), Width(regions[next].bbox));
                if (overlap <= 0 || minWidth <= 0 ||
                    (double)overlap / minWidth < 0.5) {
                    continue;
                }
                const LONG maxWidth = (std::max)(Width(mergedBox), Width(regions[next].bbox));
                if ((double)maxWidth / minWidth > 1.45) continue;
                const LONG centerA = (mergedBox.left + mergedBox.right) / 2;
                const LONG centerB = (regions[next].bbox.left + regions[next].bbox.right) / 2;
                if ((double)std::abs((long long)centerA - centerB) > minWidth * 0.22) {
                    continue;
                }
                group.regionIndices.push_back(next);
                group.alignments.push_back(PaddleDocGroupAlignment::Center);
                used[next] = true;
                mergedBox = EnclosingBox(mergedBox, regions[next].bbox);
            }
        }
        SetGroupDimensions(group, regions);
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<PaddleDocRecognitionGroup> BuildOfficialGroups(
    const std::vector<LayoutRegion>& regions,
    const std::set<std::wstring>& nonMergeLabels,
    const PaddleDocGroupingOptions& options,
    size_t& aspectSplitGroups,
    size_t& limitSplitGroups)
{
    std::vector<size_t> mergeableIndices;
    mergeableIndices.reserve(regions.size());
    for (size_t index = 0; index < regions.size(); ++index) {
        if (!IsNonMergeLabel(regions[index].className, nonMergeLabels)) {
            mergeableIndices.push_back(index);
        }
    }

    std::vector<PaddleDocRecognitionGroup> mergeableGroups;
    for (size_t position = 0; position < mergeableIndices.size(); ++position) {
        const size_t index = mergeableIndices[position];
        if (mergeableGroups.empty()) {
            mergeableGroups.push_back(SingletonGroup(index, regions));
            continue;
        }
        const size_t previousIndex = mergeableIndices[position - 1];
        PaddleDocGroupAlignment alignment = PaddleDocGroupAlignment::Center;
        const bool cross = ShouldCrossMerge(regions[index], regions[previousIndex]);
        const bool upDown = !cross && ShouldUpDownMerge(
            index, previousIndex, regions, nonMergeLabels, alignment);
        if (cross || upDown) {
            PaddleDocRecognitionGroup candidate = mergeableGroups.back();
            candidate.regionIndices.push_back(index);
            candidate.alignments.push_back(cross
                ? PaddleDocGroupAlignment::Center
                : alignment);
            SetGroupDimensions(candidate, regions);
            if (ExceedsGroupLimits(candidate, options)) {
                ++limitSplitGroups;
                mergeableGroups.push_back(SingletonGroup(index, regions));
            } else {
                mergeableGroups.back() = std::move(candidate);
            }
        } else {
            mergeableGroups.push_back(SingletonGroup(index, regions));
        }
    }

    std::vector<PaddleDocRecognitionGroup> groups;
    groups.reserve(regions.size());
    for (auto& group : mergeableGroups) {
        SetGroupDimensions(group, regions);
        const double aspect = group.composedWidth > 0
            ? (double)group.composedHeight / group.composedWidth
            : std::numeric_limits<double>::infinity();
        if (group.regionIndices.size() > 1 && aspect >= 3.0) {
            ++aspectSplitGroups;
            for (size_t index : group.regionIndices) {
                groups.push_back(SingletonGroup(index, regions));
            }
        } else {
            groups.push_back(std::move(group));
        }
    }

    for (size_t index = 0; index < regions.size(); ++index) {
        if (IsNonMergeLabel(regions[index].className, nonMergeLabels)) {
            groups.push_back(SingletonGroup(index, regions));
        }
    }
    std::stable_sort(groups.begin(), groups.end(), [](const auto& first, const auto& second) {
        return first.regionIndices.front() < second.regionIndices.front();
    });
    return groups;
}

void NormalizePlan(
    PaddleDocRecognitionPlan& plan,
    const std::vector<LayoutRegion>& regions)
{
    plan.groupIdByRegion.assign(regions.size(), L"");
    plan.contentOwnerByRegion.assign(regions.size(), (size_t)-1);
    for (auto& group : plan.groups) {
        if (group.regionIndices.empty()) continue;
        const size_t owner = group.regionIndices.front();
        group.contentOwnerIndex = owner;
        // OWN-127: pure group id (WideStringUtils).
        group.id = WideFormatGroupId(static_cast<int>(owner + 1));
        group.prompt = regions[owner].vlmPrompt.empty() ? L"OCR:" : regions[owner].vlmPrompt;
        for (size_t index : group.regionIndices) {
            if (index >= regions.size() || !plan.groupIdByRegion[index].empty()) continue;
            plan.groupIdByRegion[index] = group.id;
            plan.contentOwnerByRegion[index] = owner;
        }
    }
}

bool ValidatePlan(
    const PaddleDocRecognitionPlan& plan,
    size_t regionCount,
    std::string& error)
{
    if (plan.groupIdByRegion.size() != regionCount ||
        plan.contentOwnerByRegion.size() != regionCount) {
        error = "plan map size mismatch";
        return false;
    }
    std::set<std::wstring> ids;
    std::vector<size_t> membershipCount(regionCount, 0);
    for (const auto& group : plan.groups) {
        if (group.regionIndices.empty() || !ids.insert(group.id).second ||
            group.alignments.size() + 1 != group.regionIndices.size()) {
            error = "invalid or duplicate recognition group";
            return false;
        }
        for (size_t index : group.regionIndices) {
            if (index >= regionCount || ++membershipCount[index] != 1) {
                error = "duplicate or out-of-range group membership";
                return false;
            }
        }
    }
    for (size_t index = 0; index < regionCount; ++index) {
        if (membershipCount[index] != 1 || plan.groupIdByRegion[index].empty() ||
            plan.contentOwnerByRegion[index] >= regionCount) {
            error = "region is not assigned exactly once";
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<std::wstring> PaddleDocBuildNonMergeLabels(
    const PaddleDocGroupingOptions& options)
{
    std::vector<std::wstring> labels{ L"table" };
    if (!options.recognizeImages) {
        labels.push_back(L"image");
        labels.push_back(L"header_image");
        labels.push_back(L"footer_image");
    }
    if (!options.recognizeCharts) labels.push_back(L"chart");
    if (!options.recognizeSeals) labels.push_back(L"seal");
    return labels;
}

PaddleDocRecognitionPlan BuildPaddleDocRecognitionPlan(
    const std::vector<LayoutRegion>& regions,
    const PaddleDocGroupingOptions& options,
    PaddleDocGroupingStats* outputStats)
{
    PaddleDocGroupingStats localStats;
    PaddleDocGroupingStats& stats = outputStats ? *outputStats : localStats;
    stats = {};
    stats.regionCount = regions.size();

    PaddleDocRecognitionPlan plan;
    if (options.mode == L"none") {
        plan.groups = BuildSingletonGroups(regions);
    } else if (options.mode == L"legacy_union_ab") {
        plan.groups = BuildLegacyGroups(regions, options.legacyVerticalThreshold);
    } else {
        const auto labels = PaddleDocBuildNonMergeLabels(options);
        const std::set<std::wstring> nonMergeLabels(labels.begin(), labels.end());
        plan.groups = BuildOfficialGroups(
            regions, nonMergeLabels, options,
            stats.aspectSplitGroups, stats.limitSplitGroups);
    }
    NormalizePlan(plan, regions);

    if (!ValidatePlan(plan, regions.size(), stats.error)) {
        stats.singletonFallback = true;
        plan.groups = BuildSingletonGroups(regions);
        NormalizePlan(plan, regions);
        std::string fallbackError;
        if (!ValidatePlan(plan, regions.size(), fallbackError)) {
            stats.error += "; singleton fallback invalid: " + fallbackError;
        }
    }

    stats.groupCount = plan.groups.size();
    for (const auto& group : plan.groups) {
        stats.maxGroupMembers = (std::max)(
            stats.maxGroupMembers, group.regionIndices.size());
        if (group.regionIndices.size() > 1) {
            ++stats.multiMemberGroups;
            stats.secondaryRegions += group.regionIndices.size() - 1;
        }
        const double aspect = group.composedWidth > 0
            ? (double)group.composedHeight / group.composedWidth
            : 0.0;
        stats.maxComposedAspectRatio = (std::max)(
            stats.maxComposedAspectRatio, aspect);
    }
    return plan;
}
