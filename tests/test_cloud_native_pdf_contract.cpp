#include "ocr/OcrDocumentAlignment.h"
#include "ocr/OcrDocumentTypes.h"
#include "ocr/document/PaddleCloudDocumentNormalizer.h"
#include "ocr/document/PaddleCloudDocumentProtocol.h"
#include "ocr/document/PaddleCloudDocumentTransport.h"
#include "core/Sha256.h"
#include "support/TestArtifactPaths.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool Fail(const char* message) {
    std::cerr << "Cloud native PDF contract failed: " << message << "\n";
    return false;
}

const OcrBlockSourceMapEntry* FindSourceMap(
    const DocumentOcrPageResult& page,
    const std::wstring& idSuffix)
{
    for (const auto& entry : page.blockSourceMap) {
        if (entry.blockId.size() >= idSuffix.size() &&
            entry.blockId.compare(entry.blockId.size() - idSuffix.size(), idSuffix.size(), idSuffix) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

bool TestSha256AndPageRanges() {
    std::wstring sha;
    std::wstring error;
    if (!ComputeSha256Hex("abc", 3, sha, error)) return Fail("SHA-256 computation failed");
    if (sha != L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        return Fail("SHA-256 digest mismatch");
    }
    if (BuildCanonicalCloudPageRanges({2, 4, 5, 6, 9}) != L"2,4-6,9") {
        return Fail("canonical page range mismatch");
    }
    if (!BuildCanonicalCloudPageRanges({2, 2}).empty() ||
        !BuildCanonicalCloudPageRanges({3, 2}).empty()) {
        return Fail("invalid canonical page range was accepted");
    }
    return true;
}

bool TestExplicitPageNormalizationAndAlignment() {
    const std::wstring json = LR"JSON({
  "layoutParsingResults": [
    {
      "pageNumber": 2,
      "inputImage": {"url": "https://example.invalid/page2.png"},
      "outputImages": {"layout": "https://example.invalid/page2-layout.png"},
      "prunedResult": {
        "width": 1000,
        "height": 1400,
        "parsing_res_list": [
          {"block_label":"header","block_content":"","block_bbox":[10,10,900,40],"block_id":"header"},
          {"block_label":"text","block_content":"Alpha","block_bbox":[100,100,500,180],"block_id":"owner","group_id":"group_a"},
          {"block_label":"text","block_content":"","block_bbox":[100,190,500,260],"block_id":"secondary","group_id":"group_a"}
        ]
      },
      "markdown": {"text": "Alpha", "images": {"imgs/figure.png": "https://example.invalid/figure.png"}}
    },
    {
      "pageNumber": 4,
      "prunedResult": {
        "width": 800,
        "height": 1200,
        "parsing_res_list": [
          {"block_label":"display_formula","block_content":"x+y","block_bbox":[50,80,500,180],"block_id":"formula"},
          {"block_label":"formula_number","block_content":"(1)","block_bbox":[520,80,580,180],"block_id":"number"}
        ]
      },
      "markdown": {"text": "$$x+y\\tag{1}$$"}
    }
  ]
})JSON";

    DocumentOcrResult result;
    PaddleCloudDocumentNormalizeOptions options;
    if (!NormalizePaddleCloudDocumentJsonl(json, {2, 4}, options, result)) {
        std::wcerr << L"normalize error: " << result.error << L"\n";
        return Fail("explicit page normalization failed");
    }
    if (!result.success || result.pages.size() != 2 ||
        result.pages[0].originalPageNumber != 2 ||
        result.pages[1].originalPageNumber != 4 ||
        result.pages[0].resultOrdinal != 0 || result.pages[1].resultOrdinal != 1) {
        return Fail("explicit page identity mismatch");
    }
    if (result.pages[0].stablePageId != L"page_0002" ||
        result.pages[0].blocks.size() != 3 ||
        result.pages[0].blocks[0].id.find(L"page_2:") != 0) {
        return Fail("stable page/block identity mismatch");
    }
    if (result.pages[0].coordinateSpace.recognitionImageWidth != 1000 ||
        result.pages[0].coordinateSpace.recognitionImageHeight != 1400 ||
        result.pages[0].resources.size() != 3 ||
        result.pages[0].resources[0].kind != L"recognition_image" ||
        result.pages[0].resources[2].kind != L"markdown_image" ||
        result.pages[0].resources[2].localPath != L"imgs/figure.png" ||
        result.pages[0].resources[2].remoteUrl != L"https://example.invalid/figure.png") {
        return Fail("recognition image metadata mismatch");
    }

    const auto* header = FindSourceMap(result.pages[0], L":header");
    const auto* owner = FindSourceMap(result.pages[0], L":owner");
    const auto* secondary = FindSourceMap(result.pages[0], L":secondary");
    const auto* formula = FindSourceMap(result.pages[1], L":formula");
    const auto* number = FindSourceMap(result.pages[1], L":number");
    if (!header || !owner || !secondary || !formula || !number) {
        return Fail("source map entries missing");
    }
    if (header->relation != OcrBlockSourceRelation::LayoutOnly ||
        owner->relation != OcrBlockSourceRelation::Direct ||
        secondary->relation != OcrBlockSourceRelation::Alias ||
        secondary->contentOwnerId != owner->blockId ||
        formula->relation != OcrBlockSourceRelation::Direct ||
        number->relation != OcrBlockSourceRelation::Alias ||
        number->contentOwnerId != formula->blockId) {
        return Fail("direct/alias/layout-only source relation mismatch");
    }
    if (result.pages[0].alignment.pageIdentity != OcrAlignmentState::Verified ||
        result.pages[0].alignment.semantic != OcrAlignmentState::Verified ||
        result.pages[0].alignment.geometry != OcrAlignmentState::TextOnlyWarning ||
        result.pages[0].alignment.overall != OcrAlignmentState::TextOnlyWarning) {
        return Fail("pre-materialization alignment state mismatch");
    }

    auto& page = result.pages[0];
    page.coordinateSpace.canonicalImageKind = L"cloud_recognition_image";
    page.coordinateSpace.canonicalImagePath = L"page_images/page_0002.png";
    page.coordinateSpace.canonicalImageSha256 =
        L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    page.coordinateSpace.canonicalImageWidth = 1000;
    page.coordinateSpace.canonicalImageHeight = 1400;
    std::wstring geometryError;
    if (!ValidateDocumentPageGeometry(
            page.coordinateSpace,
            page.blocks,
            page.alignment.geometry,
            geometryError) ||
        !geometryError.empty()) {
        return Fail("materialized geometry did not verify");
    }
    RefreshDocumentPageOverallAlignment(page);
    if (!IsDocumentPageInteractiveAlignmentVerified(page)) {
        return Fail("fully verified page did not enable interactive alignment");
    }
    return true;
}

bool TestStrictOrdinalFallback() {
    const std::wstring jsonl =
        LR"({"prunedResult":{"width":100,"height":100,"parsing_res_list":[{"block_label":"text","block_content":"Two","block_bbox":[1,1,30,20],"block_id":"a"}]},"markdown":{"text":"Two"}})"
        L"\r\n"
        LR"({"prunedResult":{"width":100,"height":100,"parsing_res_list":[{"block_label":"text","block_content":"Four","block_bbox":[1,1,40,20],"block_id":"b"}]},"markdown":{"text":"Four"}})";
    DocumentOcrResult result;
    PaddleCloudDocumentNormalizeOptions options;
    options.allowStrictOrdinalFallback = true;
    if (!NormalizePaddleCloudDocumentJsonl(jsonl, {2, 4}, options, result)) {
        return Fail("strict ordinal fallback failed");
    }
    if (result.pages[0].originalPageNumber != 2 ||
        result.pages[1].originalPageNumber != 4 ||
        result.pages[0].originalPageNumberExplicit ||
        result.pages[1].originalPageNumberExplicit) {
        return Fail("ordinal fallback page identity mismatch");
    }
    options.allowStrictOrdinalFallback = false;
    if (NormalizePaddleCloudDocumentJsonl(jsonl, {2, 4}, options, result)) {
        return Fail("disabled ordinal fallback was accepted");
    }
    return true;
}

bool TestOcrResultsNormalization() {
    const std::wstring jsonl = LR"JSON({"result":{"ocrResults":[
      {"pageNumber":1,"prunedResult":{"width":640,"height":480,"rec_texts":["One","Line"]},"docPreprocessingImage":{"url":"https://cdn.example.com/p1.png"}},
      {"pageNumber":2,"prunedResult":{"width":800,"height":600,"recTexts":["Two"]},"ocrImage":"https://cdn.example.com/p2.jpg"}
    ]}})JSON";
    DocumentOcrResult result;
    PaddleCloudDocumentNormalizeOptions options;
    options.model = L"PaddleOCR-VL-1.6";
    if (!NormalizePaddleCloudDocumentJsonl(jsonl, {1, 2}, options, result) ||
        result.model != L"PaddleOCR-VL-1.6" || result.pages.size() != 2 ||
        result.pages[0].plainText != L"One\nLine" ||
        result.pages[0].markdown != L"One\nLine" ||
        result.pages[0].resources.empty() ||
        result.pages[0].resources[0].kind != L"recognition_image" ||
        result.pages[1].plainText != L"Two" ||
        result.pages[1].resources.empty() ||
        result.pages[1].resources[0].kind != L"ocr_output_image") {
        return Fail("ocrResults document payload did not normalize");
    }
    return true;
}

