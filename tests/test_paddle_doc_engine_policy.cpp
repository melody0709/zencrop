#include "ocr/engine/PaddleDocEnginePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void Fail(const char* message) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
}

void Expect(bool value, const char* message) {
    if (!value) Fail(message);
}

LayoutRegion Region(int classId, const wchar_t* className) {
    LayoutRegion region;
    region.classId = classId;
    region.className = className;
    return region;
}

void TestRecognitionAndMarkdownPoliciesAreIndependent() {
    OcrSettings settings;
    settings.docIgnorePageDecorations = true;
    settings.docKeepFootnotes = false;

    auto header = Region(12, L"header");
    Expect(PaddleDocShouldIgnoreRegionInMarkdown(header, settings),
        "header should be omitted from Markdown when decoration ignore is on");
    Expect(!PaddleDocShouldSkipVlmForRegion(header, settings),
        "header must still be recognized before Markdown filtering");

    auto number = Region(16, L"number");
    Expect(PaddleDocShouldIgnoreRegionInMarkdown(number, settings),
        "page number should follow Markdown decoration policy");
    Expect(!PaddleDocShouldSkipVlmForRegion(number, settings),
        "page number must remain a recognition input");

    auto footnote = Region(10, L"footnote");
    Expect(PaddleDocShouldIgnoreRegionInMarkdown(footnote, settings),
        "footnote should be omitted when docKeepFootnotes is false");
    Expect(!PaddleDocShouldSkipVlmForRegion(footnote, settings),
        "docKeepFootnotes must not control VLM recognition");
    settings.docKeepFootnotes = true;
    Expect(!PaddleDocShouldIgnoreRegionInMarkdown(footnote, settings),
        "footnote should enter Markdown when explicitly retained");
    Expect(!PaddleDocShouldSkipVlmForRegion(footnote, settings),
        "footnote remains recognizable in both Markdown modes");

    settings.docIgnorePageDecorations = false;
    Expect(!PaddleDocShouldIgnoreRegionInMarkdown(header, settings),
        "disabled decoration filtering should retain header Markdown");
}

void TestDynamicVisualBlockRecognition() {
    OcrSettings settings;
    auto image = Region(14, L"image");
    auto headerImage = Region(13, L"header_image");
    auto chart = Region(3, L"chart");
    auto seal = Region(20, L"seal");
    Expect(PaddleDocShouldSkipVlmForRegion(image, settings) &&
        PaddleDocShouldSkipVlmForRegion(headerImage, settings),
        "image-family blocks should follow docRecognizeImages");
    Expect(PaddleDocShouldSkipVlmForRegion(chart, settings),
        "chart should follow docRecognizeCharts");
    Expect(PaddleDocShouldSkipVlmForRegion(seal, settings),
        "seal should follow docRecognizeSeals");
    settings.docRecognizeImages = true;
    settings.docRecognizeCharts = true;
    settings.docRecognizeSeals = true;
    Expect(!PaddleDocShouldSkipVlmForRegion(image, settings) &&
        !PaddleDocShouldSkipVlmForRegion(headerImage, settings) &&
        !PaddleDocShouldSkipVlmForRegion(chart, settings) &&
        !PaddleDocShouldSkipVlmForRegion(seal, settings),
        "enabled visual recognition settings were ignored");
}

void TestLayoutCacheKeyIncludesFamily() {
    Expect(!PaddleDocLayoutCacheNeedsReload(
        true, L"model.onnx", L"pp_doclayout_v3",
        L"model.onnx", L"pp_doclayout_v3"),
        "identical layout cache key should be reused");
    Expect(PaddleDocLayoutCacheNeedsReload(
        true, L"model.onnx", L"pp_doclayout_v3",
        L"model.onnx", L"pp_doclayout_v2"),
        "family change with the same path must reload the session");
    Expect(PaddleDocLayoutCacheNeedsReload(
        true, L"a.onnx", L"auto", L"b.onnx", L"auto"),
        "path change must reload the session");
    Expect(PaddleDocLayoutCacheNeedsReload(
        false, L"model.onnx", L"auto", L"model.onnx", L"auto"),
        "unavailable engine must initialize even with an equal key");
}

void TestPageCompletionPropagatesGroupFailures() {
    Expect(PaddleDocRecognitionPageSucceeded(0),
        "a page with no failed recognition groups should succeed");
    Expect(!PaddleDocRecognitionPageSucceeded(1),
        "one failed recognition group must fail the page OCR task");
    const std::wstring error = PaddleDocRecognitionFailureError(2);
    Expect(error.find(L"2 group(s) failed") != std::wstring::npos,
        "page failure must report the failed group count");
}

} // namespace

int main() {
    TestRecognitionAndMarkdownPoliciesAreIndependent();
    TestDynamicVisualBlockRecognition();
    TestLayoutCacheKeyIncludesFamily();
    TestPageCompletionPropagatesGroupFailures();
    std::cout << "Paddle Doc engine policy contract passed.\n";
    return 0;
}
