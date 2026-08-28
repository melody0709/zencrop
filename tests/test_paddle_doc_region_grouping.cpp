#include "ocr/engine/PaddleDocRegionGrouping.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

LayoutRegion Region(
    const wchar_t* label,
    RECT box,
    int order = 0,
    const wchar_t* prompt = L"OCR:")
{
    LayoutRegion region;
    region.className = label;
    region.classId = std::wstring(label) == L"text" ? 22 :
        (std::wstring(label) == L"table" ? 21 :
        (std::wstring(label) == L"chart" ? 3 :
        (std::wstring(label) == L"image" ? 14 :
        (std::wstring(label) == L"seal" ? 20 : 0))));
    region.bbox = box;
    region.readingOrder = order;
    region.vlmPrompt = prompt;
    return region;
}

PaddleDocRecognitionPlan Plan(
    const std::vector<LayoutRegion>& regions,
    PaddleDocGroupingOptions options = {},
    PaddleDocGroupingStats* stats = nullptr)
{
    return BuildPaddleDocRecognitionPlan(regions, options, stats);
}

const PaddleDocRecognitionGroup& GroupFor(
    const PaddleDocRecognitionPlan& plan,
    size_t index)
{
    auto found = std::find_if(plan.groups.begin(), plan.groups.end(), [index](const auto& group) {
        return std::find(group.regionIndices.begin(), group.regionIndices.end(), index) !=
            group.regionIndices.end();
    });
    if (found == plan.groups.end()) Fail("group not found");
    return *found;
}

void TestNormalParagraphsStaySingleton() {
    std::vector<LayoutRegion> regions;
    for (int index = 0; index < 10; ++index) {
        regions.push_back(Region(
            L"text", RECT{ 10, index * 50, 210, index * 50 + 40 },
            300 - index));
    }
    auto plan = Plan(regions);
    Expect(plan.groups.size() == 10, "ten same-width paragraphs stay singleton");
    for (const auto& group : plan.groups) {
        Expect(group.regionIndices.size() == 1, "normal paragraph was not chained");
    }
    Expect(GroupFor(plan, 0).contentOwnerIndex == 0,
        "vector order, not sparse model order, defines owner");
}

void TestCrossFragmentsAndBoundaries() {
    auto merged = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"text", RECT{ 120, 10, 220, 50 }),
    });
    Expect(merged.groups.size() == 1 && merged.groups[0].regionIndices.size() == 2,
        "same-line left/right fragments merge");
    Expect(merged.groups[0].alignments ==
        std::vector<PaddleDocGroupAlignment>{ PaddleDocGroupAlignment::Center },
        "cross fragments use center alignment");

    auto gapEquality = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"text", RECT{ 130, 10, 230, 50 }),
    });
    Expect(gapEquality.groups.size() == 2,
        "cross gap == 0.3 * max width is not merged");

    auto topEquality = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"text", RECT{ 120, 40, 220, 80 }),
    });
    Expect(topEquality.groups.size() == 2,
        "cross current.top == previous.bottom is not merged");

    auto noVerticalIntersectionCheck = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"text", RECT{ 120, -40, 220, -10 }),
    });
    Expect(noVerticalIntersectionCheck.groups.size() == 1,
        "cross predicate does not invent an upstream-absent bottom/top check");
}

std::vector<LayoutRegion> UpDownFixture(
    LONG left,
    LONG top,
    RECT obstacle,
    const wchar_t* obstacleLabel = L"table")
{
    return {
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(obstacleLabel, obstacle,
            1, std::wstring(obstacleLabel) == L"table" ? L"Table Recognition:" : L"OCR:"),
        Region(L"text", RECT{ left, top, 120, top + 40 }),
    };
}

void TestUpDownAlignmentObstacleAndBoundaries() {
    auto merged = Plan(UpDownFixture(5, 50, RECT{ 90, 35, 110, 55 }));
    const auto& group = GroupFor(merged, 0);
    Expect(group.regionIndices == std::vector<size_t>({ 0, 2 }),
        "one-side aligned text fragments merge across a structural obstacle");
    Expect(group.alignments ==
        std::vector<PaddleDocGroupAlignment>{ PaddleDocGroupAlignment::Left },
        "5px left alignment is inclusive");
    Expect(GroupFor(merged, 1).regionIndices.size() == 1,
        "non-merge obstacle remains its own group");

    auto sixPixels = Plan(UpDownFixture(6, 50, RECT{ 90, 35, 110, 55 }));
    Expect(GroupFor(sixPixels, 0).regionIndices.size() == 1,
        "6px left-edge difference is not aligned");

    auto bothAligned = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"table", RECT{ 90, 35, 110, 55 }, 1, L"Table Recognition:"),
        Region(L"text", RECT{ 0, 50, 100, 90 }),
    });
    Expect(GroupFor(bothAligned, 0).regionIndices.size() == 1,
        "left+right alignment XOR rejects normal paragraphs");

    auto gapEquality = Plan(UpDownFixture(0, 60, RECT{ 90, 35, 110, 75 }));
    Expect(GroupFor(gapEquality, 0).regionIndices.size() == 1,
        "up/down gap == 0.5 * max height is not merged");

    auto touchingObstacle = Plan(UpDownFixture(0, 50, RECT{ 120, 35, 140, 55 }));
    Expect(GroupFor(touchingObstacle, 0).regionIndices.size() == 1,
        "zero-area obstacle contact is not overlap");

    auto positiveObstacle = Plan(UpDownFixture(0, 50, RECT{ 119, 35, 140, 55 }));
    Expect(GroupFor(positiveObstacle, 0).regionIndices.size() == 2,
        "strictly positive obstacle overlap permits grouping");
}

