#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace translation {

// One summary record per translation that needed a retry or reached a terminal
// failure. Counts, codes and timings only: never source text or translations.
struct TranslationDiagnosticRecord {
    uint64_t generation = 0;
    // Batch request id of the last attempt. The provider's own trace id (shown
    // in the error text) cannot be correlated locally, so this is what ties a
    // user report to one request.
    std::wstring batchId;
    size_t segmentCount = 0;
    size_t untranslatableCount = 0;
    size_t batchCount = 0;
    int transportRetries = 0;
    int contentRetries = 0;
    bool structured = false;
    unsigned long long elapsedMs = 0;
    // "ready", "degraded" or "failed".
    std::wstring outcome;
    std::wstring errorCode;
    std::wstring error;
};

// Appends one line to %LOCALAPPDATA%\ZenCrop\translation_diagnostics.log (or the
// explicit data directory override). This is the only persistent record of
// retry frequency, which is otherwise unmeasurable; failures are silent so a
// diagnostics problem can never affect a translation. The file is rotated to
// "<name>.1" once it exceeds 256 KB, so the footprint is bounded.
void AppendTranslationDiagnostic(const TranslationDiagnosticRecord& record);

} // namespace translation
