#pragma once
#include "OcrEngine.h"
#include "LayoutEngine.h"
#include "PaddleVlLlamaClient.h"

struct OcrSettings;

class OcrEnginePaddleDoc : public IOcrEngine {
public:
    explicit OcrEnginePaddleDoc(PaddleVlServerService* server = nullptr);
    ~OcrEnginePaddleDoc() override;

    void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) override;
    bool IsAvailable() override;
    std::wstring Name() override { return L"paddle_doc"; }

    static void CleanupLayoutEngine();
    static void GlobalCleanup();

private:
    static bool FindLayoutModel(std::wstring& layoutPath);

    static DWORD WINAPI WorkerThread(LPVOID param);
    OcrOutput DoRecognize(HBITMAP hBitmap);
    static PaddleVlLlamaResult RecognizeRegionDetailedStatic(
        PaddleVlServerService* server, HBITMAP hRegion, const std::wstring& prompt, int port, int maxTokens,
        PaddleVlImageEncoding encoding = PaddleVlImageEncoding::Png);
    OcrOutput DoRecognizeOfficial(HBITMAP hBitmap);
    std::wstring AssembleMarkdown(const std::vector<LayoutRegion>& regions,
                                   const std::vector<std::wstring>& texts,
                                   HBITMAP hOriginalBitmap,
                                   std::vector<OcrEmbeddedAssetSpec>* embeddedAssets);
    static std::wstring SplitParagraphs(const std::wstring& text);

    static LayoutEngine s_layoutEngine;
    static std::wstring s_layoutModelPath;
    static std::wstring s_layoutModelFamilySetting;
    static CRITICAL_SECTION s_layoutCs;
    PaddleVlServerService* m_server = nullptr;
};

#ifdef ZENCROP_PADDLE_DOC_CROP_ENCODING_BENCHMARK
// Test-build-only hook. It is deliberately omitted from production builds so
// the crop-encoding A/B can hold layout and grouping fixed without creating a
// hidden user-facing setting.
void SetPaddleDocBenchmarkCropEncoding(PaddleVlImageEncoding encoding);
#endif
