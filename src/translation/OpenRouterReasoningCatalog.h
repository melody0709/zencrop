#pragma once

#include <string_view>

namespace translation {

// Whether an OpenRouter endpoint refuses to disable reasoning.
//
// `reasoning` is a *gateway* parameter on OpenRouter: the same request shape
// works for every model, but the gateway rejects `{"reasoning":{"enabled":false}}`
// on endpoints whose model metadata says `reasoning.mandatory = true`
// (HTTP 400 "Reasoning is mandatory for this endpoint and cannot be disabled.").
// Those endpoints therefore have to be driven with an effort tier instead, and
// `Off` must not be offered to the user.
//
// The id list is generated from `GET https://openrouter.ai/api/v1/models`
// (`data[].reasoning.mandatory`), which is the only endpoint that carries that
// field: `GET /api/v1/models/{id}/endpoints` reports `reasoning: null`.
// Regenerate with `scripts/generate_openrouter_reasoning_table.ps1`; do not
// hand-edit the array in the .cpp. Evidence and policy: see
// `.plan/feat/openrouter-reasoning-default-off-plan.md`.
//
// Unknown ids answer false, which keeps the default "reasoning off" for the
// majority of the catalog; a model that turns out to be mandatory anyway fails
// with the provider's own 400 message, which is surfaced to the user.
bool IsOpenRouterReasoningMandatory(std::wstring_view modelId);

} // namespace translation
