#include <winsock2.h>
#include <windows.h>
#include <gdiplus.h>

#include "core/Settings.h"
#include "image/BitmapCodec.h"
#include "ocr/BitmapUtils.h"
#include "ocr/engine/OcrEngine_PaddleOCR_Doc.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

std::wstring g_matrixGroupingMode = L"official_group";

OcrSettings LoadOcrSettings() {
    wchar_t modelDir[MAX_PATH] = {};
    wchar_t layoutPath[MAX_PATH] = {};
    GetFullPathNameW(L"ocr\\paddleocr-vl-1.6", MAX_PATH, modelDir, nullptr);
    GetFullPathNameW(
        L"ocr\\shared\\PP-DocLayoutV3.onnx", MAX_PATH, layoutPath, nullptr);
    OcrSettings settings;
    settings.paddleLocalModelDir = modelDir;
    settings.docLayoutModelPath = layoutPath;
    settings.layoutModelFamily = L"pp_doclayout_v3";
    settings.layoutThresholdProfile = L"official";
    settings.paddleDocGroupingMode = g_matrixGroupingMode;
    settings.paddleVlMaxTokens = 4096;
    return settings;
}

HotkeySettings LoadHotkeySettings() { return {}; }

int ResolveOcrLlamaIdleTimeoutMin(
    const OcrSettings& settings,
    const HotkeySettings&)
{
    return settings.paddleLocalIdleTimeoutMin;
}

