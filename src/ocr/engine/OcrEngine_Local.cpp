#include "OcrEngine_Local.h"
#include "Settings.h"
#include <winrt/base.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Globalization.h>
#include <set>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>

using namespace winrt;
using namespace Windows::Media::Ocr;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;
using namespace Windows::Globalization;

struct OcrLineResult {
    std::wstring text;
    RECT bounds;
};

static RECT GetLineBounds(const OcrLine& line) {
    RECT r = { INT_MAX, INT_MAX, 0, 0 };
    for (auto&& word : line.Words()) {
        auto rect = word.BoundingRect();
        int left = (int)rect.X;
        int top = (int)rect.Y;
        int right = left + (int)rect.Width;
        int bottom = top + (int)rect.Height;
        if (left < r.left) r.left = left;
        if (top < r.top) r.top = top;
        if (right > r.right) r.right = right;
        if (bottom > r.bottom) r.bottom = bottom;
    }
    return r;
}

static bool BoundsOverlap(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

static int BoundsOverlapArea(const RECT& a, const RECT& b) {
    int overlapLeft = (std::max)(a.left, b.left);
    int overlapTop = (std::max)(a.top, b.top);
    int overlapRight = (std::min)(a.right, b.right);
    int overlapBottom = (std::min)(a.bottom, b.bottom);
    if (overlapRight <= overlapLeft || overlapBottom <= overlapTop) return 0;
    return (overlapRight - overlapLeft) * (overlapBottom - overlapTop);
}

static int BoundsArea(const RECT& r) {
    return (r.right - r.left) * (r.bottom - r.top);
}

static std::vector<OcrLineResult> RecognizeWithEngine(OcrEngine& engine, SoftwareBitmap& bitmap) {
    std::vector<OcrLineResult> results;
    auto ocrResult = engine.RecognizeAsync(bitmap).get();
    for (auto&& line : ocrResult.Lines()) {
        OcrLineResult lr;
        lr.text = std::wstring(line.Text());
        lr.bounds = GetLineBounds(line);
        if (!lr.text.empty() && lr.bounds.right > lr.bounds.left && lr.bounds.bottom > lr.bounds.top) {
            results.push_back(std::move(lr));
        }
    }
    return results;
}

static bool CollectAvailableRecognizerLanguages(std::vector<Language>& languages) {
    auto availableLanguages = OcrEngine::AvailableRecognizerLanguages();
    languages.reserve(availableLanguages.Size());
    for (auto&& language : availableLanguages) {
        languages.push_back(language);
    }
    return true;
}

// Some Windows installations expose a usable profile OCR engine while the
// AvailableRecognizerLanguages static factory crashes with an invalid ABI
// pointer instead of returning an HRESULT. Keep the normal multilingual path
// when the API is healthy, but contain that OS-component access violation so
// auto mode can fall back to TryCreateFromUserProfileLanguages().
static bool CollectAvailableRecognizerLanguagesGuarded(
    std::vector<Language>* languages)
{
    bool succeeded = false;
    __try {
        if (languages) {
            succeeded = CollectAvailableRecognizerLanguages(*languages);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        succeeded = false;
    }
    return succeeded;
}

static std::vector<OcrLineResult> MergeMultiLangResults(std::vector<std::vector<OcrLineResult>>& allResults) {
    if (allResults.empty()) return {};
    if (allResults.size() == 1) return std::move(allResults[0]);

    std::vector<OcrLineResult> merged = std::move(allResults[0]);

    for (size_t i = 1; i < allResults.size(); i++) {
        for (auto& candidate : allResults[i]) {
            if (candidate.text.empty()) continue;

            bool dominated = false;
            for (auto& existing : merged) {
                if (!BoundsOverlap(existing.bounds, candidate.bounds)) continue;

                int overlapArea = BoundsOverlapArea(existing.bounds, candidate.bounds);
                int candidateArea = BoundsArea(candidate.bounds);
                int existingArea = BoundsArea(existing.bounds);

                if (candidateArea > 0 && overlapArea * 2 >= candidateArea) {
                    if (existing.text == candidate.text) {
                        dominated = true;
                        break;
                    }
                    bool existingIsCJK = false, candidateIsCJK = false;
                    for (wchar_t c : existing.text) {
                        if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3040 && c <= 0x30FF) || (c >= 0xAC00 && c <= 0xD7AF)) {
                            existingIsCJK = true; break;
                        }
                    }
                    for (wchar_t c : candidate.text) {
                        if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3040 && c <= 0x30FF) || (c >= 0xAC00 && c <= 0xD7AF)) {
                            candidateIsCJK = true; break;
                        }
                    }

                    if (candidateIsCJK && !existingIsCJK) {
                        existing = candidate;
                        dominated = true;
                        break;
                    }
                    if (existingIsCJK && !candidateIsCJK) {
                        dominated = true;
                        break;
                    }

                    if (candidate.text.length() > existing.text.length()) {
                        existing = candidate;
                        dominated = true;
                        break;
                    }
                }
            }
            if (!dominated) {
                merged.push_back(std::move(candidate));
            }
        }
    }

    std::sort(merged.begin(), merged.end(), [](const OcrLineResult& a, const OcrLineResult& b) {
        if (a.bounds.top != b.bounds.top) return a.bounds.top < b.bounds.top;
        return a.bounds.left < b.bounds.left;
    });

    return merged;
}

