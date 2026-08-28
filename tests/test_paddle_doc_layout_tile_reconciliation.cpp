#include "core/Settings.h"
#include "ocr/OcrUtils.h"
#include "ocr/layout/LayoutEngine.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

OcrSettings LoadOcrSettings() {
    OcrSettings settings;
    settings.layoutModelFamily = L"paddle_doc_layout_v3";
    settings.layoutThresholdProfile = L"official";
    return settings;
}

HBITMAP CropBitmap(HBITMAP, RECT) {
    return nullptr;
}

const LayoutClassInfo* GetLayoutClassInfo(int) {
    // Tile reconciliation receives already decoded LayoutRegion values; model
    // decoding is intentionally outside this deterministic geometry contract.
    return nullptr;
}

struct LayoutEngineContractProbe {
    static void Reconcile(
        LayoutEngine& engine,
        std::vector<LayoutRegion>& regions,
        int pageWidth,
        int pageHeight)
    {
        engine.m_profile = BuildPaddleDocLayoutProfile(
            LayoutModelFamily::PPDocLayoutV3,
            LayoutThresholdProfile::Official);
        // The contract isolates seam geometry. Polygon extraction is separately
        // covered by test_paddle_doc_layout_mask_postprocess.
        engine.m_profile.rectMode = true;
        engine.ReconcileTileRegions(regions, pageWidth, pageHeight);
    }

    static std::vector<LayoutRegion> Fuse(
        LayoutEngine& engine,
        const std::vector<LayoutRegion>& full,
        const std::vector<LayoutRegion>& tiles,
        int pageWidth,
        int pageHeight)
    {
        engine.m_profile = BuildPaddleDocLayoutProfile(
            LayoutModelFamily::PPDocLayoutV3,
            LayoutThresholdProfile::Official);
        engine.m_profile.rectMode = true;
        return engine.FuseFullAndTileRegions(full, tiles, pageWidth, pageHeight);
    }
};

namespace {

constexpr int kPageWidth = 1860;
constexpr int kPageHeight = 2633;

void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

LayoutRegion Region(
    LONG left,
    LONG top,
    LONG right,
    LONG bottom,
    float confidence = 0.9f)
{
    LayoutRegion region;
    region.classId = 22; // text
    region.className = L"text";
    region.confidence = confidence;
    region.bbox = { left, top, right, bottom };
    return region;
}

LayoutRegion TileRegion(
    LONG left,
    LONG top,
    LONG right,
    LONG bottom,
    RECT sourceTile,
    float confidence = 0.9f)
{
    auto region = Region(left, top, right, bottom, confidence);
    region.fromTile = true;
    region.sourceTile = sourceTile;
    return region;
}

bool SameBounds(const LayoutRegion& region, LONG left, LONG top, LONG right, LONG bottom) {
    return region.bbox.left == left && region.bbox.top == top &&
        region.bbox.right == right && region.bbox.bottom == bottom;
}

void TestFullBaselineRejectsInternalSeamFragment() {
    LayoutEngine engine;
    const std::vector<LayoutRegion> full = {
        Region(241, 857, 1599, 1082),
    };
    const std::vector<LayoutRegion> tiles = {
        // This is the actual failure shape: a narrow continuation beginning at
        // the second tile's y=1033 internal edge. It has low bbox IoU with the
        // complete full region, but must never become a separate block.
        TileRegion(240, 1033, 1600, 1080, { 0, 1033, 1600, 2633 }, 0.99f),
    };

    const auto fused = LayoutEngineContractProbe::Fuse(
        engine, full, tiles, kPageWidth, kPageHeight);
    Expect(fused.size() == 1, "internal seam fragment must not be accepted");
    Expect(SameBounds(fused[0], 241, 857, 1599, 1082),
        "full-image baseline must remain authoritative");
}

void TestContainedTileSliverIsSuppressed() {
    LayoutEngine engine;
    const std::vector<LayoutRegion> full = {
        Region(100, 100, 800, 600),
    };
    const std::vector<LayoutRegion> tiles = {
        // Not adjacent to an internal seam. Suppression must therefore come
        // from same-class small-area containment rather than seam heuristics.
        TileRegion(160, 180, 300, 360, { 0, 0, 1600, 1600 }, 0.98f),
    };

    const auto fused = LayoutEngineContractProbe::Fuse(
        engine, full, tiles, kPageWidth, kPageHeight);
    Expect(fused.size() == 1, "contained tile sliver must be deduplicated");
    Expect(SameBounds(fused[0], 100, 100, 800, 600),
        "contained tile sliver must not replace the full region");
}

void TestReconciliationPrefersCompleteTileCandidate() {
    LayoutEngine engine;
    std::vector<LayoutRegion> tiles = {
        TileRegion(100, 1000, 1200, 1300, { 0, 0, 1600, 1600 }, 0.70f),
        TileRegion(100, 1033, 1200, 1250, { 0, 1033, 1600, 2633 }, 0.99f),
    };

    LayoutEngineContractProbe::Reconcile(engine, tiles, kPageWidth, kPageHeight);
    Expect(tiles.size() == 1, "overlapping seam duplicate must be reconciled");
    Expect(SameBounds(tiles[0], 100, 1000, 1200, 1300),
        "complete tile candidate must win over seam fragment");
}

} // namespace

int main() {
    try {
        TestFullBaselineRejectsInternalSeamFragment();
        TestContainedTileSliverIsSuppressed();
        TestReconciliationPrefersCompleteTileCandidate();
        std::cout << "Paddle Doc tile reconciliation contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Paddle Doc tile reconciliation contract FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