bool TestIdentityFailures() {
    const std::wstring duplicate = LR"({"layoutParsingResults":[
      {"pageNumber":2,"prunedResult":{"width":10,"height":10,"parsing_res_list":[]},"markdown":{"text":"A"}},
      {"pageNumber":2,"prunedResult":{"width":10,"height":10,"parsing_res_list":[]},"markdown":{"text":"B"}}
    ]})";
    DocumentOcrResult result;
    PaddleCloudDocumentNormalizeOptions options;
    if (NormalizePaddleCloudDocumentJsonl(duplicate, {2, 4}, options, result)) {
        return Fail("duplicate explicit page identity was accepted");
    }

    const std::wstring mixed = LR"({"layoutParsingResults":[
      {"pageNumber":2,"prunedResult":{"width":10,"height":10,"parsing_res_list":[]},"markdown":{"text":"A"}},
      {"prunedResult":{"width":10,"height":10,"parsing_res_list":[]},"markdown":{"text":"B"}}
    ]})";
    if (NormalizePaddleCloudDocumentJsonl(mixed, {2, 4}, options, result)) {
        return Fail("mixed explicit/implicit page identity was accepted");
    }

    const std::wstring onePage = LR"({"prunedResult":{"width":10,"height":10,"parsing_res_list":[]},"markdown":{"text":"A"}})";
    if (NormalizePaddleCloudDocumentJsonl(onePage, {1, 2}, options, result)) {
        return Fail("page cardinality mismatch was accepted");
    }
    if (NormalizePaddleCloudDocumentJsonl(L"{\"markdown\":{\"text\":\"A\"}", {1}, options, result)) {
        return Fail("truncated JSONL was accepted");
    }
    const std::wstring reordered = LR"({"layoutParsingResults":[
      {"pageNumber":4,"prunedResult":{"width":10,"height":10,"parsing_res_list":[]},"markdown":{"text":"B"}},
      {"pageNumber":2,"prunedResult":{"width":10,"height":10,"parsing_res_list":[]},"markdown":{"text":"A"}}
    ]})";
    if (NormalizePaddleCloudDocumentJsonl(reordered, {2, 4}, options, result)) {
        return Fail("explicit page identity/ordinal conflict was accepted");
    }
    if (NormalizePaddleCloudDocumentJsonl(
            LR"({"prunedResult":{"width":10,"height":10},"markdown":{"text":"A",}})",
            {1},
            options,
            result)) {
        return Fail("balanced but malformed JSONL record was accepted");
    }
    if (NormalizePaddleCloudDocumentJsonl(
            LR"({"pageIndex":0.5,"prunedResult":{"width":10,"height":10},"markdown":{"text":"A"}})",
            {1},
            options,
            result) ||
        NormalizePaddleCloudDocumentJsonl(
            LR"({"pageNumber":"1junk","prunedResult":{"width":10,"height":10},"markdown":{"text":"A"}})",
            {1},
            options,
            result)) {
        return Fail("malformed explicit page identity was accepted");
    }
    const std::wstring nonJsonWhitespace =
        L"{\"pageNumber\":" + std::wstring(1, static_cast<wchar_t>(0x00A0)) +
        L"1,\"prunedResult\":{\"width\":10,\"height\":10},"
        L"\"markdown\":{\"text\":\"A\"}}";
    if (NormalizePaddleCloudDocumentJsonl(
            nonJsonWhitespace,
            {1},
            options,
            result)) {
        return Fail("non-JSON Unicode whitespace was accepted");
    }
    if (NormalizePaddleCloudDocumentJsonl(L"{\"result\":{\"unknown\":[]}}", {1}, options, result)) {
        return Fail("unknown document payload was accepted");
    }
    if (NormalizePaddleCloudDocumentJsonl(L"{\"result\":{\"ocrResults\":[\"bad\"]}}", {1}, options, result)) {
        return Fail("non-object ocrResults item was accepted");
    }
    return true;
}

