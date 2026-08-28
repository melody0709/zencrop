#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#include "Settings.h"
#include "MiniHttpServer.h"
#include "ocr/engine/OcrEngine.h"
#include "ocr/engine/OcrEngine_PPOCRv6_ONNX.h"
#include "ocr/engine/OcrEngine_PaddleOCR_Cloud.h"
#include "ocr/engine/OcrEngine_PaddleOCR_Doc.h"
#include "ocr/engine/OcrEngine_PaddleOCR_Local.h"

namespace {

std::wstring GetEnvWide(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return L"";
    std::wstring value(needed, L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0) return L"";
    value.resize(written);
    return value;
}

bool EnvEnabled(const wchar_t* name) {
    std::wstring value = GetEnvWide(name);
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value == L"1" || value == L"true" || value == L"yes" || value == L"on";
}

int EnvInt(const wchar_t* name, int fallback) {
    std::wstring value = GetEnvWide(name);
    if (value.empty()) return fallback;
    int parsed = _wtoi(value.c_str());
    return parsed == 0 && value != L"0" ? fallback : parsed;
}

std::wstring EnvOr(const wchar_t* primary, const wchar_t* fallbackName) {
    std::wstring value = GetEnvWide(primary);
    if (!value.empty()) return value;
    return fallbackName ? GetEnvWide(fallbackName) : L"";
}

HBITMAP CreateSmokeBitmap() {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 900, 260);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    RECT rc{0, 0, 900, 260};
    FillRect(mem, &rc, white);
    DeleteObject(white);

    HFONT font = CreateFontW(
        88, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
    HGDIOBJ oldFont = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0, 0, 0));
    RECT textRect{36, 48, 864, 210};
    // Use one stable word boundary so this end-to-end smoke isolates the CTC
    // ASCII-space class. The assertion below only collapses whitespace runs,
    // then requires the exact logical line. Multi-space sequence coverage stays
    // in the hermetic CTC test, where model variance cannot mask decoder bugs.
    DrawTextW(mem, L"ZEN CROP123", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(mem, oldFont);
    DeleteObject(font);
    SelectObject(mem, oldBitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

std::wstring NormalizeSmokeLine(const std::wstring& value) {
    std::wstring normalized;
    normalized.reserve(value.size());
    bool pendingSpace = false;
    for (wchar_t ch : value) {
        if (ch == L' ' || ch == L'\t') {
            if (!normalized.empty()) pendingSpace = true;
            continue;
        }
        if (pendingSpace) normalized.push_back(L' ');
        pendingSpace = false;
        normalized.push_back((wchar_t)towlower(ch));
    }
    return normalized;
}

bool LooksLikeExpectedOcr(const std::wstring& text) {
    std::wstring lower = Lower(text);
    return lower.find(L"zen") != std::wstring::npos ||
           lower.find(L"crop") != std::wstring::npos ||
           lower.find(L"123") != std::wstring::npos ||
           lower.find(L"zencrop") != std::wstring::npos;
}

// PP-OCRv6 P0: ASCII spaces must survive CTC decode (class 18709).
// Collapse only runs of ASCII whitespace, then require the exact logical line.
// This still rejects the pre-fix glued form "ZENCROP123" and any missing gap.
bool LooksLikePPOcrV6SpacePreservingOcr(const std::wstring& text) {
    // Scan lines (CRLF / LF); one normalized line must be exactly the target.
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (NormalizeSmokeLine(line) == L"zen crop123") return true;
        if (end == text.size()) break;
        start = end + 1;
        if (start < text.size() && text[end] == L'\r' && text[start] == L'\n') start++;
    }
    return false;
}

// PP-OCRv6 GOAL-A: when smoke is requested, require structured blocks that match text.
bool VerifyPPOcrV6Blocks(const OcrOutput& output, int bitmapW, int bitmapH, std::wstring& error) {
    if (output.blocks.empty()) {
        error = L"PP-OCRv6 smoke returned empty blocks";
        return false;
    }
    if (output.blocks.size() != output.bboxes.size() ||
        output.blocks.size() != output.bboxClasses.size()) {
        error = L"PP-OCRv6 blocks/bboxes/bboxClasses size mismatch";
        return false;
    }

    std::vector<std::wstring> lines;
    lines.reserve(output.blocks.size());
    std::set<std::wstring> ids;
    for (size_t i = 0; i < output.blocks.size(); ++i) {
        const auto& block = output.blocks[i];
        if (block.content.empty()) {
            error = L"PP-OCRv6 block content empty at index " + std::to_wstring(i);
            return false;
        }
        if (block.label != L"text") {
            error = L"PP-OCRv6 block label is not text: " + block.label;
            return false;
        }
        if (block.source != L"ppocrv6_onnx") {
            error = L"PP-OCRv6 block source is not ppocrv6_onnx: " + block.source;
            return false;
        }
        if (block.pageIndex != 0) {
            error = L"PP-OCRv6 block pageIndex expected 0";
            return false;
        }
        if (block.order != static_cast<int>(i) + 1) {
            error = L"PP-OCRv6 block order is not dense 1..N";
            return false;
        }
        if (block.id.find(L"page_1:ppocrv6_line_") != 0) {
            error = L"PP-OCRv6 block id missing prefix: " + block.id;
            return false;
        }
        if (!ids.insert(block.id).second) {
            error = L"PP-OCRv6 block id not unique: " + block.id;
            return false;
        }
        if (output.bboxClasses[i] != L"text") {
            error = L"PP-OCRv6 bboxClasses entry is not text";
            return false;
        }
        const RECT& bb = block.bbox;
        if (bb.right <= bb.left || bb.bottom <= bb.top) {
            error = L"PP-OCRv6 block bbox invalid";
            return false;
        }
        // Soft bounds: allow slight overflow from unclip; reject absurd values.
        if (bb.left < -bitmapW || bb.top < -bitmapH ||
            bb.right > bitmapW * 2 || bb.bottom > bitmapH * 2) {
            error = L"PP-OCRv6 block bbox far outside image";
            return false;
        }
        if (!(block.confidence >= 0.0 && block.confidence <= 1.0)) {
            error = L"PP-OCRv6 block confidence out of [0,1]";
            return false;
        }
        if (!block.polygon.empty()) {
            if (block.polygon.size() < 3) {
                error = L"PP-OCRv6 polygon has fewer than 3 points";
                return false;
            }
            for (const auto& p : block.polygon) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                    error = L"PP-OCRv6 polygon has non-finite point";
                    return false;
                }
            }
        }
        lines.push_back(block.content);
    }

    // Rebuild with production CRLF join.
    std::wstring rebuilt;
    for (const auto& line : lines) {
        if (line.empty()) continue;
        if (!rebuilt.empty()) rebuilt += L"\r\n";
        rebuilt += line;
    }
    if (rebuilt != output.text) {
        error = L"PP-OCRv6 blocks rebuild (CRLF) does not match output.text";
        return false;
    }
    return true;
}