void TestDynamicNonMergeSet() {
    PaddleDocGroupingOptions options;
    auto chartObstacle = UpDownFixture(0, 50, RECT{ 90, 35, 110, 55 }, L"chart");
    auto chartOff = Plan(chartObstacle, options);
    Expect(GroupFor(chartOff, 0).regionIndices.size() == 2,
        "disabled chart recognition makes chart an obstacle");
    options.recognizeCharts = true;
    auto chartOn = Plan(chartObstacle, options);
    Expect(GroupFor(chartOn, 0).regionIndices.size() == 1,
        "enabled chart recognition removes chart from obstacle set");

    auto labels = PaddleDocBuildNonMergeLabels(options);
    Expect(std::find(labels.begin(), labels.end(), L"table") != labels.end(),
        "table is always non-merge");
    Expect(std::find(labels.begin(), labels.end(), L"chart") == labels.end(),
        "recognized chart is not an obstacle");
    Expect(std::find(labels.begin(), labels.end(), L"image") != labels.end(),
        "unrecognized image remains an obstacle");
    options.recognizeImages = true;
    labels = PaddleDocBuildNonMergeLabels(options);
    Expect(std::find(labels.begin(), labels.end(), L"image") == labels.end() &&
        std::find(labels.begin(), labels.end(), L"header_image") == labels.end(),
        "image OCR removes all official image labels from non-merge set");
}

void TestPromptLabelAndAspectRules() {
    auto differentPrompt = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }, 0, L"OCR:"),
        Region(L"text", RECT{ 120, 10, 220, 50 }, 1, L"Other Recognition:"),
    });
    Expect(differentPrompt.groups.size() == 2,
        "same label with different prompts never groups");

    auto differentLabel = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"vertical_text", RECT{ 120, 10, 220, 50 }),
    });
    Expect(differentLabel.groups.size() == 2, "different labels never group");

    auto tall = Plan({
        Region(L"text", RECT{ 0, 0, 100, 200 }),
        Region(L"text", RECT{ 120, 100, 220, 300 }),
    });
    Expect(tall.groups.size() == 2,
        "composed height/width >= 3 splits back to singleton groups");

    PaddleDocGroupingStats stats;
    auto boundary = Plan({
        Region(L"text", RECT{ 0, 0, 100, 150 }),
        Region(L"text", RECT{ 120, 100, 220, 250 }),
    }, {}, &stats);
    Expect(boundary.groups.size() == 2 && stats.aspectSplitGroups == 1,
        "composed aspect exactly 3 is split and counted");

    PaddleDocGroupingOptions memberLimit;
    memberLimit.maxGroupMembers = 1;
    PaddleDocGroupingStats limitStats;
    auto memberSplit = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"text", RECT{ 120, 10, 220, 50 }),
    }, memberLimit, &limitStats);
    Expect(memberSplit.groups.size() == 2 && limitStats.limitSplitGroups == 1,
        "defensive member cap splits a pathological group");

    PaddleDocGroupingOptions pixelLimit;
    pixelLimit.maxComposedPixels = 7999;
    auto pixelSplit = Plan({
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"text", RECT{ 120, 10, 220, 50 }),
    }, pixelLimit, &limitStats);
    Expect(pixelSplit.groups.size() == 2 && limitStats.limitSplitGroups == 1,
        "defensive composed-pixel cap prevents oversized allocation");
}

void TestPlanInvariantsAndModes() {
    std::vector<LayoutRegion> regions = {
        Region(L"text", RECT{ 0, 0, 100, 40 }),
        Region(L"text", RECT{ 120, 10, 220, 50 }),
        Region(L"table", RECT{ 0, 80, 200, 130 }, 0, L"Table Recognition:"),
    };
    PaddleDocGroupingStats stats;
    auto plan = Plan(regions, {}, &stats);
    Expect(plan.groupIdByRegion.size() == regions.size() &&
        plan.contentOwnerByRegion.size() == regions.size(),
        "every region has group and owner maps");
    Expect(plan.groupIdByRegion[0] == L"group_1" &&
        plan.groupIdByRegion[1] == L"group_1" &&
        plan.contentOwnerByRegion[1] == 0,
        "multi-member group has stable page-local id and first owner");
    Expect(plan.groupIdByRegion[2] == L"group_3",
        "singleton gets its own page-local id");
    Expect(stats.groupCount == 2 && stats.secondaryRegions == 1,
        "grouping statistics preserve block/group distinction");

    PaddleDocGroupingOptions none;
    none.mode = L"none";
    auto singleton = Plan(regions, none);
    Expect(singleton.groups.size() == regions.size(),
        "none mode is singleton-safe");

    PaddleDocGroupingOptions legacy;
    legacy.mode = L"legacy_union_ab";
    legacy.legacyVerticalThreshold = 20;
    auto legacyPlan = Plan({
        Region(L"text", RECT{ 0, 0, 200, 40 }),
        Region(L"text", RECT{ 0, 50, 200, 90 }),
    }, legacy);
    Expect(legacyPlan.groups.size() == 1 &&
        legacyPlan.groups[0].useLegacyUnionCrop,
        "legacy A/B mode remains explicit and separate from official grouping");
}

} // namespace

int main() {
    TestNormalParagraphsStaySingleton();
    TestCrossFragmentsAndBoundaries();
    TestUpDownAlignmentObstacleAndBoundaries();
    TestDynamicNonMergeSet();
    TestPromptLabelAndAspectRules();
    TestPlanInvariantsAndModes();
    std::cout << "Paddle Doc region grouping contract passed.\n";
    return 0;
}