bool TestAmbiguousSourceAndGeometryBounds() {
    std::vector<OcrLayoutBlock> blocks(1);
    blocks[0].id = L"page_1:block";
    blocks[0].label = L"text";
    blocks[0].content = L"same";
    blocks[0].bbox = RECT{1, 1, 20, 20};
    blocks[0].polygon = {{1.0f, 1.0f}, {20.0f, 1.0f}, {20.0f, 20.0f}, {1.0f, 20.0f}};

    std::vector<OcrBlockSourceMapEntry> sourceMap;
    std::wstring revision;
    std::wstring error;
    OcrAlignmentState semantic = OcrAlignmentState::NotChecked;
    if (!BuildVerifiedBlockSourceMap(
            L"same\n\nsame",
            blocks,
            sourceMap,
            revision,
            semantic,
            error) ||
        semantic != OcrAlignmentState::Ambiguous ||
        sourceMap.size() != 1 ||
        sourceMap[0].relation != OcrBlockSourceRelation::Ambiguous) {
        return Fail("repeated text was not reported as ambiguous");
    }

    OcrCoordinateSpaceMetadata coordinate;
    coordinate.recognitionImageWidth = 10;
    coordinate.recognitionImageHeight = 10;
    OcrAlignmentState geometry = OcrAlignmentState::NotChecked;
    if (ValidateDocumentPageGeometry(coordinate, blocks, geometry, error) ||
        geometry != OcrAlignmentState::Failed) {
        return Fail("out-of-bounds bbox was not rejected");
    }

    blocks[0].bbox = RECT{1, 1, 9, 9};
    blocks.push_back(blocks[0]);
    if (!BuildVerifiedBlockSourceMap(
            L"same",
            blocks,
            sourceMap,
            revision,
            semantic,
            error) ||
        semantic != OcrAlignmentState::Ambiguous) {
        return Fail("duplicate stable block ID was not reported as ambiguous");
    }

    blocks.resize(1);
    blocks[0].polygon = {{1.0f, 1.0f}, {9.0f, 1.0f}, {9.0f, 9.0f}, {1.0f, 9.0f}};
    coordinate.recognitionImageWidth = 10;
    coordinate.recognitionImageHeight = 10;
    coordinate.canonicalImageWidth = 20;
    coordinate.canonicalImageHeight = 20;
    coordinate.canonicalImagePath = L"page.png";
    coordinate.canonicalImageSha256 =
        L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    coordinate.transformVerified = true;
    coordinate.recognitionToCanonical = {2.0, 0.0, 0.0, 2.0, 0.0, 0.0};
    if (!ValidateDocumentPageGeometry(coordinate, blocks, geometry, error) ||
        geometry != OcrAlignmentState::TextOnlyWarning) {
        return Fail("unmaterialized coordinate transform enabled interaction");
    }
    return true;
}