bool RunRecognizeSmoke(
    IOcrEngine& engine,
    DWORD timeoutMs,
    std::wstring& error,
    bool requirePPOcrV6Blocks)
{
    HBITMAP bitmap = CreateSmokeBitmap();
    if (!bitmap) {
        error = L"failed to create test bitmap";
        return false;
    }

    BITMAP bm = {};
    GetObject(bitmap, sizeof(BITMAP), &bm);
    const int bitmapW = bm.bmWidth > 0 ? bm.bmWidth : 900;
    const int bitmapH = bm.bmHeight > 0 ? bm.bmHeight : 260;

    // Heap state so a late WorkerThread callback cannot write into a dead stack
    // frame if the wait times out (Recognize still owns the HBITMAP).
    struct SmokeCompletion {
        OcrOutput output;
        std::atomic<bool> callbackCalled{false};
        HANDLE done = nullptr;
        ~SmokeCompletion() {
            if (done) {
                CloseHandle(done);
                done = nullptr;
            }
        }
    };
    auto completion = std::make_shared<SmokeCompletion>();
    completion->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!completion->done) {
        DeleteObject(bitmap);
        error = L"failed to create completion event";
        return false;
    }

    // Recognize takes ownership of the HBITMAP and frees it in WorkerThread.
    // Never DeleteObject(bitmap) after this call.
    engine.Recognize(bitmap, [completion](OcrOutput result) {
        completion->output = std::move(result);
        completion->callbackCalled.store(true);
        if (completion->done) SetEvent(completion->done);
    });
    bitmap = nullptr;

    DWORD wait = WaitForSingleObject(completion->done, timeoutMs);
    if (wait != WAIT_OBJECT_0 || !completion->callbackCalled.load()) {
        // Lambda captured `completion` by value; the still-running WorkerThread
        // keeps that shared_ptr (and this state) alive until the callback fires.
        // Do not intentionally leak, and do not free the HBITMAP (engine owns it).
        error = L"recognize callback timed out";
        return false;
    }
    const OcrOutput& output = completion->output;
    if (!output.success) {
        error = output.error.empty() ? L"recognize returned success=false" : output.error;
        return false;
    }
    if (output.text.empty()) {
        error = L"recognize returned empty text";
        return false;
    }
    if (requirePPOcrV6Blocks) {
        // P0: exact/space-preserving assertion — substring zen|crop|123 is not enough.
        if (!LooksLikePPOcrV6SpacePreservingOcr(output.text)) {
            error = L"PP-OCRv6 recognize lost ASCII spaces or unexpected text: " + output.text;
            return false;
        }
        if (!VerifyPPOcrV6Blocks(output, bitmapW, bitmapH, error)) {
            return false;
        }
        // Block content must also keep internal spaces (writer must not trim).
        bool anySpace = false;
        for (const auto& block : output.blocks) {
            if (block.content.find(L' ') != std::wstring::npos) {
                anySpace = true;
                break;
            }
        }
        if (!anySpace) {
            error = L"PP-OCRv6 blocks lost internal ASCII spaces";
            return false;
        }
    } else if (!LooksLikeExpectedOcr(output.text)) {
        error = L"recognize returned unexpected text: " + output.text;
        return false;
    }
    return true;
}

