#pragma once
#include "OcrEngine.h"

class OcrEngineLocal : public IOcrEngine {
public:
    void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) override;
    bool IsAvailable() override;
    std::wstring Name() override { return L"local"; }
};