bool TestMetadataRoundTrip() {
    OcrBlockSourceMapEntry entry;
    entry.blockId = L"page_1:block";
    entry.relation = OcrBlockSourceRelation::Direct;
    entry.contentOwnerId = entry.blockId;
    entry.sourceStart = 4;
    entry.sourceEnd = 9;
    entry.sourceRevisionSha256 =
        L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    entry.reason = L"verified";
    std::wstring sourceMapJson = OcrBlockSourceMapToJson({entry});
    auto roundTrip = ParseOcrBlockSourceMap(sourceMapJson);
    if (roundTrip.size() != 1 || roundTrip[0].blockId != entry.blockId ||
        roundTrip[0].relation != entry.relation ||
        roundTrip[0].sourceStart != 4 || roundTrip[0].sourceEnd != 9 ||
        roundTrip[0].sourceRevisionSha256 != entry.sourceRevisionSha256) {
        return Fail("block source map did not round-trip");
    }

    OcrCoordinateSpaceMetadata coordinate;
    coordinate.canonicalImageKind = L"cloud_recognition_image";
    coordinate.canonicalImagePath = L"page_images/page_0001.png";
    coordinate.canonicalImageSha256 = entry.sourceRevisionSha256;
    coordinate.canonicalImageWidth = 100;
    coordinate.canonicalImageHeight = 200;
    coordinate.recognitionImageWidth = 100;
    coordinate.recognitionImageHeight = 200;
    coordinate.coordinateSpaceKind = L"paddleocr_pruned_result_pixels";
    OcrCoordinateSpaceMetadata coordinateRoundTrip =
        ParseOcrCoordinateSpace(OcrCoordinateSpaceToJson(coordinate));
    if (coordinateRoundTrip.canonicalImagePath != coordinate.canonicalImagePath ||
        coordinateRoundTrip.canonicalImageWidth != 100 ||
        coordinateRoundTrip.recognitionImageHeight != 200 ||
        coordinateRoundTrip.coordinateSpaceKind != coordinate.coordinateSpaceKind) {
        return Fail("coordinate metadata did not round-trip");
    }

    OcrPageAlignmentStatus alignment;
    alignment.pageIdentity = OcrAlignmentState::Verified;
    alignment.geometry = OcrAlignmentState::TextOnlyWarning;
    alignment.semantic = OcrAlignmentState::Ambiguous;
    alignment.overall = OcrAlignmentState::TextOnlyWarning;
    alignment.reason = L"diagnostic";
    OcrPageAlignmentStatus alignmentRoundTrip =
        ParseOcrPageAlignment(OcrPageAlignmentToJson(alignment));
    if (alignmentRoundTrip.pageIdentity != alignment.pageIdentity ||
        alignmentRoundTrip.geometry != alignment.geometry ||
        alignmentRoundTrip.semantic != alignment.semantic ||
        alignmentRoundTrip.reason != alignment.reason) {
        return Fail("alignment metadata did not round-trip");
    }


    DocumentOcrPageResult interactive;
    interactive.canonicalSourceMarkdown = L"hello";
    interactive.sourceRevisionSha256 = entry.sourceRevisionSha256;
    interactive.blocks.resize(1);
    interactive.blocks[0].id = entry.blockId;
    interactive.blockSourceMap = {entry};
    interactive.alignment.pageIdentity = OcrAlignmentState::Verified;
    interactive.alignment.geometry = OcrAlignmentState::Verified;
    interactive.alignment.semantic = OcrAlignmentState::Verified;
    interactive.alignment.overall = OcrAlignmentState::Verified;
    if (IsDocumentPageInteractiveAlignmentVerified(interactive)) {
        return Fail("mismatched source revision/range enabled interaction");
    }

    const std::wstring redacted = RedactDocumentOcrSensitiveText(
        L"Authorization: Bearer secret-token\n"
        L"{\"password\":\"hunter2\",\"jsonUrl\":\"https://cdn.example.com/a?sig=secret\"}\n"
        L"C:\\Users\\Alice\\private.pdf");
    if (redacted.find(L"secret-token") != std::wstring::npos ||
        redacted.find(L"hunter2") != std::wstring::npos ||
        redacted.find(L"sig=secret") != std::wstring::npos ||
        redacted.find(L"Alice") != std::wstring::npos ||
        redacted.find(L"<redacted>") == std::wstring::npos ||
        redacted.find(L"<local-path>") == std::wstring::npos) {
        return Fail("durable diagnostic redaction leaked sensitive text");
    }
    return true;
}