bool CheckAvailability(const wchar_t* label, IOcrEngine& engine, bool expectAvailable, bool& available) {
    available = engine.IsAvailable();
    std::wcout << label << L": " << (available ? L"available" : L"unavailable") << L"\n";
    if (expectAvailable && !available) {
        std::wcerr << label << L" was explicitly requested but is unavailable.\n";
        return false;
    }
    return true;
}

bool RunOptionalSmoke(
    const wchar_t* label,
    const wchar_t* runEnv,
    IOcrEngine& engine,
    bool available,
    DWORD timeoutMs,
    bool runByDefault,
    bool requirePPOcrV6Blocks = false)
{
    bool shouldRun = runByDefault || EnvEnabled(runEnv);
    if (!shouldRun) {
        std::wcout << label << L": smoke skipped; set " << runEnv << L"=1 to run recognition.\n";
        return true;
    }
    if (!available) {
        std::wcerr << label << L": smoke requested but engine is unavailable.\n";
        return false;
    }
    std::wstring error;
    if (!RunRecognizeSmoke(engine, timeoutMs, error, requirePPOcrV6Blocks)) {
        std::wcerr << label << L": smoke failed: " << error << L"\n";
        return false;
    }
    std::wcout << label << L": smoke passed"
               << (requirePPOcrV6Blocks ? L" (with blocks)." : L".") << L"\n";
    return true;
}

bool VerifyPaddleCloudUploadContract() {
    HBITMAP bitmap = CreateSmokeBitmap();
    if (!bitmap) {
        std::wcerr << L"Paddle Cloud upload contract could not create a bitmap.\n";
        return false;
    }

    PaddleCloudRequest::UploadImage upload =
        PaddleCloudRequest::EncodeUploadImage(bitmap, 95);
    DeleteObject(bitmap);

    if (upload.usedPngFallback ||
        upload.filename != "zencrop.jpg" ||
        upload.contentType != "image/jpeg" ||
        upload.bytes.size() < 4 ||
        upload.bytes[0] != 0xFF || upload.bytes[1] != 0xD8 ||
        upload.bytes[upload.bytes.size() - 2] != 0xFF ||
        upload.bytes.back() != 0xD9) {
        std::wcerr << L"Paddle Cloud upload was not encoded as a valid JPEG payload.\n";
        return false;
    }

    const std::string boundary = "----ZenCropCloudUploadContract";
    const std::string optionalPayload =
        "{\"useDocOrientationClassify\":false,\"useDocUnwarping\":false}";
    std::string body = PaddleCloudRequest::BuildMultipartBody(
        upload,
        "PaddleOCR-VL-1.6",
        optionalPayload,
        boundary);
    const std::string encodedBytes(
        reinterpret_cast<const char*>(upload.bytes.data()), upload.bytes.size());
    if (body.find("filename=\"zencrop.jpg\"") == std::string::npos ||
        body.find("Content-Type: image/jpeg\r\n\r\n") == std::string::npos ||
        body.find("Content-Type: image/png") != std::string::npos ||
        body.find(optionalPayload) == std::string::npos ||
        body.find(encodedBytes) == std::string::npos ||
        body.rfind("\r\n--" + boundary + "--\r\n") == std::string::npos) {
        std::wcerr << L"Paddle Cloud multipart body does not match the JPEG upload contract.\n";
        return false;
    }

    std::wcout << L"Paddle Cloud JPEG upload contract passed ("
               << upload.bytes.size() << L" bytes).\n";
    return true;
}

} // namespace

