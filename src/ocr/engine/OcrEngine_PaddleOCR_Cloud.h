#pragma once
#include "OcrEngine.h"
#include "ocr/document/PaddleCloudImageOcrWorkflow.h"

class OcrEnginePaddleCloud : public IOcrEngine {
public:
    void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) override;
    bool IsAvailable() override;
    std::wstring Name() override { return L"paddle_cloud"; }
};
