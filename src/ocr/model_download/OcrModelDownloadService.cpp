#include "OcrModelDownloadService.h"

#include "OcrModelDownloadCatalog.h"
#include "OcrModelInstaller.h"
#include "net/WinHttpFileDownloader.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

OcrModelDownloadError MapDownloadError(
    const OcrModelArtifactSpec& artifact,
    const FileDownloadResult& result)
{
    OcrModelDownloadError error;
    error.artifactId = artifact.id;
    error.stage = L"downloading";
    error.technicalDetail = result.error;
    error.httpStatus = result.httpStatus;
    error.win32Error = result.win32Error;
    error.retryable = result.retryable;
    switch (result.failure) {
    case FileDownloadFailure::Cancelled:
        error.category = OcrModelDownloadErrorCategory::Cancelled;
        error.userMessage = L"The model download was cancelled.";
        break;
    case FileDownloadFailure::HttpStatus:
        error.category = OcrModelDownloadErrorCategory::HttpStatus;
        error.userMessage = L"The model server rejected the download request.";
        break;
    case FileDownloadFailure::RedirectRejected:
        error.category = OcrModelDownloadErrorCategory::RedirectRejected;
        error.userMessage = L"The model server returned an unsafe redirect.";
        break;
    case FileDownloadFailure::RangeRejected:
        error.category = OcrModelDownloadErrorCategory::RangeRejected;
        error.userMessage = L"The model server returned an invalid resume response.";
        break;
    case FileDownloadFailure::TruncatedResponse:
        error.category = OcrModelDownloadErrorCategory::TruncatedResponse;
        error.userMessage = L"The model download ended before the expected file was received.";
        break;
    case FileDownloadFailure::FileSystem:
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.userMessage = L"The model download could not be written to disk.";
        break;
    case FileDownloadFailure::InvalidUrl:
        error.category = OcrModelDownloadErrorCategory::InvalidCatalog;
        error.userMessage = L"The model catalog contains an invalid URL.";
        break;
    case FileDownloadFailure::Network:
    case FileDownloadFailure::None:
    default:
        error.category = OcrModelDownloadErrorCategory::Network;
        error.userMessage = L"The model download failed because of a network error.";
        break;
    }
    return error;
}

} // namespace

OcrModelDownloadService::OcrModelDownloadService(std::wstring runtimeTemplateDir)
    : runtimeTemplateDir_(runtimeTemplateDir.empty()
          ? OcrModelRuntimeTemplateDirectory()
          : std::move(runtimeTemplateDir))
{
}

OcrModelDownloadService::~OcrModelDownloadService()
{
    Cancel();
    Wait();
}

bool OcrModelDownloadService::Start(
    OcrModelBundleId bundle,
    const std::wstring& modelRoot,
    OcrModelMirrorPreference mirrorPref)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (OcrModelDownloadStateIsActive(snapshot_.state)) return false;
    }
    if (worker_.joinable()) worker_.join();

    cancelRequested_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = {};
        snapshot_.state = OcrModelDownloadState::Preparing;
        snapshot_.bundle = bundle;
        snapshot_.statusText = L"Preparing download...";
    }
    worker_ = std::thread(&OcrModelDownloadService::Run, this, bundle, modelRoot, mirrorPref);
    return true;
}

void OcrModelDownloadService::Cancel()
{
    cancelRequested_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (OcrModelDownloadStateIsActive(snapshot_.state)) {
        snapshot_.state = OcrModelDownloadState::Cancelling;
        snapshot_.statusText = L"Cancelling...";
    }
}

void OcrModelDownloadService::Wait()
{
    if (worker_.joinable()) worker_.join();
}

OcrModelDownloadSnapshot OcrModelDownloadService::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool OcrModelDownloadService::BundleInstalled(
    OcrModelBundleId bundle,
    const std::wstring& modelRoot) const
{
    const OcrModelBundleSpec* spec = OcrModelDownloadFindBundle(bundle);
    if (!spec) return false;
    return OcrModelInstaller(runtimeTemplateDir_).BundleInstalled(modelRoot, *spec);
}

