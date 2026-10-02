#pragma once

#include "TranslationProviderCatalog.h"

#include "core/HttpTransport.h"

#include <string>
#include <vector>

namespace translation {

// Everything a caller needs to ask a provider *which models do you offer*.
//
// The request is deliberately described here rather than inside an engine: model
// discovery is a settings-page action with its own failure semantics (a provider
// that cannot list is not broken), and the engines' contract is "translate" and
// "prove this connection works". Keeping the URL and the envelope in one pure
// module is also what makes both testable without a window.
struct ModelListFetchPlan {
    bool supported = false;
    std::wstring url;
    // Contains the credential. Callers must wipe it once the request is issued
    // (ProviderHeadersWipe()) -- it must never outlive the call.
    std::vector<std::wstring> headers;
    ModelListProtocol protocol = ModelListProtocol::None;
};

// What one listing returned, or the message to show when it failed. `error` always
// carries the provider's own wording when the provider explained itself. Each entry
// carries the id (the only part any request uses) plus the display name the vendor
// reported, falling back to the id when it reported none.
struct ModelListResult {
    std::vector<ModelNameEntry> models;
    std::wstring error;
    // Whether `models` is the vendor's whole list. False when the response was paged
    // (Google continues with `nextPageToken`) or when the cap cut it short. It is not
    // an error -- the ids that did arrive are usable -- but it is the difference
    // between "this model is gone" and "this model was not on this page", so the
    // advisory that draws that conclusion has to ask.
    bool complete = true;
};

// Whether a model listing can be requested for this profile *right now*, with a
// message naming what is missing.
//
// Deliberately narrower than the translation profile check, and the difference is the
// whole point: a listing GET uses the protocol, the base URL, the auth mode and the
// credential -- not the model, the temperature or the advanced options. Validating the
// whole profile made the first-run flow circular, because a preset with no seeds
// (OpenRouter, Ollama, a custom endpoint) starts with an empty model, so the one
// action that would tell the user which models exist was refused for having no model.
// Test connection and Apply keep the full check; a profile that cannot translate is
// still refused there.
bool ValidateListingTarget(
    const TranslationProviderProfile& profile, std::wstring* error);

// Whether this profile's API protocol publishes a model listing at all. False for
// machine translation, for a custom endpoint with no base URL yet, and for a
// protocol whose vendor has no listing path.
bool SupportsModelListing(const TranslationProviderProfile& profile);

// Builds the listing request for the profile and the credential to use. `error`
// is set (and `supported` stays false) when the profile cannot list models, so
// the caller can disable the action instead of issuing a doomed request.
ModelListFetchPlan PlanModelListFetch(
    const TranslationProviderProfile& profile,
    const std::wstring& key,
    std::wstring* error = nullptr);

// Wipes the credential-bearing headers of a plan that is no longer needed.
void ProviderHeadersWipe(std::vector<std::wstring>& headers);

// Parses a listing response into model ids: catalog order preserved, duplicates
// and unusable entries dropped, ids trimmed and length-capped at
// kMaxTranslationModelLength, total capped at 2000. Status codes, content type
// and the provider's own error text are handled here so every caller reports the
// same thing.
ModelListResult ParseModelListResponse(
    ModelListProtocol protocol, const HttpResponse& response);

// The display seeds this listing did not contain, i.e. the offered models that are
// apparently gone upstream. Advisory: it is what lets the page say so, and it
// changes nothing. An empty listing returns empty -- "the provider listed nothing"
// is not evidence that every seed is retired, and a truncated or proxied listing
// must never be able to delete a choice. `complete` is the listing's own claim to
// being the whole list: a paged answer returns empty here as well, because a seed
// that is merely on the next page is not a retired seed.
std::vector<std::wstring> UnlistedSeedModels(
    const TranslationProviderProfile& profile,
    const std::vector<ModelNameEntry>& fetched,
    bool complete);

// `Restore defaults` for the model catalog: clears the user's custom-model pool
// and puts the active model back on the preset's first catalog entry.
//
// Deliberately narrow. It does not touch the base URL, the API protocol, the auth
// mode, the reasoning tier, the temperature, the advanced options, the region or
// the credential -- those belong to the profile-level `Reset`, and the pool is
// the one thing that reset must never clear (a user's remembered model ids are
// not recoverable). Providers without a finite catalog keep their active model:
// clearing it would leave a profile that Apply rejects with "model is required".
bool RestoreModelCatalogDefaults(TranslationProviderProfile& profile);

} // namespace translation