namespace {

enum class Transform {
    None,
    LeftHalf,
    StackTwice,
};

struct Sample {
    const wchar_t* name;
    const wchar_t* path;
    Transform transform;
    std::vector<std::wstring> expectedAny;
};

std::vector<int> ExtractIntegers(
    const std::wstring& json,
    const std::wstring& field)
{
    std::vector<int> values;
    const std::wstring needle = L"\"" + field + L"\":";
    size_t position = 0;
    while ((position = json.find(needle, position)) != std::wstring::npos) {
        position += needle.size();
        wchar_t* end = nullptr;
        const long value = wcstol(json.c_str() + position, &end, 10);
        if (end != json.c_str() + position) values.push_back(static_cast<int>(value));
    }
    return values;
}

int Percentile(std::vector<int> values, double quantile) {
    values.erase(std::remove_if(values.begin(), values.end(),
        [](int value) { return value <= 0; }), values.end());
    if (values.empty()) return -1;
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(
        std::ceil(quantile * static_cast<double>(values.size())));
    if (index == 0) index = 1;
    return values[(std::min)(index - 1, values.size() - 1)];
}

int DiagnosticInt(const std::wstring& json, const std::wstring& field) {
    const auto values = ExtractIntegers(json, field);
    return values.empty() ? -1 : values.front();
}

size_t CountSubstring(const std::wstring& text, const std::wstring& needle) {
    if (needle.empty()) return 0;
    size_t count = 0;
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::wstring::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

bool BitmapSize(HBITMAP bitmap, int& width, int& height) {
    BITMAP value = {};
    if (!bitmap || GetObject(bitmap, sizeof(value), &value) != sizeof(value)) return false;
    width = value.bmWidth;
    height = std::abs(value.bmHeight);
    return width > 0 && height > 0;
}

bool IsBboxRectangle(const OcrLayoutBlock& block) {
    if (block.polygon.size() != 4) return false;
    const float left = static_cast<float>(block.bbox.left);
    const float top = static_cast<float>(block.bbox.top);
    const float right = static_cast<float>(block.bbox.right);
    const float bottom = static_cast<float>(block.bbox.bottom);
    return block.polygon[0].x == left && block.polygon[0].y == top &&
        block.polygon[1].x == right && block.polygon[1].y == top &&
        block.polygon[2].x == right && block.polygon[2].y == bottom &&
        block.polygon[3].x == left && block.polygon[3].y == bottom;
}

HBITMAP StackBitmapTwice(HBITMAP source) {
    int width = 0;
    int height = 0;
    if (!BitmapSize(source, width, height)) return nullptr;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -(height * 2);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP stacked = CreateDIBSection(
        nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!stacked || !bits) {
        if (stacked) DeleteObject(stacked);
        return nullptr;
    }
    std::fill_n(static_cast<unsigned char*>(bits),
        static_cast<size_t>(width) * height * 2 * 4, 255);
    HDC sourceDc = CreateCompatibleDC(nullptr);
    HDC targetDc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldSource = SelectObject(sourceDc, source);
    HGDIOBJ oldTarget = SelectObject(targetDc, stacked);
    BitBlt(targetDc, 0, 0, width, height, sourceDc, 0, 0, SRCCOPY);
    BitBlt(targetDc, 0, height, width, height, sourceDc, 0, 0, SRCCOPY);
    SelectObject(sourceDc, oldSource);
    SelectObject(targetDc, oldTarget);
    DeleteDC(sourceDc);
    DeleteDC(targetDc);
    return stacked;
}

HBITMAP LoadSampleBitmap(const Sample& sample, std::wstring& error) {
    HBITMAP source = ImageCodec::LoadHBitmapFromFile(sample.path, &error);
    if (!source) return nullptr;
    if (sample.transform == Transform::None) return source;

    int width = 0;
    int height = 0;
    if (!BitmapSize(source, width, height)) {
        DeleteObject(source);
        error = L"invalid source bitmap dimensions";
        return nullptr;
    }
    HBITMAP transformed = nullptr;
    if (sample.transform == Transform::LeftHalf) {
        transformed = CropBitmap(source, RECT{0, 0, width / 2, height});
    } else {
        transformed = StackBitmapTwice(source);
    }
    DeleteObject(source);
    if (!transformed) error = L"failed to create transformed fixture";
    return transformed;
}

bool ContainsExpected(
    const std::wstring& content,
    const std::vector<std::wstring>& expectedAny)
{
    for (const auto& expected : expectedAny) {
        if (content.find(expected) != std::wstring::npos) return true;
    }
    return false;
}

bool RecognizeSync(
    OcrEnginePaddleDoc& engine,
    HBITMAP bitmap,
    OcrOutput& output,
    std::wstring& error)
{
    HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!completed) {
        DeleteObject(bitmap);
        error = L"CreateEvent failed";
        return false;
    }
    engine.Recognize(bitmap, [&](OcrOutput value) {
        output = std::move(value);
        SetEvent(completed);
    });
    const DWORD wait = WaitForSingleObject(completed, 15 * 60 * 1000);
    CloseHandle(completed);
    if (wait != WAIT_OBJECT_0) {
        error = L"recognition timeout";
        return false;
    }
    if (!output.success) {
        error = output.error.empty() ? L"recognition failed" : output.error;
        return false;
    }
    return true;
}

bool ValidateSample(
    const Sample& sample,
    const OcrOutput& output,
    int width,
    int height,
    std::wstring& error)
{
    if (output.blocks.empty() || output.blocks.size() != output.bboxes.size()) {
        error = L"block/bbox cardinality invariant failed";
        return false;
    }
    if (!ContainsExpected(output.text, sample.expectedAny)) {
        error = L"expected key field not retained";
        return false;
    }

    std::map<std::wstring, int> nonEmptyByGroup;
    for (const auto& block : output.blocks) {
        if (block.groupId.empty()) {
            error = L"missing groupId";
            return false;
        }
        if (block.bbox.left < 0 || block.bbox.top < 0 ||
            block.bbox.right > width || block.bbox.bottom > height ||
            block.bbox.right <= block.bbox.left || block.bbox.bottom <= block.bbox.top) {
            error = L"bbox outside page";
            return false;
        }
        if (!block.content.empty()) ++nonEmptyByGroup[block.groupId];
        if ((block.label == L"text" || block.label == L"vertical_text" ||
                block.label == L"content") &&
            block.bbox.bottom - block.bbox.top > height / 2) {
            error = L"giant text union block survived";
            return false;
        }
    }
    for (const auto& [group, count] : nonEmptyByGroup) {
        if (count > 1) {
            error = L"multiple content owners in group " + group;
            return false;
        }
    }
    if (output.diagnosticsJson.find(L"\"maxTokens\":4096") == std::wstring::npos ||
        output.diagnosticsJson.find(L"\"serverMultimodal\":true") == std::wstring::npos) {
        error = L"wire/server diagnostics contract missing";
        return false;
    }
    if (DiagnosticInt(output.diagnosticsJson, L"failedGroups") != 0 ||
        DiagnosticInt(output.diagnosticsJson, L"layoutReturned") !=
            static_cast<int>(output.blocks.size())) {
        error = L"failed group or layout telemetry mismatch";
        return false;
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        const std::wstring requested = argv[1];
        if (requested != L"official_group" && requested != L"legacy_union_ab" &&
            requested != L"none") {
            std::wcerr << L"Usage: test_paddle_doc_sample_matrix "
                L"[official_group|legacy_union_ab|none]\n";
            return 2;
        }
        g_matrixGroupingMode = requested;
    }
    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        WSACleanup();
        return 1;
    }

