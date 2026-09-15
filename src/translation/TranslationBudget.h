#pragma once

#include "LlmModelPolicy.h"

namespace translation {

// Timeout budget for one translation batch.
//
// Terminology is fixed on purpose, because a single "budget" word was read
// two different ways during review:
//   - attemptTimeoutMs is the *single-attempt* timeout. It drives WinHTTP's
//     dwReceiveTimeout, which for a non-streaming LLM response is effectively
//     "the whole generation must finish within this many milliseconds"
//     (headers are not sent until generation completes).
//   - requestDeadlineMs is the *total budget duration* for the batch. It is a
//     duration, never an absolute timestamp; the coordinator converts it to a
//     remaining-ms value with its own start tick.
struct TranslationBudget {
    int attemptTimeoutMs = 60000;   // single attempt; drives dwReceiveTimeout
    int requestDeadlineMs = 135000; // total duration across all attempts
    int maxAttempts = 2;            // including the first
};

// Reasoning Off and reasoning-on differ by roughly a factor of two both in the
// measured worst case and in the reasoning token counts, so they need separate
// ceilings. Measured 2026-09-15 on the largest batch the coordinator can emit
// (12000 characters / 53 segments): Off 12.7-28.4 s (n=6), High 37.3-51.4 s
// (n=2). The constants below give roughly 2.1x / 2.35x headroom.
//
// Both callers must resolve the budget here so the coordinator's retry
// threshold and the engine's request options can never drift apart.
inline TranslationBudget ResolveTranslationBudget(
    const TranslationProviderProfile& profile) {
    if (profile.reasoningMode == TranslationReasoningMode::Off) {
        return TranslationBudget{60000, 135000, 2};
    }
    return TranslationBudget{120000, 260000, 2};
}

// TestConnection is a diagnostics action: a tiny GET /models plus a 64-token
// probe translation. It must never inherit the translation budget, otherwise a
// dead connection at the High tier would spin for minutes inside the settings
// dialog. The probe translation issued by that flow uses this budget too.
inline constexpr TranslationBudget kConnectionProbeBudget{15000, 20000, 1};

} // namespace translation