OcrSettings LoadOcrSettings() {
    OcrSettings settings;
    settings.language = L"auto";
    settings.mode = L"local";

    settings.ppocrv6ModelDir = EnvOr(L"ZENCROP_PPOCRV6_MODEL_DIR", L"PPOCRV6_MODEL_DIR");
    settings.ppocrv6Variant = EnvOr(L"ZENCROP_PPOCRV6_VARIANT", nullptr);
    if (settings.ppocrv6Variant.empty()) settings.ppocrv6Variant = L"small";
    settings.ppocrv6CpuThreads = EnvInt(L"ZENCROP_PPOCRV6_CPU_THREADS", 4);
    settings.ppocrv6RecBatchSize = EnvInt(L"ZENCROP_PPOCRV6_REC_BATCH_SIZE", 1);
    settings.ppocrv6DetLimitSideLen = EnvInt(L"ZENCROP_PPOCRV6_DET_LIMIT_SIDE_LEN", 64);
    settings.ppocrv6DetLimitType = EnvOr(L"ZENCROP_PPOCRV6_DET_LIMIT_TYPE", nullptr);
    if (settings.ppocrv6DetLimitType.empty()) settings.ppocrv6DetLimitType = L"min";
    settings.ppocrv6DetMaxSideLimit = EnvInt(L"ZENCROP_PPOCRV6_DET_MAX_SIDE_LIMIT", 4000);
    settings.ppocrv6DetThreshPct = EnvInt(L"ZENCROP_PPOCRV6_DET_THRESH_PCT", 20);
    settings.ppocrv6DetBoxThreshPct = EnvInt(L"ZENCROP_PPOCRV6_DET_BOX_THRESH_PCT", 45);
    settings.ppocrv6DetUnclipRatioPct = EnvInt(L"ZENCROP_PPOCRV6_DET_UNCLIP_RATIO_PCT", 140);
    settings.ppocrv6RecScoreThreshPct = EnvInt(L"ZENCROP_PPOCRV6_REC_SCORE_THRESH_PCT", 0);

    settings.paddleLocalModelDir = EnvOr(L"ZENCROP_PADDLE_LOCAL_MODEL_DIR", L"PADDLE_LOCAL_MODEL_DIR");
    settings.paddleLocalPort = EnvInt(L"ZENCROP_PADDLE_LOCAL_PORT", 0);
    settings.paddleLocalIdleTimeoutMin = EnvInt(L"ZENCROP_PADDLE_LOCAL_IDLE_TIMEOUT_MIN", 1);
    settings.paddleLocalPrompt = EnvOr(L"ZENCROP_PADDLE_LOCAL_PROMPT", nullptr);
    if (settings.paddleLocalPrompt.empty()) settings.paddleLocalPrompt = L"OCR:";
    settings.enableDocParsing = EnvEnabled(L"ZENCROP_ENABLE_DOC_PARSING");
    settings.docLayoutModelPath = EnvOr(L"ZENCROP_DOC_LAYOUT_MODEL_PATH", nullptr);

    settings.paddleApiUrl = EnvOr(L"ZENCROP_PADDLE_API_URL", nullptr);
    if (settings.paddleApiUrl.empty()) {
        settings.paddleApiUrl = L"https://paddleocr.aistudio-app.com/api/v2/ocr/jobs";
    }
    settings.paddleToken = EnvOr(L"ZENCROP_PADDLE_TOKEN", L"PADDLE_TOKEN");
    settings.timeoutMs = EnvInt(L"ZENCROP_PADDLE_CLOUD_TIMEOUT_MS", 120000);
    return settings;
}

void SaveOcrSettings(const OcrSettings&) {}

HotkeySettings LoadHotkeySettings() {
    return HotkeySettings{};
}

std::wstring NormalizeOcrRoute(const std::wstring& route) {
    if (route == L"local" ||
        route == L"paddle_cloud" ||
        route == L"paddle_local" ||
        route == L"paddle_local_doc" ||
        route == L"ppocrv6_onnx") {
        return route;
    }
    if (route == L"paddle_doc" || route == L"doc_parsing") return L"paddle_local_doc";
    return L"current";
}

