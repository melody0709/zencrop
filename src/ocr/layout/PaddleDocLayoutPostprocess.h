#pragma once

#include "PaddleDocLayoutProfile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PaddleDocPointF {
    float x = 0.0f;
    float y = 0.0f;
};

struct PaddleDocLayoutCandidate {
    int classId = -1;
    float confidence = 0.0f;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    int readingOrder = 0;
    size_t queryIndex = 0;
    std::vector<PaddleDocPointF> polygon;
    bool polygonFromMask = false;
};

struct PaddleDocMaskTensorView {
    const int32_t* data = nullptr;
    size_t count = 0;
    int height = 0;
    int width = 0;

    bool IsUsableFor(size_t index) const {
        return data != nullptr && index < count && height > 0 && width > 0;
    }
};

struct PaddleDocPostprocessOptions {
    PaddleDocLayoutProfile profile;
    int imageWidth = 0;
    int imageHeight = 0;
    int modelInputWidth = 800;
    int modelInputHeight = 800;
    int offsetX = 0;
    int offsetY = 0;
    int readingOrderBase = 0;
    bool pinnedMaxBoxWidthCompat = true;
};

struct PaddleDocPostprocessStats {
    size_t raw = 0;
    size_t scorePassed = 0;
    size_t nmsKept = 0;
    size_t imageAreaKept = 0;
    size_t classModeKept = 0;
    size_t polygonFallbacks = 0;
    size_t polygonTopologyFallbacks = 0;
    size_t overlapKept = 0;
    size_t finalCount = 0;
    size_t exactScoreTies = 0;
    size_t removedReference = 0;
    size_t removedMinEdge = 0;
    size_t removedInlineFormula = 0;
    size_t removedGeneralOverlap = 0;
    bool polygonRuntimeAvailable = false;
    bool v3PolygonDegraded = false;
    std::string error;
};

bool PaddleDocPolygonSmallOverlap(
    const std::vector<PaddleDocPointF>& first,
    const std::vector<PaddleDocPointF>& second,
    double& ratio);

bool PaddleDocPolygonUnionOverlap(
    const std::vector<PaddleDocPointF>& first,
    const std::vector<PaddleDocPointF>& second,
    double& ratio);

std::vector<PaddleDocLayoutCandidate> PostprocessPaddleDocLayoutCandidates(
    std::vector<PaddleDocLayoutCandidate> candidates,
    const PaddleDocMaskTensorView& masks,
    const PaddleDocPostprocessOptions& options,
    PaddleDocPostprocessStats* stats = nullptr);
