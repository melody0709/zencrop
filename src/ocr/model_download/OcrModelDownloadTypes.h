#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class OcrModelBundleId {
    PpOcrV6Small = 0,
    PpOcrV6Medium,
    PaddleOcrVl16,
    DocLayout,
};

// Mirror source preference for download URL ordering.
// Each artifact may carry both HuggingFace and ModelScope URLs; this enum
// controls which source is tried first. The other source remains as fallback.
enum class OcrModelMirrorPreference {
    Auto = 0,         // HuggingFace first, ModelScope fallback (default)
    ModelScopeFirst,  // ModelScope first, HuggingFace fallback (China-friendly)
};

enum class OcrModelArtifactKind {
    File = 0,
    LlamaRuntimeZip,
};

struct OcrModelArtifactSpec {
    std::wstring id;
    std::vector<std::wstring> urls;
    std::wstring relativeInstallPath;
    std::uint64_t expectedBytes = 0;
    std::wstring sha256;
    OcrModelArtifactKind kind = OcrModelArtifactKind::File;
};

struct OcrModelBundleSpec {
    OcrModelBundleId id = OcrModelBundleId::PpOcrV6Small;
    std::wstring stableId;
    std::wstring displayName;
    std::wstring description;
    std::wstring upstreamLicense;
    std::vector<OcrModelArtifactSpec> artifacts;
};

enum class OcrModelDownloadState {
    Idle = 0,
    Preparing,
    Downloading,
    Verifying,
    Installing,
    Cancelling,
    Cancelled,
    Failed,
    Completed,
};

enum class OcrModelDownloadErrorCategory {
    None = 0,
    InvalidCatalog,
    TargetUnavailable,
    DiskSpace,
    Network,
    HttpStatus,
    RedirectRejected,
    RangeRejected,
    TruncatedResponse,
    Integrity,
    Archive,
    Publish,
    ModelInUse,
    Cancelled,
    InternalError,
};

struct OcrModelDownloadError {
    OcrModelDownloadErrorCategory category = OcrModelDownloadErrorCategory::None;
    std::wstring artifactId;
    std::wstring stage;
    std::wstring userMessage;
    std::wstring technicalDetail;
    std::wstring sourceHost;
    int httpStatus = 0;
    unsigned long win32Error = 0;
    bool retryable = false;
};

struct OcrModelInstallResult {
    OcrModelBundleId bundle = OcrModelBundleId::PpOcrV6Small;
    std::wstring modelRoot;
    std::wstring paddleLocalModelDir;
    std::wstring ppocrv6ModelDir;
    std::wstring ppocrv6Variant;
    std::wstring docLayoutModelPath;
};

struct OcrModelDownloadSnapshot {
    OcrModelDownloadState state = OcrModelDownloadState::Idle;
    OcrModelBundleId bundle = OcrModelBundleId::PpOcrV6Small;
    std::wstring currentArtifact;
    std::wstring statusText;
    std::uint64_t currentArtifactBytes = 0;
    std::uint64_t currentArtifactTotal = 0;
    std::uint64_t completedBytes = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t bytesPerSecond = 0;
    OcrModelDownloadError error;
    OcrModelInstallResult installResult;
};

inline bool OcrModelDownloadStateIsActive(OcrModelDownloadState state)
{
    return state == OcrModelDownloadState::Preparing ||
        state == OcrModelDownloadState::Downloading ||
        state == OcrModelDownloadState::Verifying ||
        state == OcrModelDownloadState::Installing ||
        state == OcrModelDownloadState::Cancelling;
}

