#pragma once

#include "LayoutEngine.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class PaddleDocGroupAlignment {
    Left,
    Center,
    Right,
};

struct PaddleDocRecognitionGroup {
    std::wstring id;
    std::vector<size_t> regionIndices;
    // One alignment for each member after the first, matching PaddleX's
    // iterative vertical composition semantics.
    std::vector<PaddleDocGroupAlignment> alignments;
    size_t contentOwnerIndex = 0;
    std::wstring prompt;
    int composedWidth = 0;
    int composedHeight = 0;
    bool useLegacyUnionCrop = false;
};

struct PaddleDocRecognitionPlan {
    std::vector<PaddleDocRecognitionGroup> groups;
    std::vector<std::wstring> groupIdByRegion;
    std::vector<size_t> contentOwnerByRegion;
};

struct PaddleDocGroupingOptions {
    std::wstring mode = L"official_group";
    bool recognizeCharts = false;
    bool recognizeImages = false;
    bool recognizeSeals = false;
    int legacyVerticalThreshold = 0;
    // Pathological-input guards. Defaults are intentionally far above normal
    // official groups and only prevent unbounded bitmap allocation.
    size_t maxGroupMembers = 128;
    uint64_t maxComposedPixels = 64ull * 1024ull * 1024ull;
};

struct PaddleDocGroupingStats {
    size_t regionCount = 0;
    size_t groupCount = 0;
    size_t multiMemberGroups = 0;
    size_t secondaryRegions = 0;
    size_t maxGroupMembers = 0;
    double maxComposedAspectRatio = 0.0;
    size_t aspectSplitGroups = 0;
    size_t limitSplitGroups = 0;
    bool singletonFallback = false;
    std::string error;
};

std::vector<std::wstring> PaddleDocBuildNonMergeLabels(
    const PaddleDocGroupingOptions& options);

PaddleDocRecognitionPlan BuildPaddleDocRecognitionPlan(
    const std::vector<LayoutRegion>& regions,
    const PaddleDocGroupingOptions& options,
    PaddleDocGroupingStats* stats = nullptr);
