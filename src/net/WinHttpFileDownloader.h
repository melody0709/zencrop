#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

enum class FileDownloadFailure {
    None = 0,
    Cancelled,
    InvalidUrl,
    Network,
    HttpStatus,
    RedirectRejected,
    RangeRejected,
    TruncatedResponse,
    FileSystem,
};

struct FileDownloadRequest {
    std::wstring url;
    std::wstring destinationPath;
    std::uint64_t expectedBytes = 0;
    int timeoutMs = 15000;
    bool allowResume = true;
};

struct FileDownloadResult {
    bool success = false;
    bool retryable = false;
    FileDownloadFailure failure = FileDownloadFailure::None;
    std::wstring error;
    std::wstring finalUrl;
    int httpStatus = 0;
    unsigned long win32Error = 0;
    std::uint64_t finalBytes = 0;
};

using FileDownloadProgressFn =
    std::function<void(std::uint64_t receivedBytes, std::uint64_t totalBytes)>;

FileDownloadResult WinHttpDownloadFile(
    const FileDownloadRequest& request,
    const std::atomic_bool& cancelRequested,
    const FileDownloadProgressFn& onProgress);