bool TestCapabilityRoutingAndProtocol() {
    NativePdfEligibilityInput input;
    input.featureFlagEnabled = true;
    input.gateProfileVerified = true;
    input.fullPdfConsentGranted = true;
    input.providerHealthy = true;
    input.sourceBytes = 1024;
    input.sourcePageCount = 3;
    input.requestedPages = {1, 2, 3};
    input.allPagesSelected = true;
    input.engineMode = L"paddle_cloud";
    input.model = L"PaddleOCR-VL-1.6";
    NativePdfEligibilityDecision decision = EvaluatePaddleCloudNativePdfEligibility(input);
    if (!decision.eligible || decision.transportKind != L"cloud_native_pdf" ||
        decision.canonicalPageRanges != L"1-3") {
        return Fail("eligible native PDF was routed to raster");
    }
    input.model = L"PP-OCRv5";
    if (EvaluatePaddleCloudNativePdfEligibility(input).eligible) {
        return Fail("retired PP-OCRv5 native PDF profile was accepted");
    }
    input.model = L"PaddleOCR-VL";
    if (EvaluatePaddleCloudNativePdfEligibility(input).eligible) {
        return Fail("unversioned PaddleOCR-VL native PDF profile was accepted");
    }
    input.model = L"PaddleOCR-VL-1.6";
    input.requiresPassword = true;
    if (EvaluatePaddleCloudNativePdfEligibility(input).eligible) {
        return Fail("password PDF was routed to native transport");
    }
    input.requiresPassword = false;
    input.sourceBytes = 50'000'001ull;
    if (EvaluatePaddleCloudNativePdfEligibility(input).eligible) {
        return Fail("oversized PDF was routed to native transport");
    }
    input.sourceBytes = 1024;
    input.allPagesSelected = false;
    if (EvaluatePaddleCloudNativePdfEligibility(input).eligible) {
        return Fail("partial page range bypassed the feature gate");
    }
    input.allPagesSelected = true;
    input.requestedPages = {1, 3};
    if (EvaluatePaddleCloudNativePdfEligibility(input).eligible) {
        return Fail("inconsistent all-pages selection was accepted");
    }
    input.allPagesSelected = false;
    input.allowPartialPageRanges = true;
    input.requestedPages = {4};
    if (EvaluatePaddleCloudNativePdfEligibility(input).eligible) {
        return Fail("out-of-range requested page was accepted");
    }
    input.allowPartialPageRanges = false;
    input.allPagesSelected = true;
    input.requestedPages = {1, 2, 3};

    const std::vector<unsigned char> pdf = {'%', 'P', 'D', 'F', '-', '1', '.', '7', 0, 0xff};
    PaddleCloudPdfMultipartRequest request = BuildPaddleCloudPdfMultipartRequest(
        pdf,
        "PaddleOCR-VL-1.6",
        "{\"useDocOrientationClassify\":false,\"useDocUnwarping\":false}",
        "1-3",
        "batch-contract",
        "----ZenCropContractBoundary");
    if (!request.error.empty() ||
        request.contentType.find("multipart/form-data; boundary=") != 0 ||
        request.body.find("name=\"model\"") == std::string::npos ||
        request.body.find("name=\"optionalPayload\"") == std::string::npos ||
        request.body.find("name=\"pageRanges\"") == std::string::npos ||
        request.body.find("name=\"batchId\"") == std::string::npos ||
        request.body.find("filename=\"document.pdf\"") == std::string::npos ||
        request.body.find("Content-Type: application/pdf") == std::string::npos ||
        request.body.find("fileType") != std::string::npos ||
        request.body.find("password") != std::string::npos) {
        return Fail("native PDF multipart wire contract mismatch");
    }
    std::string rawPdf(reinterpret_cast<const char*>(pdf.data()), pdf.size());
    if (request.body.find(rawPdf) == std::string::npos) {
        return Fail("native PDF multipart body did not preserve binary bytes");
    }
    if (BuildPaddleCloudPdfMultipartRequest(
            pdf, "PaddleOCR-VL-1.6", "{}", "1", "batch", "bad boundary").error.empty()) {
        return Fail("unsafe multipart boundary was accepted");
    }

    PaddleCloudFileUrlJsonRequest urlRequest = BuildPaddleCloudFileUrlJsonRequest(
        L"https://files.example.com/source.pdf?sig=private",
        L"PaddleOCR-VL-1.6",
        L"{\"useDocUnwarping\":false}",
        L"1-3",
        L"batch-contract");
    if (!urlRequest.error.empty() ||
        urlRequest.body.find("\"fileUrl\"") == std::string::npos ||
        urlRequest.body.find("\"optionalPayload\":{") == std::string::npos ||
        urlRequest.body.find("fileType") != std::string::npos) {
        return Fail("fileUrl JSON request contract mismatch");
    }
    if (BuildPaddleCloudFileUrlJsonRequest(
            L"https://files.example.com/source.pdf",
            L"PaddleOCR-VL-1.6",
            L"{\"broken\":}",
            L"1",
            L"batch").error.empty()) {
        return Fail("invalid fileUrl optionalPayload was accepted");
    }
    std::wstring authorization;
    std::wstring authorizationError;
    if (!BuildPaddleCloudAuthorizationHeader(
            L"Bearer token-value", authorization, authorizationError) ||
        authorization != L"Authorization: Bearer token-value" ||
        BuildPaddleCloudAuthorizationHeader(
            L"token-value\r\nX-Injected: yes", authorization, authorizationError)) {
        return Fail("authorization header validation mismatch");
    }

    PaddleCloudApiEnvelope accepted = ParsePaddleCloudApiEnvelope(
        200,
        R"({"code":0,"jobId":"ocrjob-contract","state":"pending"})",
        true,
        false);
    if (!accepted.success || !accepted.submitAccepted ||
        accepted.jobId != L"ocrjob-contract") {
        return Fail("successful submit envelope was rejected");
    }
    PaddleCloudApiEnvelope bodyError = ParsePaddleCloudApiEnvelope(
        200,
        R"({"code":10003,"msg":"invalid content"})",
        true,
        false);
    if (bodyError.success || bodyError.diagnosticCode != L"body_code_10003") {
        return Fail("HTTP 200 body error was accepted");
    }
    PaddleCloudApiEnvelope submit503 = ParsePaddleCloudApiEnvelope(
        503,
        R"({"msg":"queue full"})",
        true,
        false);
    if (!submit503.reconcileBeforeReplay || submit503.retrySameJob) {
        return Fail("ambiguous submit retry policy mismatch");
    }
    PaddleCloudApiEnvelope poll503 = ParsePaddleCloudApiEnvelope(
        503,
        R"({"msg":"temporarily unavailable"})",
        false,
        true);
    if (!poll503.retrySameJob || poll503.reconcileBeforeReplay) {
        return Fail("same-job poll retry policy mismatch");
    }
    PaddleCloudApiEnvelope invalidJson = ParsePaddleCloudApiEnvelope(
        200, R"({"code":0,})", true, false);
    if (invalidJson.success || invalidJson.submitAccepted) {
        return Fail("malformed Cloud API envelope was accepted");
    }
    PaddleCloudApiEnvelope invalidCode = ParsePaddleCloudApiEnvelope(
        200, R"({"code":"not-a-number","jobId":"unsafe-accept"})", true, false);
    if (invalidCode.success || invalidCode.submitAccepted ||
        invalidCode.diagnosticCode != L"body_code_invalid") {
        return Fail("invalid Cloud API body code was accepted");
    }
    PaddleCloudApiEnvelope oversizedEnvelope = ParsePaddleCloudApiEnvelope(
        200, std::string(1024 * 1024 + 1, 'x'), true, false);
    if (oversizedEnvelope.success || oversizedEnvelope.diagnosticCode != L"response_too_large") {
        return Fail("oversized Cloud API envelope was accepted");
    }

    std::wstring fingerprint;
    std::wstring error;
    if (!BuildPaddleCloudRequestFingerprint(
            L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            L"PaddleOCR-VL-1.6",
            L"1-3",
            L"{\"useDocUnwarping\":false}",
            fingerprint,
            error) ||
        !IsSha256Hex(fingerprint)) {
        return Fail("request fingerprint generation failed");
    }
    return true;
}

