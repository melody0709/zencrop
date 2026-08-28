#pragma once
#include "OcrEngine.h"

class OcrEnginePaddleLocal : public IOcrEngine {
public:
    explicit OcrEnginePaddleLocal(PaddleVlServerService* server = nullptr);
    ~OcrEnginePaddleLocal() override;

    void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) override;
    bool IsAvailable() override;
    std::wstring Name() override { return L"paddle_local"; }

private:
    static DWORD WINAPI WorkerThread(LPVOID param);
    OcrOutput DoRecognize(HBITMAP hBitmap);
    PaddleVlServerService* m_server = nullptr;
};
