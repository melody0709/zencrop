#pragma once
#include "OcrEngine.h"
#include "PPOcrV6OrtSession.h"
#include <string>
class OcrEnginePPOcrV6Onnx : public IOcrEngine {
public:
    explicit OcrEnginePPOcrV6Onnx(PPOcrV6Config config);
    void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) override;
    bool IsAvailable() override;
    std::wstring Name() override { return L"ppocrv6_onnx"; }

private:
    static DWORD WINAPI WorkerThread(LPVOID param);
    OcrOutput DoRecognize(HBITMAP hBitmap);

    PPOcrV6Config m_config;
};