class MockDocumentHttpClient final : public IPaddleCloudDocumentHttpClient {
public:
    HttpResponse submitResponse;
    HttpResponse pollResponse;
    HttpResponse batchResponse;
    HttpResponse resourceResponse;
    std::wstring submitUrl;
    std::wstring pollUrl;
    std::wstring resourceUrl;
    std::wstring batchUrl;
    std::vector<std::wstring> submitHeaders;
    std::vector<std::wstring> pollHeaders;
    std::vector<std::wstring> resourceHeaders;
    std::vector<std::wstring> batchHeaders;
    std::string submitBody;

    HttpResponse Post(
        const std::wstring& url,
        const std::string& body,
        const std::vector<std::wstring>& headers,
        int) override
    {
        submitUrl = url;
        submitBody = body;
        submitHeaders = headers;
        return submitResponse;
    }

    HttpResponse Get(
        const std::wstring& url,
        const std::vector<std::wstring>& headers,
        int) override
    {
        if (url.find(L"/batch/") != std::wstring::npos) {
            batchUrl = url;
            batchHeaders = headers;
            return batchResponse;
        }
        if (url.find(L"ocrjob-transport") != std::wstring::npos) {
            pollUrl = url;
            pollHeaders = headers;
            return pollResponse;
        }
        resourceUrl = url;
        resourceHeaders = headers;
        return resourceResponse;
    }
};

bool HasAuthorization(const std::vector<std::wstring>& headers) {
    for (const auto& header : headers) {
        if (header.size() >= 14 && _wcsnicmp(header.c_str(), L"Authorization:", 14) == 0) {
            return true;
        }
    }
    return false;
}

bool WriteBinaryFixture(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(
        file,
        bytes.data(),
        static_cast<DWORD>(bytes.size()),
        &written,
        nullptr) && written == bytes.size();
    CloseHandle(file);
    return ok;
}

