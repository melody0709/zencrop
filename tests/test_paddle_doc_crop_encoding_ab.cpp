#include <winsock2.h>
#include <windows.h>
#include <gdiplus.h>

#include "core/Settings.h"
#include "image/BitmapCodec.h"
#include "ocr/engine/OcrEngine_PaddleOCR_Doc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
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

long long SumPositive(const std::vector<int>& values) {
    long long total = 0;
    for (int value : values) {
        if (value > 0) total += value;
    }
    return total;
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

std::wstring NormalizeForComparison(const std::wstring& text) {
    std::wstring normalized;
    normalized.reserve(text.size());
    for (wchar_t character : text) {
        if (!iswspace(character)) normalized.push_back(character);
    }
    return normalized;
}

int LcsSimilarityPermille(const std::wstring& first, const std::wstring& second) {
    if (first.empty() && second.empty()) return 1000;
    if (first.empty() || second.empty()) return 0;
    std::vector<int> previous(second.size() + 1, 0);
    std::vector<int> current(second.size() + 1, 0);
    for (wchar_t firstCharacter : first) {
        for (size_t index = 1; index <= second.size(); ++index) {
            if (firstCharacter == second[index - 1]) {
                current[index] = previous[index - 1] + 1;
            } else {
                current[index] = (std::max)(previous[index], current[index - 1]);
            }
        }
        std::swap(previous, current);
        std::fill(current.begin(), current.end(), 0);
    }
    return static_cast<int>((1000ll * previous.back()) /
        (std::max)(first.size(), second.size()));
}

struct RunMetrics {
    std::wstring name;
    int blocks = 0;
    int groups = 0;
    DWORD totalMs = 0;
    int layoutMs = -1;
    int vlmWallMs = -1;
    int groupP50Ms = -1;
    int groupP95Ms = -1;
    long long imageBytes = 0;
    long long requestBuildUs = 0;
    long long requestBytes = 0;
    long long promptTokens = 0;
    long long completionTokens = 0;
    uint64_t layoutFingerprint = 0;
    uint64_t textFingerprint = 0;
    std::wstring normalizedText;
};

RunMetrics RunRecognition(
    OcrEnginePaddleDoc& engine,
    const std::wstring& imagePath,
    PaddleVlImageEncoding encoding,
    const std::wstring& name)
{
    SetPaddleDocBenchmarkCropEncoding(encoding);
    std::wstring imageError;
    HBITMAP bitmap = ImageCodec::LoadHBitmapFromFile(imagePath, &imageError);
    if (!bitmap) Fail(L"failed to load page image: " + imageError);

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
    if (wait != WAIT_OBJECT_0) Fail(name + L" timed out");
    if (!output.success) Fail(name + L" failed: " + output.error);

    std::set<std::wstring> groups;
    for (const auto& block : output.blocks) {
        if (block.groupId.empty()) Fail(name + L" emitted an empty group id");
        groups.insert(block.groupId);
    }
    const std::wstring expectedMime = encoding == PaddleVlImageEncoding::LegacyJpeg95
        ? L"image/jpeg" : L"image/png";
    if (output.diagnosticsJson.find(L"\"imageMime\":\"" + expectedMime + L"\"") ==
        std::wstring::npos) {
        Fail(name + L" did not use the requested crop MIME");
    }

    RunMetrics metrics;
    metrics.name = name;
    metrics.blocks = static_cast<int>(output.blocks.size());
    metrics.groups = static_cast<int>(groups.size());
    metrics.totalMs = output.elapsedMs;
    metrics.layoutMs = DiagnosticInt(output.diagnosticsJson, L"layoutMs");
    metrics.vlmWallMs = DiagnosticInt(output.diagnosticsJson, L"vlmWallMs");
    const auto elapsed = ExtractIntegers(output.diagnosticsJson, L"elapsedMs");
    metrics.groupP50Ms = Percentile(elapsed, 0.50);
    metrics.groupP95Ms = Percentile(elapsed, 0.95);
    metrics.imageBytes = SumPositive(ExtractIntegers(output.diagnosticsJson, L"imageBytes"));
    metrics.requestBuildUs = SumPositive(
        ExtractIntegers(output.diagnosticsJson, L"requestBuildUs"));
    metrics.requestBytes = SumPositive(ExtractIntegers(output.diagnosticsJson, L"requestBytes"));
    metrics.promptTokens = SumPositive(ExtractIntegers(output.diagnosticsJson, L"promptTokens"));
    metrics.completionTokens = SumPositive(
        ExtractIntegers(output.diagnosticsJson, L"completionTokens"));
    metrics.layoutFingerprint = LayoutFingerprint(output);
    metrics.textFingerprint = Fingerprint(output.text);
    metrics.normalizedText = NormalizeForComparison(output.text);
    return metrics;
}

void PrintRun(const RunMetrics& metrics) {
    std::wcout << L"crop-ab run=" << metrics.name
        << L" blocks=" << metrics.blocks
        << L" groups=" << metrics.groups
        << L" totalMs=" << metrics.totalMs
        << L" layoutMs=" << metrics.layoutMs
        << L" vlmWallMs=" << metrics.vlmWallMs
        << L" groupP50Ms=" << metrics.groupP50Ms
        << L" groupP95Ms=" << metrics.groupP95Ms
        << L" imageBytes=" << metrics.imageBytes
        << L" requestBuildUs=" << metrics.requestBuildUs
        << L" requestBytes=" << metrics.requestBytes
        << L" promptTokens=" << metrics.promptTokens
        << L" completionTokens=" << metrics.completionTokens
        << L" layoutSig=0x" << std::hex << metrics.layoutFingerprint
        << L" textSig=0x" << metrics.textFingerprint << std::dec
        << L" textChars=" << metrics.normalizedText.size() << L"\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3 ||
        (argc == 3 && _wcsicmp(argv[2], L"--two-pass") != 0)) {
        std::wcerr << L"Usage: test_paddle_doc_crop_encoding_ab page-image [--two-pass]\n";
        return 2;
    }

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) Fail(L"WSAStartup failed");
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        Fail(L"GDI+ startup failed");
    }

    OcrEnginePaddleDoc engine;
    if (!engine.IsAvailable()) Fail(L"PaddleDoc engine unavailable");

    // PNG/JPEG/PNG keeps server/model state warm while also measuring the
    // natural same-format variance against which JPEG must be judged. The
    // two-pass form is reserved for expensive complex-document samples after
    // the ordinary-page baseline has established that natural variance.
    const bool twoPass = argc == 3;
    const RunMetrics pngFirst = RunRecognition(
        engine, argv[1], PaddleVlImageEncoding::Png, L"png-first");
    const RunMetrics jpeg95 = RunRecognition(
        engine, argv[1], PaddleVlImageEncoding::LegacyJpeg95, L"jpeg95");
    RunMetrics pngSecond;
    if (!twoPass) {
        pngSecond = RunRecognition(
            engine, argv[1], PaddleVlImageEncoding::Png, L"png-second");
    }

    PrintRun(pngFirst);
    PrintRun(jpeg95);
    if (!twoPass) PrintRun(pngSecond);

    if (pngFirst.layoutFingerprint != jpeg95.layoutFingerprint ||
        pngFirst.blocks != jpeg95.blocks || pngFirst.groups != jpeg95.groups ||
        (!twoPass && (pngFirst.layoutFingerprint != pngSecond.layoutFingerprint ||
            pngFirst.blocks != pngSecond.blocks || pngFirst.groups != pngSecond.groups))) {
        Fail(L"A/B no longer holds the layout/group plan fixed");
    }

    const int pngSelfSimilarity = twoPass ? -1 : LcsSimilarityPermille(
        pngFirst.normalizedText, pngSecond.normalizedText);
    const int jpegVsPngFirstSimilarity = LcsSimilarityPermille(
        jpeg95.normalizedText, pngFirst.normalizedText);
    const int jpegVsPngSecondSimilarity = twoPass ? -1 : LcsSimilarityPermille(
        jpeg95.normalizedText, pngSecond.normalizedText);
    std::wcout << L"crop-ab comparison"
        << L" pngSelfSimilarityPermille=" << pngSelfSimilarity
        << L" jpegVsPngFirstSimilarityPermille=" << jpegVsPngFirstSimilarity
        << L" jpegVsPngSecondSimilarityPermille=" << jpegVsPngSecondSimilarity
        << L" jpegImageByteReductionPct=" <<
            ((pngFirst.imageBytes - jpeg95.imageBytes) * 100 / pngFirst.imageBytes)
        << L" jpegRequestByteReductionPct=" <<
            ((pngFirst.requestBytes - jpeg95.requestBytes) * 100 / pngFirst.requestBytes)
        << L"\n";

    OcrEnginePaddleDoc::GlobalCleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    WSACleanup();
    return 0;
}