    const std::vector<Sample> samples = {
        {L"form_handwriting_seal",
            L"ocr\\ocr_paper\\交通事故查询函1\\page_images\\page_0001.webp",
            Transform::None, {L"交通事故查询函"}},
        {L"single_column_article",
            L"ocr\\ocr_paper\\双列公式编号文档\\source.png",
            Transform::LeftHalf, {L"electron spin", L"adiabatic electronic"}},
        {L"double_column_article",
            L"ocr\\ocr_paper\\双列文档.webp",
            Transform::None, {L"GFN2-xTB", L"零阶项"}},
        {L"invoice_table",
            L"ocr\\ocr_paper\\发票.jpg",
            Transform::None, {L"电子发票", L"发票"}},
        {L"merged_header_table",
            L"ocr\\ocr_paper\\多层表头与合并单元格.webp",
            Transform::None, {L"大棚", L"报价清单"}},
        {L"formula_number_image",
            L"ocr\\ocr_paper\\双列公式编号文档\\source.png",
            Transform::None, {L"conical intersections", L"David R. Yarkony"}},
        {L"mixed_vertical_horizontal",
            L"ocr\\ocr_paper\\横排与竖排混合文档.webp",
            Transform::None, {L"近视管理白皮书", L"Expert Consensus"}},
        {L"long_tile_double_column",
            L"ocr\\ocr_paper\\双列文档.webp",
            Transform::StackTwice, {L"GFN2-xTB", L"零阶项"}},
    };

    OcrEnginePaddleDoc engine;
    if (!engine.IsAvailable()) {
        std::wcerr << L"PaddleDoc sample matrix engine unavailable.\n";
        return 1;
    }

