#include <winsock2.h>
#include <windows.h>
#include <gdiplus.h>

#include "core/Settings.h"
#include "image/BitmapCodec.h"
#include "ocr/engine/OcrEngine_PaddleOCR_Doc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

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
    settings.paddleDocGroupingMode = L"official_group";
    return settings;
}

HotkeySettings LoadHotkeySettings() {
    return {};
}

int ResolveOcrLlamaIdleTimeoutMin(
    const OcrSettings& settings,
    const HotkeySettings&)
{
    return settings.paddleLocalIdleTimeoutMin;
}

namespace {

[[noreturn]] void Fail(const std::wstring& message) {
    std::wcerr << L"FAIL: " << message << L"\n";
    OcrEnginePaddleDoc::GlobalCleanup();
    std::exit(1);
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
        long value = wcstol(json.c_str() + position, &end, 10);
        if (end != json.c_str() + position) values.push_back(static_cast<int>(value));
    }
    return values;
}

int Percentile(std::vector<int> values, double quantile) {
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

long long SumPositive(const std::vector<int>& values) {
    long long total = 0;
    for (int value : values) {
        if (value > 0) total += value;
    }
    return total;
}

uint64_t Fingerprint(const std::wstring& text) {
    uint64_t hash = 1469598103934665603ull;
    for (wchar_t character : text) {
        hash ^= static_cast<uint16_t>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t LayoutFingerprint(const OcrOutput& output) {
    std::wstring canonical;
    for (const auto& block : output.blocks) {
        canonical += block.id + L"|" + block.label + L"|" + block.groupId + L"|" +
            std::to_wstring(block.bbox.left) + L"," +
            std::to_wstring(block.bbox.top) + L"," +
            std::to_wstring(block.bbox.right) + L"," +
            std::to_wstring(block.bbox.bottom) + L";";
    }
    return Fingerprint(canonical);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcerr << L"Usage: test_paddle_doc_full_integration page-image "
            L"[--benchmark] [--crop-png|--crop-jpeg95] [--dump-text]\n";
        return 2;
    }
    bool benchmarkMode = false;
    bool dumpText = false;
    PaddleVlImageEncoding cropEncoding = PaddleVlImageEncoding::Png;
    for (int index = 2; index < argc; ++index) {
        if (_wcsicmp(argv[index], L"--benchmark") == 0) {
            benchmarkMode = true;
        } else if (_wcsicmp(argv[index], L"--crop-png") == 0) {
            cropEncoding = PaddleVlImageEncoding::Png;
        } else if (_wcsicmp(argv[index], L"--crop-jpeg95") == 0) {
            cropEncoding = PaddleVlImageEncoding::LegacyJpeg95;
        } else if (_wcsicmp(argv[index], L"--dump-text") == 0) {
            dumpText = true;
        } else {
            std::wcerr << L"Unknown option: " << argv[index] << L"\n";
            return 2;
        }
    }
    SetPaddleDocBenchmarkCropEncoding(cropEncoding);

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) Fail(L"WSAStartup failed");
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        Fail(L"GDI+ startup failed");
    }

    int pageWidth = 0;
    int pageHeight = 0;
    std::wstring imageError;
    HBITMAP bitmap = ImageCodec::LoadHBitmapFromFile(argv[1], &imageError);
    if (!bitmap) Fail(L"failed to load page image: " + imageError);
    BITMAP pageBitmap = {};
    if (GetObject(bitmap, sizeof(pageBitmap), &pageBitmap) != sizeof(pageBitmap)) {
        DeleteObject(bitmap);
        Fail(L"failed to inspect page bitmap");
    }
    pageWidth = pageBitmap.bmWidth;
    pageHeight = std::abs(pageBitmap.bmHeight);

    OcrEnginePaddleDoc engine;
    if (!engine.IsAvailable()) {
        DeleteObject(bitmap);
        Fail(L"PaddleDoc engine unavailable with copied integration settings");
    }

    HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!completed) {
        DeleteObject(bitmap);
        Fail(L"failed to create completion event");
    }
    OcrOutput output;
    engine.Recognize(bitmap, [&](OcrOutput value) {
        output = std::move(value);
        SetEvent(completed);
    });
    const DWORD wait = WaitForSingleObject(completed, 15 * 60 * 1000);
    CloseHandle(completed);
    if (wait != WAIT_OBJECT_0) Fail(L"full PaddleDoc recognition timed out");
    if (!output.success) Fail(L"recognition failed: " + output.error);
    if (output.blocks.size() < 18 || output.blocks.size() > 21) {
        Fail(L"official-profile block count left the accepted 18-21 range: " +
            std::to_wstring(output.blocks.size()));
    }
    if (output.bboxes.size() != output.blocks.size() ||
        output.bboxClasses.size() != output.blocks.size()) {
        Fail(L"overlay arrays do not preserve one entry per original region");
    }

    bool lowScoreSurvived = false;
    bool truePolygonSurvived = false;
    std::map<std::wstring, std::vector<const OcrLayoutBlock*>> groups;
    for (const auto& block : output.blocks) {
        if (block.confidence > 0.30 && block.confidence < 0.40) lowScoreSurvived = true;
        if (!IsBboxRectangle(block)) truePolygonSurvived = true;
        if (block.groupId.empty()) Fail(L"a final block has no page-local groupId");
        if (block.bbox.left < 0 || block.bbox.top < 0 ||
            block.bbox.right > pageWidth || block.bbox.bottom > pageHeight ||
            block.bbox.right <= block.bbox.left || block.bbox.bottom <= block.bbox.top) {
            Fail(L"a final block bbox is outside the page");
        }
        if (block.bbox.bottom - block.bbox.top > pageHeight / 2) {
            Fail(L"a giant union bbox survived into final blocks");
        }
        groups[block.groupId].push_back(&block);
    }
    if (!lowScoreSurvived) Fail(L"no 0.30-0.40 official V3 candidate survived");
    if (!truePolygonSurvived) Fail(L"no non-rect mask polygon survived to blocks");

    size_t multiMemberGroups = 0;
    for (const auto& [id, members] : groups) {
        size_t contentOwners = 0;
        for (const auto* member : members) {
            if (!member->content.empty()) ++contentOwners;
        }
        if (members.size() > 1) {
            ++multiMemberGroups;
            if (contentOwners > 1) {
                Fail(L"recognition group has more than one nonempty content owner: " + id);
            }
        }
    }

    if (output.diagnosticsJson.find(L"\"pipeline\":\"paddle_doc_grouped\"") ==
        std::wstring::npos) {
        Fail(L"grouped pipeline diagnostics are missing");
    }
    if (!benchmarkMode &&
        (DiagnosticInt(output.diagnosticsJson, L"recognizedGroups") != 17 ||
         DiagnosticInt(output.diagnosticsJson, L"skippedGroups") != 1 ||
         DiagnosticInt(output.diagnosticsJson, L"failedGroups") != 0)) {
        Fail(L"expected 17 recognized / 1 skipped / 0 failed groups");
    }
    if (output.diagnosticsJson.find(L"\"serverModelsReachable\":true") == std::wstring::npos ||
        output.diagnosticsJson.find(L"\"serverPropsReachable\":true") == std::wstring::npos ||
        output.diagnosticsJson.find(L"\"serverModelListed\":true") == std::wstring::npos ||
        output.diagnosticsJson.find(L"\"serverMultimodal\":true") == std::wstring::npos) {
        Fail(L"llama-server model/multimodal capability validation failed");
    }
    if (output.diagnosticsJson.find(L"\"mmprojMinPixels\":112896") == std::wstring::npos ||
        output.diagnosticsJson.find(L"\"mmprojMaxPixels\":1003520") == std::wstring::npos ||
        output.diagnosticsJson.find(L"\"serverVersion\":\"\"") != std::wstring::npos ||
        output.diagnosticsJson.find(L"\"serverBackend\":\"\"") != std::wstring::npos ||
        output.diagnosticsJson.find(L"\"modelSha256\":\"\"") != std::wstring::npos ||
        output.diagnosticsJson.find(L"\"mmprojSha256\":\"\"") != std::wstring::npos ||
        output.diagnosticsJson.find(L"\"modelSha256CacheHit\":") == std::wstring::npos ||
        output.diagnosticsJson.find(L"\"mmprojSha256CacheHit\":") == std::wstring::npos ||
        DiagnosticInt(output.diagnosticsJson, L"sha256Ms") < 0 ||
        output.diagnosticsJson.find(L"\"chatTemplatePath\":\"\"") != std::wstring::npos) {
        Fail(L"server/mmproj launch diagnostics contract failed");
    }
    const int layoutRaw = DiagnosticInt(output.diagnosticsJson, L"layoutFullRaw");
    const int layoutScore = DiagnosticInt(
        output.diagnosticsJson, L"layoutFullScorePassed");
    const int layoutNms = DiagnosticInt(output.diagnosticsJson, L"layoutFullNmsKept");
    const int layoutImage = DiagnosticInt(
        output.diagnosticsJson, L"layoutFullImageAreaKept");
    const int layoutClass = DiagnosticInt(
        output.diagnosticsJson, L"layoutFullClassModeKept");
    const int layoutOverlap = DiagnosticInt(
        output.diagnosticsJson, L"layoutFullOverlapKept");
    const int layoutFinal = DiagnosticInt(output.diagnosticsJson, L"layoutFullFinal");
    if (layoutRaw < layoutScore || layoutScore < layoutNms ||
        layoutNms < layoutImage || layoutImage < layoutClass ||
        layoutClass < layoutOverlap || layoutOverlap != layoutFinal ||
        layoutFinal != static_cast<int>(output.blocks.size()) ||
        DiagnosticInt(output.diagnosticsJson, L"layoutReturned") != layoutFinal) {
        Fail(L"layout stage-count telemetry is missing or non-conservative");
    }
    const auto attempts = ExtractIntegers(output.diagnosticsJson, L"attempts");
    if (attempts.empty() || std::any_of(attempts.begin(), attempts.end(),
            [](int value) { return value > 1; }) ||
        output.diagnosticsJson.find(L"\"errorCategory\":\"timeout\"") !=
            std::wstring::npos ||
        output.diagnosticsJson.find(L"suffix_repeat") != std::wstring::npos ||
        output.diagnosticsJson.find(L"full_repeat") != std::wstring::npos ||
        output.diagnosticsJson.find(L"line_repeat") != std::wstring::npos ||
        output.diagnosticsJson.find(L"\"finishReason\":\"length\"") !=
            std::wstring::npos) {
        Fail(L"retry/timeout/repetition/length guard distribution regressed");
    }
    std::vector<int> elapsed = ExtractIntegers(output.diagnosticsJson, L"elapsedMs");
    std::vector<int> imageBytes = ExtractIntegers(output.diagnosticsJson, L"imageBytes");
    std::vector<int> pngBytes = ExtractIntegers(output.diagnosticsJson, L"pngBytes");
    std::vector<int> requestBuildUs = ExtractIntegers(
        output.diagnosticsJson, L"requestBuildUs");
    std::vector<int> requestBytes = ExtractIntegers(
        output.diagnosticsJson, L"requestBytes");
    std::vector<int> promptTokens = ExtractIntegers(output.diagnosticsJson, L"promptTokens");
    std::vector<int> completionTokens = ExtractIntegers(
        output.diagnosticsJson, L"completionTokens");
    const auto serverSlots = ExtractIntegers(output.diagnosticsJson, L"serverTotalSlots");
    const auto serverContext = ExtractIntegers(output.diagnosticsJson, L"serverSlotContext");
    int p50 = Percentile(elapsed, 0.50);
    int p95 = Percentile(elapsed, 0.95);
    const long long imageTotal = SumPositive(imageBytes);
    const long long pngTotal = SumPositive(pngBytes);
    const long long requestBuildTotal = SumPositive(requestBuildUs);
    const long long requestTotal = SumPositive(requestBytes);
    const long long promptTotal = SumPositive(promptTokens);
    const long long completionTotal = SumPositive(completionTokens);
    const uint64_t layoutFingerprint = LayoutFingerprint(output);
    const uint64_t textFingerprint = Fingerprint(output.text);
    const wchar_t* cropEncodingName =
        cropEncoding == PaddleVlImageEncoding::LegacyJpeg95 ? L"jpeg95" : L"png";

    std::wcout << (benchmarkMode
            ? L"PaddleDoc full integration benchmark: page="
            : L"PaddleDoc full integration passed: page=") << pageWidth << L"x" << pageHeight
        << L" blocks=" << output.blocks.size()
        << L" groups=" << groups.size()
        << L" multiMemberGroups=" << multiMemberGroups
        << L" totalMs=" << output.elapsedMs
        << L" groupP50Ms=" << p50
        << L" groupP95Ms=" << p95
        << L" cropEncoding=" << cropEncodingName
        << L" imageBytes=" << imageTotal
        << L" pngBytes=" << pngTotal
        << L" requestBuildUs=" << requestBuildTotal
        << L" requestBytes=" << requestTotal
        << L" promptTokens=" << promptTotal
        << L" completionTokens=" << completionTotal
        << L" layoutMs=" << DiagnosticInt(output.diagnosticsJson, L"layoutMs")
        << L" cropMs=" << DiagnosticInt(output.diagnosticsJson, L"cropMs")
        << L" vlmWallMs=" << DiagnosticInt(output.diagnosticsJson, L"vlmWallMs")
        << L" markdownMs=" << DiagnosticInt(output.diagnosticsJson, L"markdownMs")
        << L" sha256Ms=" << DiagnosticInt(output.diagnosticsJson, L"sha256Ms")
        << L" modelHashCache=" <<
            (output.diagnosticsJson.find(L"\"modelSha256CacheHit\":true") !=
                std::wstring::npos ? L"hit" : L"miss")
        << L" mmprojHashCache=" <<
            (output.diagnosticsJson.find(L"\"mmprojSha256CacheHit\":true") !=
                std::wstring::npos ? L"hit" : L"miss")
        << L" slots=" << (serverSlots.empty() ? -1 : serverSlots.front())
        << L" context=" << (serverContext.empty() ? -1 : serverContext.front())
        << L" layoutSig=0x" << std::hex << layoutFingerprint
        << L" textSig=0x" << textFingerprint << std::dec
        << L" textChars=" << output.text.size() << L"\n";
    if (dumpText) {
        std::wcout << L"--- BEGIN RECOGNIZED TEXT ---\n" << output.text
            << L"\n--- END RECOGNIZED TEXT ---\n";
    }
    OcrEnginePaddleDoc::GlobalCleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    WSACleanup();
    return 0;
}