bool TestDocumentTransportWithMockHttp() {
    const std::wstring pdfPath =
        (ZenCropTestArtifactDirectory(L"cloud_native_pdf") /
            L"cloud_native_pdf_transport_fixture.pdf").wstring();
    if (!WriteBinaryFixture(pdfPath, "%PDF-1.7\ncontract\n%%EOF\n")) {
        return Fail("failed to create native PDF transport fixture");
    }

    MockDocumentHttpClient http;
    http.submitResponse.statusCode = 200;
    http.submitResponse.body = R"({"code":0,"jobId":"ocrjob-transport","state":"pending"})";
    PaddleCloudDocumentSubmitRequest submit;
    submit.sourcePdfPath = pdfPath;
    submit.jobsEndpoint = L"https://paddleocr.aistudio-app.com/api/v2/ocr/jobs";
    submit.bearerToken = L"contract-secret-token";
    submit.requestedPageNumbers = {1};
    submit.batchId = L"batch-transport";
    submit.model = L"PP-OCRv5";
    http.submitBody.clear();
    const PaddleCloudDocumentSubmitResult retiredModelSubmit =
        SubmitPaddleCloudDocument(submit, http);
    if (retiredModelSubmit.success ||
        retiredModelSubmit.error.find(L"PaddleOCR-VL-1.6") == std::wstring::npos ||
        !http.submitBody.empty()) {
        DeleteFileW(pdfPath.c_str());
        return Fail("retired native PDF model reached the submit transport");
    }
    submit.model = L" paddleocr-vl-1.6 ";
    PaddleCloudDocumentSubmitResult submitted = SubmitPaddleCloudDocument(submit, http);
    if (!submitted.success || submitted.remoteJob.jobId != L"ocrjob-transport" ||
        submitted.remoteJob.model != L"PaddleOCR-VL-1.6" ||
        submitted.remoteJob.pageRanges != L"1" ||
        !HasAuthorization(http.submitHeaders) ||
        http.submitBody.find("contract-secret-token") != std::string::npos ||
        !IsSha256Hex(submitted.sourcePdfSha256) ||
        !IsSha256Hex(submitted.requestFingerprint)) {
        DeleteFileW(pdfPath.c_str());
        return Fail("mock document submit contract mismatch");
    }
    http.submitResponse.contentType = L"application/jsonp";
    if (SubmitPaddleCloudDocument(submit, http).success) {
        DeleteFileW(pdfPath.c_str());
        return Fail("non-JSON submit content type was accepted");
    }
    http.submitResponse.contentType.clear();

    PaddleCloudDocumentUrlSubmitRequest urlSubmit;
    urlSubmit.fileUrl = L"https://files.example.com/document.pdf?sig=private";
    urlSubmit.jobsEndpoint = submit.jobsEndpoint;
    urlSubmit.bearerToken = submit.bearerToken;
    urlSubmit.requestedPageNumbers = {1};
    urlSubmit.batchId = L"batch-url";
    urlSubmit.model = L"PP-OCRv5";
    http.submitBody.clear();
    const PaddleCloudDocumentSubmitResult retiredUrlModelSubmit =
        SubmitPaddleCloudDocumentUrl(urlSubmit, http);
    if (retiredUrlModelSubmit.success ||
        retiredUrlModelSubmit.error.find(L"PaddleOCR-VL-1.6") == std::wstring::npos ||
        !http.submitBody.empty()) {
        DeleteFileW(pdfPath.c_str());
        return Fail("retired fileUrl model reached the submit transport");
    }
    urlSubmit.model = L" paddleocr-vl-1.6 ";
    PaddleCloudDocumentSubmitResult urlSubmitted =
        SubmitPaddleCloudDocumentUrl(urlSubmit, http);
    if (!urlSubmitted.success || urlSubmitted.remoteJob.jobId != L"ocrjob-transport" ||
        urlSubmitted.remoteJob.model != L"PaddleOCR-VL-1.6" ||
        !IsSha256Hex(urlSubmitted.sourcePdfSha256) ||
        urlSubmitted.remoteJob.requestFingerprint.empty() ||
        http.submitBody.find("files.example.com/document.pdf?sig=private") == std::string::npos ||
        http.submitBody.find("contract-secret-token") != std::string::npos) {
        DeleteFileW(pdfPath.c_str());
        return Fail("mock fileUrl document submit contract mismatch");
    }

    http.batchResponse.statusCode = 200;
    http.batchResponse.body =
        R"({"code":0,"jobs":[{"jobId":"ocrjob-transport","model":"PaddleOCR-VL-1.6","state":"running"}]})";
    PaddleCloudDocumentBatchQueryResult batch = QueryPaddleCloudDocumentBatch(
        submit.jobsEndpoint,
        submit.bearerToken,
        L"batch-url",
        1000,
        http);
    if (!batch.success || batch.jobs.size() != 1 ||
        batch.jobs[0].jobId != L"ocrjob-transport" ||
        batch.jobs[0].state != DocumentOcrTransportState::Running ||
        http.batchUrl.find(L"/batch/batch-url") == std::wstring::npos ||
        !HasAuthorization(http.batchHeaders)) {
        DeleteFileW(pdfPath.c_str());
        return Fail("mock Cloud batch query contract mismatch");
    }

    http.pollResponse.statusCode = 200;
    http.pollResponse.body = R"({"code":0,"state":"done","jsonUrl":"https://cdn.example.com/result.jsonl","extractProgress":{"extractedPages":1,"totalPages":1}})";
    PaddleCloudDocumentPollResult polled = PollPaddleCloudDocument(
        submit.jobsEndpoint,
        submit.bearerToken,
        submitted.remoteJob.jobId,
        1000,
        http);
    if (!polled.success || !polled.terminal ||
        polled.state != DocumentOcrTransportState::Downloading ||
        polled.jsonUrl != L"https://cdn.example.com/result.jsonl" ||
        polled.extractedPages != 1 || polled.totalPages != 1 ||
        !HasAuthorization(http.pollHeaders)) {
        DeleteFileW(pdfPath.c_str());
        return Fail("mock document poll contract mismatch");
    }
    http.pollResponse = {};
    http.pollResponse.error = L"temporary connection failure";
    PaddleCloudDocumentPollResult retryablePoll = PollPaddleCloudDocument(
        submit.jobsEndpoint,
        submit.bearerToken,
        submitted.remoteJob.jobId,
        1000,
        http);
    if (retryablePoll.success || !retryablePoll.retrySameJob) {
        DeleteFileW(pdfPath.c_str());
        return Fail("known-job poll transport failure was not retryable");
    }

    http.resourceResponse.statusCode = 200;
    http.resourceResponse.body = R"({"pageNumber":1,"prunedResult":{"width":100,"height":100,"parsing_res_list":[{"block_label":"text","block_content":"Native","block_bbox":[1,1,80,20],"block_id":"native"}]},"markdown":{"text":"Native"}})";
    PaddleCloudDocumentNormalizeOptions options;
    PaddleCloudDocumentDownloadResult downloaded = DownloadAndNormalizePaddleCloudDocument(
        polled.jsonUrl,
        {1},
        options,
        1000,
        http);
    DeleteFileW(pdfPath.c_str());
    if (!downloaded.success || downloaded.document.pages.size() != 1 ||
        downloaded.document.pages[0].originalPageNumber != 1 ||
        HasAuthorization(http.resourceHeaders) ||
        http.resourceUrl != L"https://cdn.example.com/result.jsonl") {
        return Fail("signed resource download leaked auth or failed normalization");
    }

    std::wstring urlError;
    if (IsSafePaddleCloudResourceUrl(L"http://cdn.example.com/result", urlError) ||
        IsSafePaddleCloudResourceUrl(L"https://localhost/result", urlError) ||
        IsSafePaddleCloudResourceUrl(L"https://cdn.example.com:444/result", urlError) ||
        IsSafePaddleCloudResourceUrl(L"https://10.0.0.1/result", urlError) ||
        IsSafePaddleCloudResourceUrl(L"https://8.8.8.8/result", urlError) ||
        IsSafePaddleCloudResourceUrl(L"https://169.254.169.254/latest/meta-data", urlError) ||
        IsSafePaddleCloudResourceUrl(L"https://[::1]/result", urlError) ||
        IsSafePaddleCloudResourceUrl(L"https://[fc00::1]/result", urlError)) {
        return Fail("unsafe resource URL was accepted");
    }
    if (IsOfficialPaddleCloudJobsEndpoint(L"https://evil.example/api/v2/ocr/jobs", urlError) ||
        IsOfficialPaddleCloudJobsEndpoint(L"http://paddleocr.aistudio-app.com/api/v2/ocr/jobs", urlError)) {
        return Fail("unsafe jobs endpoint was accepted for Authorization");
    }
    if (!IsSamePaddleCloudUrlTarget(
            L"https://CDN.example.com/result.jsonl?sig=one",
            L"https://cdn.example.com/result.jsonl?sig=one") ||
        IsSamePaddleCloudUrlTarget(
            L"https://cdn.example.com/Result.jsonl?sig=one",
            L"https://cdn.example.com/result.jsonl?sig=one") ||
        IsSamePaddleCloudUrlTarget(
            L"https://cdn.example.com/result.jsonl?sig=one",
            L"https://cdn.example.com/result.jsonl?sig=two") ||
        IsSamePaddleCloudUrlTarget(
            L"https://cdn.example.com/result.jsonl?sig=one",
            L"https://user@cdn.example.com/result.jsonl?sig=one")) {
        return Fail("signed resource redirect target comparison is unsafe");
    }
    return true;
}

} // namespace

int main() {
    if (!TestSha256AndPageRanges()) return 1;
    if (!TestExplicitPageNormalizationAndAlignment()) return 1;
    if (!TestStrictOrdinalFallback()) return 1;
    if (!TestOcrResultsNormalization()) return 1;
    if (!TestIdentityFailures()) return 1;
    if (!TestAmbiguousSourceAndGeometryBounds()) return 1;
    if (!TestMetadataRoundTrip()) return 1;
    if (!TestCapabilityRoutingAndProtocol()) return 1;
    if (!TestDocumentTransportWithMockHttp()) return 1;
    std::cout << "Cloud native PDF contract passed.\n";
    return 0;
}