    bool allPassed = true;
    size_t passedSamples = 0;
    size_t aggregateRequests = 0;
    size_t aggregateRetries = 0;
    size_t aggregateTimeouts = 0;
    size_t aggregateRepetitionGuards = 0;
    size_t aggregateLengthFinishes = 0;
    size_t aggregateRecognized = 0;
    size_t aggregateSkipped = 0;
    long long aggregatePngBytes = 0;
    long long aggregateImageBytes = 0;
    long long aggregateCompletionTokens = 0;
    std::vector<int> aggregateTotalMs;
    std::vector<int> aggregateGroupMs;
    int serverSlots = -1;
    int serverContext = -1;
    std::wstring serverBackend;
    std::wstring serverVersion;
    for (const auto& sample : samples) {
        std::wstring error;
        HBITMAP bitmap = LoadSampleBitmap(sample, error);
        int width = 0;
        int height = 0;
        if (!bitmap || !BitmapSize(bitmap, width, height)) {
            std::wcerr << L"FAIL " << sample.name << L": load: " << error << L"\n";
            allPassed = false;
            continue;
        }

        OcrOutput output;
        if (!RecognizeSync(engine, bitmap, output, error) ||
            !ValidateSample(sample, output, width, height, error)) {
            std::wcerr << L"FAIL " << sample.name << L": " << error <<
                L" blocks=" << output.blocks.size() << L"\n";
            allPassed = false;
            continue;
        }

        const auto elapsed = ExtractIntegers(output.diagnosticsJson, L"elapsedMs");
        const auto imageBytes = ExtractIntegers(output.diagnosticsJson, L"imageBytes");
        const auto pngBytes = ExtractIntegers(output.diagnosticsJson, L"pngBytes");
        const auto completionTokens = ExtractIntegers(
            output.diagnosticsJson, L"completionTokens");
        const auto attempts = ExtractIntegers(output.diagnosticsJson, L"attempts");
        size_t requests = 0;
        size_t retries = 0;
        long long pngTotal = 0;
        long long imageTotal = 0;
        long long completionTotal = 0;
        for (int value : attempts) {
            if (value > 0) requests += static_cast<size_t>(value);
            if (value > 1) retries += static_cast<size_t>(value - 1);
        }
        for (int value : imageBytes) if (value > 0) imageTotal += value;
        for (int value : pngBytes) if (value > 0) pngTotal += value;
        for (int value : completionTokens) if (value > 0) completionTotal += value;
        double maxAreaRatio = 0.0;
        double maxTextHeightRatio = 0.0;
        size_t lowScoreBlocks = 0;
        size_t truePolygons = 0;
        for (const auto& block : output.blocks) {
            const double area = static_cast<double>(block.bbox.right - block.bbox.left) *
                (block.bbox.bottom - block.bbox.top);
            maxAreaRatio = (std::max)(maxAreaRatio, area / (width * static_cast<double>(height)));
            if (block.label == L"text" || block.label == L"vertical_text" ||
                block.label == L"content") {
                maxTextHeightRatio = (std::max)(maxTextHeightRatio,
                    (block.bbox.bottom - block.bbox.top) /
                        static_cast<double>(height));
            }
            if (block.confidence > 0.30 && block.confidence < 0.40) ++lowScoreBlocks;
            if (!IsBboxRectangle(block)) ++truePolygons;
        }

        const size_t timeouts = CountSubstring(
            output.diagnosticsJson, L"\"errorCategory\":\"timeout\"");
        const size_t repetitionGuards =
            CountSubstring(output.diagnosticsJson, L"suffix_repeat") +
            CountSubstring(output.diagnosticsJson, L"full_repeat") +
            CountSubstring(output.diagnosticsJson, L"line_repeat");
        const size_t lengthFinishes = CountSubstring(
            output.diagnosticsJson, L"\"finishReason\":\"length\"");
        ++passedSamples;
        aggregateRequests += requests;
        aggregateRetries += retries;
        aggregateTimeouts += timeouts;
        aggregateRepetitionGuards += repetitionGuards;
        aggregateLengthFinishes += lengthFinishes;
        aggregateRecognized += static_cast<size_t>((std::max)(
            0, DiagnosticInt(output.diagnosticsJson, L"recognizedGroups")));
        aggregateSkipped += static_cast<size_t>((std::max)(
            0, DiagnosticInt(output.diagnosticsJson, L"skippedGroups")));
        aggregatePngBytes += pngTotal;
        aggregateImageBytes += imageTotal;
        aggregateCompletionTokens += completionTotal;
        aggregateTotalMs.push_back(static_cast<int>((std::min)(
            static_cast<DWORD>(INT_MAX), output.elapsedMs)));
        aggregateGroupMs.insert(
            aggregateGroupMs.end(), elapsed.begin(), elapsed.end());
        if (serverSlots < 0) {
            serverSlots = DiagnosticInt(output.diagnosticsJson, L"serverTotalSlots");
            serverContext = DiagnosticInt(output.diagnosticsJson, L"serverSlotContext");
            serverBackend = ExtractJsonField(output.diagnosticsJson, L"serverBackend");
            serverVersion = ExtractJsonField(output.diagnosticsJson, L"serverVersion");
        }

        std::wcout << L"PASS mode=" << g_matrixGroupingMode << L" " << sample.name
            << L" size=" << width << L"x" << height
            << L" blocks=" << output.blocks.size()
            << L" groups=" << DiagnosticInt(output.diagnosticsJson, L"groupCount")
            << L" requests=" << requests
            << L" retries=" << retries
            << L" recognized=" << DiagnosticInt(output.diagnosticsJson, L"recognizedGroups")
            << L" skipped=" << DiagnosticInt(output.diagnosticsJson, L"skippedGroups")
            << L" totalMs=" << output.elapsedMs
            << L" p50Ms=" << Percentile(elapsed, 0.50)
            << L" p95Ms=" << Percentile(elapsed, 0.95)
            << L" layoutMs=" << DiagnosticInt(output.diagnosticsJson, L"layoutMs")
            << L" cropMs=" << DiagnosticInt(output.diagnosticsJson, L"cropMs")
            << L" vlmWallMs=" << DiagnosticInt(output.diagnosticsJson, L"vlmWallMs")
            << L" markdownMs=" << DiagnosticInt(output.diagnosticsJson, L"markdownMs")
            << L" layoutStages="
            << DiagnosticInt(output.diagnosticsJson, L"layoutFullRaw") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutFullScorePassed") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutFullNmsKept") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutFullClassModeKept") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutFullOverlapKept")
            << L" tileUsed=" <<
                (output.diagnosticsJson.find(L"\"layoutUsedTiled\":true") !=
                    std::wstring::npos ? 1 : 0)
            << L" tileStages="
            << DiagnosticInt(output.diagnosticsJson, L"layoutTileRaw") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutTileScorePassed") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutTileNmsKept") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutTileClassModeKept") << L"/"
            << DiagnosticInt(output.diagnosticsJson, L"layoutTileOverlapKept")
            << L" imageBytes=" << imageTotal
            << L" pngBytes=" << pngTotal
            << L" completionTokens=" << completionTotal
            << L" maxAreaRatio=" << maxAreaRatio
            << L" maxTextHeightRatio=" << maxTextHeightRatio
            << L" lowScore=" << lowScoreBlocks
            << L" nonQuadPolygon=" << truePolygons
            << L" timeouts=" << timeouts
            << L" repetitionGuards=" << repetitionGuards
            << L" lengthFinishes=" << lengthFinishes
            << L" keyFieldRetained=1\n";
    }

    OcrEnginePaddleDoc::GlobalCleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    WSACleanup();
    if (!allPassed) return 1;
    std::wcout << L"SUMMARY mode=" << g_matrixGroupingMode
        << L" samples=" << passedSamples
        << L" totalP50Ms=" << Percentile(aggregateTotalMs, 0.50)
        << L" totalP95Ms=" << Percentile(aggregateTotalMs, 0.95)
        << L" groupP50Ms=" << Percentile(aggregateGroupMs, 0.50)
        << L" groupP95Ms=" << Percentile(aggregateGroupMs, 0.95)
        << L" requests=" << aggregateRequests
        << L" retries=" << aggregateRetries
        << L" recognized=" << aggregateRecognized
        << L" skipped=" << aggregateSkipped
        << L" imageBytes=" << aggregateImageBytes
        << L" pngBytes=" << aggregatePngBytes
        << L" completionTokens=" << aggregateCompletionTokens
        << L" timeouts=" << aggregateTimeouts
        << L" repetitionGuards=" << aggregateRepetitionGuards
        << L" lengthFinishes=" << aggregateLengthFinishes
        << L" fieldsRetained=" << passedSamples << L"/" << samples.size()
        << L" backend=" << serverBackend
        << L" slots=" << serverSlots
        << L" context=" << serverContext
        << L" version=" << serverVersion << L"\n";
    std::wcout << L"PaddleDoc multi-type sample matrix passed.\n";
    return 0;
}
