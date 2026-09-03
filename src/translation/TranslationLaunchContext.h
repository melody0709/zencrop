#pragma once

#include <windows.h>

namespace translation {

enum class TranslationSourceMode {
    OcrImage,
    SelectedText,
};

struct TranslationLaunchContext {
    TranslationSourceMode mode = TranslationSourceMode::OcrImage;
    RECT anchorRect = {};
};

enum class TranslationStartError {
    None,
    ProviderUnavailable,
    CredentialMissing,
    InvalidLanguages,
    UnsupportedSettings,
    EmptyText,
    ShuttingDown,
    WindowCreationFailed,
};

struct TranslationStartResult {
    bool started = false;
    TranslationStartError error = TranslationStartError::None;
};

} // namespace translation
