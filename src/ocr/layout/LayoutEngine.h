#pragma once
#include "OcrBlock.h"
#include "PaddleDocLayoutProfile.h"

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

struct LayoutRegion {
    RECT bbox = {};
    std::wstring className;
    int classId = -1;
    float confidence = 0.0f;
    // Sparse model-provided sorting key. It is normalized to a dense
    // OcrLayoutBlock order only after final filtering; recognition grouping
    // never mutates or merges this geometry.
    int readingOrder = 0;
    std::wstring vlmPrompt;
    int headingLevel = 0;
    std::vector<OcrBlockPoint> polygon;
    bool polygonFromMask = false;
    size_t queryIndex = 0;
    // Populated only by the long-document fallback. It lets reconciliation
    // distinguish a real page edge from a crop seam.
    bool fromTile = false;
    RECT sourceTile = {};
};

struct OcrSettings;
// Narrow friend used only by the deterministic tile reconciliation contract.
// Keeping the fusion methods private avoids exposing long-document internals
// through the production LayoutEngine API.
struct LayoutEngineContractProbe;

struct LayoutDetectionStageCounts {
    size_t raw = 0;
    size_t scorePassed = 0;
    size_t nmsKept = 0;
    size_t imageAreaKept = 0;
    size_t classModeKept = 0;
    size_t polygonFallbacks = 0;
    size_t overlapKept = 0;
    size_t finalCount = 0;
    size_t exactScoreTies = 0;
    bool polygonDegraded = false;
    std::string error;
};

struct LayoutDetectionDiagnostics {
    LayoutModelFamily family = LayoutModelFamily::Unknown;
    std::wstring modelPath;
    uint64_t modelBytes = 0;
    std::wstring modelSha256;
    std::wstring modelSha256Error;
    LayoutDetectionStageCounts full;
    LayoutDetectionStageCounts tiled;
    bool tiledTriggered = false;
    bool usedTiled = false;
    size_t returnedRegions = 0;
    std::string error;
};

class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine();

    LayoutEngine(const LayoutEngine&) = delete;
    LayoutEngine& operator=(const LayoutEngine&) = delete;

    bool Initialize(const std::wstring& modelPath);
    bool IsAvailable() const { return m_loaded; }
    std::vector<LayoutRegion> Detect(
        HBITMAP hBitmap,
        LayoutDetectionDiagnostics* diagnostics = nullptr);
    void Reset();

private:
    friend struct LayoutEngineContractProbe;
    std::vector<float> Preprocess(HBITMAP hBitmap, int& origW, int& origH,
                                   float& scaleH, float& scaleW);
    std::vector<LayoutRegion> DetectSingle(HBITMAP hBitmap, LONG offsetX, LONG offsetY,
                                           int orderBase, bool logDetails,
                                           LayoutDetectionStageCounts* stageCounts = nullptr);
    std::vector<LayoutRegion> DetectTiled(HBITMAP hBitmap, int origW, int origH,
                                          LayoutDetectionStageCounts* stageCounts = nullptr);
    void PostprocessRegions(std::vector<LayoutRegion>& regions, bool sortByPosition);
    void SortRegions(std::vector<LayoutRegion>& regions, bool sortByPosition);
    void ReconcileTileRegions(
        std::vector<LayoutRegion>& regions, int pageWidth, int pageHeight);
    std::vector<LayoutRegion> FuseFullAndTileRegions(
        const std::vector<LayoutRegion>& fullRegions,
        const std::vector<LayoutRegion>& tileRegions,
        int pageWidth,
        int pageHeight);
    bool ShouldRunTiled(int origW, int origH, size_t fullRegionCount) const;
    void* m_env = nullptr;
    void* m_session = nullptr;
    void* m_allocator = nullptr;
    bool m_loaded = false;
    std::wstring m_modelPath;
    uint64_t m_modelBytes = 0;
    std::wstring m_modelSha256;
    std::wstring m_modelSha256Error;
    LayoutModelFamily m_modelFamily = LayoutModelFamily::Unknown;
    PaddleDocLayoutProfile m_profile;
    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    int m_boxesOutputIndex = -1;
    int m_bboxNumOutputIndex = -1;
    int m_masksOutputIndex = -1;
};