void OcrModelDownloadService::SetFailed(const OcrModelDownloadError& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = OcrModelDownloadState::Failed;
    snapshot_.statusText = error.userMessage;
    snapshot_.error = error;
}

void OcrModelDownloadService::SetCancelled()
{
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = OcrModelDownloadState::Cancelled;
    snapshot_.statusText = L"Download paused. Partial data was kept for resume.";
    snapshot_.error = {};
    snapshot_.error.category = OcrModelDownloadErrorCategory::Cancelled;
}

bool OcrModelDownloadService::WaitForRetry(int milliseconds) const
{
    int remaining = milliseconds;
    while (remaining > 0) {
        if (cancelRequested_.load()) return false;
        const int interval = (std::min)(remaining, 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        remaining -= interval;
    }
    return !cancelRequested_.load();
}

void OcrModelDownloadService::Run(
    OcrModelBundleId bundleId,
    std::wstring modelRoot,
    OcrModelMirrorPreference mirrorPref)
{
    try {
        RunImpl(bundleId, std::move(modelRoot), mirrorPref);
    } catch (...) {
        OcrModelDownloadError error;
        error.category = OcrModelDownloadErrorCategory::InternalError;
        error.stage = L"runtime";
        error.userMessage = L"An unexpected internal error occurred during model download.";
        SetFailed(error);
    }
}

void OcrModelDownloadService::RunImpl(
    OcrModelBundleId bundleId,
    std::wstring modelRoot,
    OcrModelMirrorPreference mirrorPref)
{
    std::wstring catalogError;
    if (!OcrModelDownloadValidateCatalog(catalogError)) {
        OcrModelDownloadError error;
        error.category = OcrModelDownloadErrorCategory::InvalidCatalog;
        error.stage = L"preparing";
        error.userMessage = L"The built-in model catalog is invalid.";
        error.technicalDetail = catalogError;
        SetFailed(error);
        return;
    }
    const OcrModelBundleSpec* bundle = OcrModelDownloadFindBundle(bundleId);
    if (!bundle || modelRoot.empty()) {
        OcrModelDownloadError error;
        error.category = OcrModelDownloadErrorCategory::InvalidCatalog;
        error.stage = L"preparing";
        error.userMessage = L"The selected model bundle or target directory is invalid.";
        SetFailed(error);
        return;
    }

    OcrModelInstaller installer(runtimeTemplateDir_);
    const std::uint64_t totalBytes = OcrModelDownloadBundleBytes(*bundle);
    OcrModelDownloadError error;
    if (!installer.PrepareTarget(modelRoot, totalBytes, error)) {
        SetFailed(error);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.totalBytes = totalBytes;
        snapshot_.statusText = L"Checking installed files...";
    }

    std::uint64_t completedBytes = 0;
    for (const auto& artifact : bundle->artifacts) {
        if (cancelRequested_.load()) {
            SetCancelled();
            return;
        }
        if (installer.ArtifactInstalled(modelRoot, artifact, true)) {
            completedBytes += artifact.expectedBytes;
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.completedBytes = completedBytes;
            snapshot_.statusText = L"Verified installed file: " + artifact.id;
            continue;
        }

        std::wstring stagingPath;
        if (!installer.PrepareStaging(modelRoot, artifact, stagingPath, error)) {
            SetFailed(error);
            return;
        }

        FileDownloadResult downloadResult;
        bool downloaded = false;
        // Reorder URLs based on mirror preference. Catalog stores HuggingFace
        // first; ModelScopeFirst moves ModelScope URLs to the front.
        std::vector<std::wstring> orderedUrls = artifact.urls;
        if (mirrorPref == OcrModelMirrorPreference::ModelScopeFirst &&
            orderedUrls.size() > 1) {
            std::stable_sort(orderedUrls.begin(), orderedUrls.end(),
                [](const std::wstring& a, const std::wstring& b) {
                    const bool aMs = a.find(L"modelscope.cn") != std::wstring::npos;
                    const bool bMs = b.find(L"modelscope.cn") != std::wstring::npos;
                    return aMs && !bMs;
                });
        }
        for (const std::wstring& url : orderedUrls) {
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (cancelRequested_.load()) {
                    SetCancelled();
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot_.state = OcrModelDownloadState::Downloading;
                    snapshot_.currentArtifact = artifact.id;
                    snapshot_.currentArtifactBytes = 0;
                    snapshot_.currentArtifactTotal = artifact.expectedBytes;
                    snapshot_.statusText = L"Downloading " + artifact.id + L"...";
                    snapshot_.error = {};
                }

                ULONGLONG lastTick = GetTickCount64();
                std::uint64_t lastBytes = 0;
                FileDownloadRequest request;
                request.url = url;
                request.destinationPath = stagingPath;
                request.expectedBytes = artifact.expectedBytes;
                request.allowResume = true;
                downloadResult = WinHttpDownloadFile(
                    request,
                    cancelRequested_,
                    [this, completedBytes, &lastTick, &lastBytes](
                        std::uint64_t received, std::uint64_t total) {
                        const ULONGLONG now = GetTickCount64();
                        std::uint64_t speed = 0;
                        if (now > lastTick && received >= lastBytes) {
                            speed = (received - lastBytes) * 1000ULL / (now - lastTick);
                        }
                        if (now - lastTick >= 500) {
                            lastTick = now;
                            lastBytes = received;
                        }
                        std::lock_guard<std::mutex> lock(mutex_);
                        snapshot_.currentArtifactBytes = received;
                        snapshot_.currentArtifactTotal = total;
                        snapshot_.completedBytes = completedBytes + received;
                        if (speed > 0) snapshot_.bytesPerSecond = speed;
                    });
                if (downloadResult.success) {
                    downloaded = true;
                    break;
                }
                if (downloadResult.failure == FileDownloadFailure::Cancelled) {
                    SetCancelled();
                    return;
                }
                if (!downloadResult.retryable || attempt == 2) break;
                if (!WaitForRetry(250 * (1 << attempt))) {
                    SetCancelled();
                    return;
                }
            }
            if (downloaded) break;
        }
        if (!downloaded) {
            SetFailed(MapDownloadError(artifact, downloadResult));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.state = OcrModelDownloadState::Verifying;
            snapshot_.statusText = L"Verifying " + artifact.id + L"...";
        }
        if (cancelRequested_.load()) {
            SetCancelled();
            return;
        }
        if (!installer.VerifyStaging(artifact, stagingPath, error)) {
            DeleteFileW(stagingPath.c_str());
            DeleteFileW((stagingPath + L".json").c_str());
            SetFailed(error);
            return;
        }
        if (cancelRequested_.load()) {
            SetCancelled();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.state = OcrModelDownloadState::Installing;
            snapshot_.statusText = L"Installing " + artifact.id + L"...";
        }
        if (!installer.InstallArtifact(modelRoot, artifact, stagingPath, error)) {
            SetFailed(error);
            return;
        }
        completedBytes += artifact.expectedBytes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.completedBytes = completedBytes;
            snapshot_.currentArtifactBytes = artifact.expectedBytes;
        }
    }

    if (cancelRequested_.load()) {
        SetCancelled();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.state = OcrModelDownloadState::Installing;
        snapshot_.statusText = L"Finalizing model bundle...";
    }
    if (!installer.FinalizeBundle(modelRoot, *bundle, error)) {
        SetFailed(error);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = OcrModelDownloadState::Completed;
    snapshot_.statusText = L"Model bundle installed and verified.";
    snapshot_.completedBytes = totalBytes;
    snapshot_.currentArtifactBytes = snapshot_.currentArtifactTotal;
    snapshot_.installResult = OcrModelDownloadBuildInstallResult(bundleId, modelRoot);
    snapshot_.error = {};
}
