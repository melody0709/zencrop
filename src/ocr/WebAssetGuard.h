#pragma once

#include <string>

namespace ZenCrop::WebAssets {

enum class GuardFailure {
    None,
    RootMissing,
    RootNotDirectory,
    RootReparsePoint,
    DirectoryOpen,
    FileOpen,
    ReparsePoint,
    PathEscaped,
    InvalidRelativePath,
    Enumeration,
    CaseCollision,
    MissingFile,
    UnknownFile,
    SizeMismatch,
    HashMismatch,
    HashFailure,
};

struct GuardResult {
    GuardFailure failure = GuardFailure::None;
    std::wstring relativePath;
    std::wstring message;

    bool ok() const { return failure == GuardFailure::None; }
};

// Checks one installed webview_assets directory against the manifest compiled
// into ZenCrop.exe. The function deliberately closes every handle before it
// returns; it detects a bad payload before WebView2 maps it, not a hostile
// same-user replacement that races after validation.
GuardResult VerifyWebAssetDirectory(const std::wstring& assetsRoot);
const wchar_t* GuardFailureName(GuardFailure failure);

} // namespace ZenCrop::WebAssets