static DWORD WINAPI LocalOcrWorkerThread(LPVOID param) {
    auto* pParams = (OcrParams*)param;
    OcrOutput result;
    ULONGLONG startTick = GetTickCount64();

    OutputDebugStringA("[OCR] Local worker thread started\n");

    try {
        winrt::init_apartment();
        // IsAvailable() probes OCR on a short-lived apartment. Do not reuse a
        // cached WinRT activation factory from that apartment on this worker.
        winrt::clear_factory_cache();

        int width = 0, height = 0;
        std::vector<uint8_t> pixels;
        GetBitmapBits32(pParams->hBitmap, width, height, pixels);

        OcrSettings ocrSettings = LoadOcrSettings();
        std::wstring targetLang = ocrSettings.language;

        auto stream = InMemoryRandomAccessStream();
        auto encoder = BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream).get();
        encoder.SetPixelData(BitmapPixelFormat::Rgba8, BitmapAlphaMode::Ignore,
            width, height, 96.0, 96.0, pixels);
        encoder.FlushAsync().get();
        stream.Seek(0);
        auto decoder = BitmapDecoder::CreateAsync(stream).get();
        auto bitmap = decoder.GetSoftwareBitmapAsync().get();

        std::wstring text;

        if (targetLang == L"auto") {
            std::vector<Language> availableLangs;
            if (!CollectAvailableRecognizerLanguagesGuarded(&availableLangs)) {
                availableLangs.clear();
                OutputDebugStringA(
                    "[OCR] AvailableRecognizerLanguages failed; using the user-profile OCR engine.\n");
            }
            std::vector<OcrEngine> engines;

            for (const auto& lang : availableLangs) {
                auto engine = OcrEngine::TryCreateFromLanguage(lang);
                if (engine) {
                    engines.push_back(engine);
                }
            }

            if (engines.empty()) {
                auto fallback = OcrEngine::TryCreateFromUserProfileLanguages();
                if (!fallback) {
                    result.error = L"Failed to create OCR engine. Install OCR language packs.";
                    DeleteObject(pParams->hBitmap);
                    InvokeOcrCallbackSafely(pParams->callback, result);
                    delete pParams;
                    return 1;
                }
                auto lines = RecognizeWithEngine(fallback, bitmap);
                for (auto& lr : lines) {
                    if (!text.empty()) text += L"\r\n";
                    text += lr.text;
                }
            } else {
                std::vector<std::vector<OcrLineResult>> allResults;
                allResults.reserve(engines.size());
                for (auto& engine : engines) {
                    allResults.push_back(RecognizeWithEngine(engine, bitmap));
                }
                auto merged = MergeMultiLangResults(allResults);
                for (auto& lr : merged) {
                    if (!text.empty()) text += L"\r\n";
                    text += lr.text;
                }
            }
        } else {
            auto engine = OcrEngine::TryCreateFromUserProfileLanguages();

            std::vector<Language> languages;
            if (!CollectAvailableRecognizerLanguagesGuarded(&languages)) {
                languages.clear();
                OutputDebugStringA(
                    "[OCR] AvailableRecognizerLanguages failed; using the user-profile OCR engine.\n");
            }
            for (const auto& lang : languages) {
                if (std::wstring(lang.LanguageTag()) == targetLang) {
                    auto specificEngine = OcrEngine::TryCreateFromLanguage(lang);
                    if (specificEngine) {
                        engine = specificEngine;
                        break;
                    }
                }
            }

            if (!engine) {
                result.error = L"Failed to create OCR engine. Install OCR language packs.";
                DeleteObject(pParams->hBitmap);
                InvokeOcrCallbackSafely(pParams->callback, result);
                delete pParams;
                return 1;
            }

            auto ocrResult = engine.RecognizeAsync(bitmap).get();
            auto lines = ocrResult.Lines();
            for (auto&& line : lines) {
                if (!text.empty()) text += L"\r\n";
                text += std::wstring(line.Text());
            }
        }

        result.success = true;
        result.text = text;
    }
    catch (winrt::hresult_error const& ex) {
        result.error = std::wstring(ex.message());
    }
    catch (...) {
        result.error = L"Unknown error during local OCR";
    }

    result.elapsedMs = (DWORD)(GetTickCount64() - startTick);
    DeleteObject(pParams->hBitmap);
    InvokeOcrCallbackSafely(pParams->callback, result);
    delete pParams;
    return 0;
}

