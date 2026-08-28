#pragma once

#include "OcrModelDownloadTypes.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class OcrModelDownloadService final {
public:
    explicit OcrModelDownloadService(
        std::wstring runtimeTemplateDir = std::wstring());
    ~OcrModelDownloadService();

    OcrModelDownloadService(const OcrModelDownloadService&) = delete;
    OcrModelDownloadService& operator=(const OcrModelDownloadService&) = delete;

    bool Start(
        OcrModelBundleId bundle,
        const std::wstring& modelRoot,
        OcrModelMirrorPreference mirrorPref = OcrModelMirrorPreference::Auto);
    void Cancel();
    void Wait();

    OcrModelDownloadSnapshot Snapshot() const;
    bool BundleInstalled(
        OcrModelBundleId bundle,
        const std::wstring& modelRoot) const;

private:
    void Run(
        OcrModelBundleId bundle,
        std::wstring modelRoot,
        OcrModelMirrorPreference mirrorPref);
    void RunImpl(
        OcrModelBundleId bundle,
        std::wstring modelRoot,
        OcrModelMirrorPreference mirrorPref);
    void SetFailed(const OcrModelDownloadError& error);
    void SetCancelled();
    bool WaitForRetry(int milliseconds) const;

    std::wstring runtimeTemplateDir_;
    mutable std::mutex mutex_;
    OcrModelDownloadSnapshot snapshot_;
    std::atomic_bool cancelRequested_{ false };
    std::thread worker_;
};