int ResolveOcrLlamaIdleTimeoutMin(const OcrSettings& settings, const HotkeySettings& hotkeys) {
    auto usesLlama = [](const std::wstring& route) {
        std::wstring normalized = NormalizeOcrRoute(route);
        return normalized == L"paddle_local" || normalized == L"paddle_local_doc";
    };

    bool mainUsesLlama = usesLlama(settings.mode);
    bool altUsesLlama = !hotkeys.ocrAlt.IsEmpty() && usesLlama(settings.altHotkeyRoute);
    int mainMinutes = (std::min)(240, (std::max)(0, settings.paddleLocalIdleTimeoutMin));
    int altMinutes = (std::min)(240, (std::max)(0, settings.altHotkeyIdleTimeoutMin));
    if (mainUsesLlama && altUsesLlama) {
        if (mainMinutes <= 0) return altMinutes;
        if (altMinutes <= 0) return mainMinutes;
        return (std::min)(mainMinutes, altMinutes);
    }
    if (altUsesLlama) return altMinutes;
    return mainMinutes;
}

MiniHttpServer::MiniHttpServer() = default;
MiniHttpServer::~MiniHttpServer() = default;
MiniHttpServer& MiniHttpServer::Instance() {
    static MiniHttpServer instance;
    return instance;
}
bool MiniHttpServer::Start(unsigned short) { return false; }
void MiniHttpServer::Stop() {}
bool MiniHttpServer::IsRunning() const { return false; }
unsigned short MiniHttpServer::GetPort() const { return 0; }

int wmain() {
    WSADATA wsaData = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::wcerr << L"Failed to initialize Winsock.\n";
        return 1;
    }

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        std::wcerr << L"Failed to initialize GDI+.\n";
        WSACleanup();
        return 1;
    }

    bool ok = true;
    bool available = false;

    ok = VerifyPaddleCloudUploadContract() && ok;

    {
        OcrEnginePPOcrV6Onnx engine(BuildPPOcrV6Config(LoadOcrSettings()));
        ok = CheckAvailability(L"PP-OCRv6 ONNX", engine, EnvEnabled(L"ZENCROP_REQUIRE_PPOCRV6"), available) && ok;
        ok = RunOptionalSmoke(
                 L"PP-OCRv6 ONNX",
                 L"ZENCROP_RUN_PPOCRV6_SMOKE",
                 engine,
                 available,
                 (DWORD)EnvInt(L"ZENCROP_PPOCRV6_SMOKE_TIMEOUT_MS", 120000),
                 available,
                 /*requirePPOcrV6Blocks=*/true) && ok;
    }

    {
        OcrEnginePaddleLocal engine;
        ok = CheckAvailability(L"Paddle Local VLM", engine, EnvEnabled(L"ZENCROP_REQUIRE_PADDLE_LOCAL"), available) && ok;
        ok = RunOptionalSmoke(
                 L"Paddle Local VLM",
                 L"ZENCROP_RUN_PADDLE_LOCAL_SMOKE",
                 engine,
                 available,
                 (DWORD)EnvInt(L"ZENCROP_PADDLE_LOCAL_SMOKE_TIMEOUT_MS", 180000),
                 false) && ok;
    }

    {
        OcrEnginePaddleDoc engine;
        ok = CheckAvailability(L"PaddleDoc Local", engine, EnvEnabled(L"ZENCROP_REQUIRE_PADDLE_DOC"), available) && ok;
        ok = RunOptionalSmoke(
                 L"PaddleDoc Local",
                 L"ZENCROP_RUN_PADDLE_DOC_SMOKE",
                 engine,
                 available,
                 (DWORD)EnvInt(L"ZENCROP_PADDLE_DOC_SMOKE_TIMEOUT_MS", 240000),
                 false) && ok;
    }

    {
        OcrEnginePaddleCloud engine;
        ok = CheckAvailability(L"Paddle Cloud", engine, EnvEnabled(L"ZENCROP_REQUIRE_PADDLE_CLOUD"), available) && ok;
        ok = RunOptionalSmoke(
                 L"Paddle Cloud",
                 L"ZENCROP_RUN_PADDLE_CLOUD_SMOKE",
                 engine,
                 available,
                 (DWORD)EnvInt(L"ZENCROP_PADDLE_CLOUD_SMOKE_TIMEOUT_MS", 240000),
                 false) && ok;
    }

    OcrEnginePaddleDoc::GlobalCleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    WSACleanup();

    if (!ok) return 1;
    std::wcout << L"Multi-engine OCR runtime contract completed.\n";
    return 0;
}