struct OcrAvailabilityProbe {
    HANDLE doneEvent = nullptr;
    std::atomic<bool> available{false};

    ~OcrAvailabilityProbe() {
        if (doneEvent) CloseHandle(doneEvent);
    }
};

struct OcrAvailabilityProbeStart {
    std::shared_ptr<OcrAvailabilityProbe> probe;
};

struct OcrAvailabilityCache {
    std::mutex mutex;
    std::shared_ptr<OcrAvailabilityProbe> pending;
    bool hasResult = false;
    bool result = false;
    ULONGLONG resultTick = 0;
};

OcrAvailabilityCache& AvailabilityCache() {
    static OcrAvailabilityCache cache;
    return cache;
}

static bool ProbeLocalOcrAvailability() {
    try {
        winrt::init_apartment();
        auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
        return engine != nullptr;
    } catch (...) {
        return false;
    }
}

static bool ProbeLocalOcrAvailabilityGuarded() {
    __try {
        return ProbeLocalOcrAvailability();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static DWORD WINAPI LocalOcrAvailabilityThread(LPVOID param) {
    std::unique_ptr<OcrAvailabilityProbeStart> start(
        static_cast<OcrAvailabilityProbeStart*>(param));
    if (!start || !start->probe) return 1;
    const auto probe = std::move(start->probe);

    probe->available.store(ProbeLocalOcrAvailabilityGuarded(), std::memory_order_release);
    // The probe apartment is about to disappear. Leaving its activation
    // factory cached can hand the recognition worker an invalid ABI pointer.
    winrt::clear_factory_cache();

    SetEvent(probe->doneEvent);
    return 0;
}

void OcrEngineLocal::Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) {
    auto* params = new OcrParams{ hBitmap, callback };
    HANDLE h = CreateThread(nullptr, 0, LocalOcrWorkerThread, params, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        OcrOutput result;
        result.error = L"Failed to start the local OCR worker thread.";
        DeleteObject(params->hBitmap);
        InvokeOcrCallbackSafely(params->callback, result);
        delete params;
    }
}

bool OcrEngineLocal::IsAvailable() {
    auto& cache = AvailabilityCache();
    std::shared_ptr<OcrAvailabilityProbe> probe;
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        constexpr ULONGLONG kAvailabilityCacheMs = 30000;
        if (cache.hasResult &&
            GetTickCount64() - cache.resultTick < kAvailabilityCacheMs) {
            return cache.result;
        }
        if (!cache.pending) {
            probe = std::make_shared<OcrAvailabilityProbe>();
            probe->doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!probe->doneEvent) {
                cache.hasResult = true;
                cache.result = false;
                cache.resultTick = GetTickCount64();
                return false;
            }
            auto* start = new OcrAvailabilityProbeStart{probe};
            HANDLE thread = CreateThread(nullptr, 0, LocalOcrAvailabilityThread, start, 0, nullptr);
            if (!thread) {
                delete start;
                cache.hasResult = true;
                cache.result = false;
                cache.resultTick = GetTickCount64();
                return false;
            }
            CloseHandle(thread);
            cache.pending = probe;
        } else {
            probe = cache.pending;
        }
    }

    // Never block the UI while WinRT OCR activation is probing. Treat a
    // pending probe as provisionally available; the recognition worker still
    // reports a concrete language-pack/activation failure if the probe later
    // resolves negative.
    if (WaitForSingleObject(probe->doneEvent, 0) != WAIT_OBJECT_0) return true;

    const bool available = probe->available.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.pending == probe) {
            cache.pending.reset();
            cache.hasResult = true;
            cache.result = available;
            cache.resultTick = GetTickCount64();
        }
    }
    return available;
}
