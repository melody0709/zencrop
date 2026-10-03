#include "TranslationModelListing.h"

#include "TranslationTextUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwchar>

namespace translation {
namespace {

using json = nlohmann::json;

// A listing is a catalog, not a translation response: the ceiling only exists so
// a misbehaving gateway cannot make the settings page allocate without bound. The
// largest real listing measured so far is OpenRouter's (458 entries); 2000 leaves
// room for the vendors that publish thousands of fine-tunes.
constexpr size_t kMaxListedModels = 2000;

std::wstring TrimCopy(const std::wstring& value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

// Normalizes a raw id from a listing: trim, drop the Google `models/` prefix so
// ids match the preset catalog, drop anything empty, over the stored-model length
// ceiling, or carrying a character a request URL cannot hold (a listing cannot be
// allowed to write a value Apply would then reject -- length was only half of that
// contract).
std::wstring NormalizeListedModel(
    const std::string& raw, ModelListProtocol protocol) {
    std::wstring value = TrimCopy(Utf8ToWide(raw));
    if (protocol == ModelListProtocol::GoogleModels &&
        value.rfind(L"models/", 0) == 0) {
        value.erase(0, 7);
        value = TrimCopy(value);
    }
    if (value.empty() || value.size() > kMaxTranslationModelLength ||
        !IsStorableModelIdentifier(value)) {
        return {};
    }
    return value;
}

// Normalizes a vendor-reported display name. Display-only, so anything unusable
// simply falls back to the id: an unnamed model must still be listed.
std::wstring NormalizeListedLabel(const std::string& raw, const std::wstring& id) {
    std::wstring value = TrimCopy(Utf8ToWide(raw));
    if (value.empty() || value.size() > kMaxTranslationModelLength) return id;
    return value;
}

void AppendUnique(std::vector<ModelNameEntry>& models,
    std::vector<std::wstring>& seen, ModelNameEntry entry) {
    if (models.size() >= kMaxListedModels) return;
    if (std::find(seen.begin(), seen.end(), entry.id) != seen.end()) return;
    seen.push_back(entry.id);
    models.push_back(std::move(entry));
}

// Both the Google and the Ollama envelopes wrap their rows in `models`, and both
// name the id `name`; only Google adds a human name (`displayName`). The `models/`
// prefix is NormalizeListedModel's business.
ModelListResult ParseNamedModels(
    const json& outer, ModelListProtocol protocol, const wchar_t* missingError) {
    ModelListResult result;
    if (!outer.is_object() || !outer.contains("models") ||
        !outer["models"].is_array()) {
        result.error = missingError;
        return result;
    }
    std::vector<std::wstring> seen;
    for (const auto& entry : outer["models"]) {
        if (!entry.is_object()) continue;
        if (!entry.contains("name") || !entry["name"].is_string()) continue;
        // Google's Model resource declares what each model can do, and the list
        // contains far more than generators: an embedding-only entry used to be listed
        // here, shown as an ordinary choice, and then sent to `generateContent`, which
        // answers 404. Only filtered when the field is actually present -- its absence
        // is not evidence that a model cannot generate, and dropping those would empty
        // the list of any gateway that leaves it out.
        if (protocol == ModelListProtocol::GoogleModels &&
            entry.contains("supportedGenerationMethods") &&
            entry["supportedGenerationMethods"].is_array()) {
            const auto& methods = entry["supportedGenerationMethods"];
            const bool canGenerate = std::any_of(methods.begin(), methods.end(),
                [](const json& method) {
                    return method.is_string() &&
                        method.get<std::string>() == "generateContent";
                });
            if (!canGenerate) continue;
        }
        std::wstring value = NormalizeListedModel(entry["name"], protocol);
        if (value.empty()) continue;
        ModelNameEntry listed;
        listed.id = value;
        listed.label = entry.contains("displayName") && entry["displayName"].is_string()
            ? NormalizeListedLabel(entry["displayName"], value)
            : value;
        AppendUnique(result.models, seen, std::move(listed));
    }
    // Google's list is paged and continues in further pages, so a token means this is
    // not the whole list either. (The 2000-entry cap is answered once for every
    // protocol at the end of ParseModelListResponse.)
    if (protocol == ModelListProtocol::GoogleModels) {
        const auto token = outer.find("nextPageToken");
        if (token != outer.end() && token->is_string() &&
            !token->get<std::string>().empty()) {
            result.complete = false;
        }
    }
    return result;
}

} // namespace

void ProviderHeadersWipe(std::vector<std::wstring>& headers) {
    for (auto& header : headers) {
        if (!header.empty()) {
            SecureZeroMemory(header.data(), header.size() * sizeof(wchar_t));
        }
    }
    headers.clear();
}

bool ValidateListingTarget(
    const TranslationProviderProfile& profile, std::wstring* error) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) {
        if (error) *error = L"Unknown translation provider preset.";
        return false;
    }
    const auto* option = FindProtocolOption(*preset, profile.adapterKind);
    if (!option || option->modelListProtocol == ModelListProtocol::None ||
        option->modelListPath.empty()) {
        if (error) {
            *error = L"This provider does not publish a model listing.";
        }
        return false;
    }
    // A complete request address cannot be turned into a listing address. Appending the
    // listing path to one produces something that is not a listing URL at all: for
    // `.../v1/chat/completions?api-version=3.0` the path lands *inside* the query, and
    // for `.../invoke` it produces a guessed `.../invoke/models`. Keeping the
    // translation request byte-compatible says nothing about being able to derive a
    // catalogue URL, so the fetch is refused with the reason instead. Answered before
    // the auth and base checks so the message names the real obstacle.
    if (EndpointIsCompleteRequestUrl(profile)) {
        if (error) {
            *error =
                L"This provider's endpoint is a complete request URL, so no model "
                L"listing address can be derived from it. Set a base URL first.";
        }
        return false;
    }
    // The auth mode decides both the header and whether a key is needed at all, so an
    // unsupported one is refused here rather than turned into a request the vendor
    // would reject. Asked through the *protocol*'s set, which is the whole point:
    // Gemini's vendor default is `{ApiKey}` (the Google header) while its OpenAI-
    // compatible surface declares `{BearerApiKey}`, and reading the preset default here
    // refused a fetch on a surface whose own protocol says Bearer is correct.
    //
    // One named set, compared with itself: writing `ProviderAuthModes(...).find(...) ==
    // ProviderAuthModes(...).end()` compares an iterator into one temporary container
    // with an iterator into another. That is undefined behaviour, and in a release
    // build it answered "supported" for every mode -- the auth check was dead code
    // until a test caught it.
    const std::set<TranslationAuthMode> allowedAuthModes =
        ProviderAuthModes(*preset, profile.adapterKind);
    if (allowedAuthModes.find(profile.authMode) == allowedAuthModes.end()) {
        if (error) {
            *error = L"Translation provider authentication mode is unsupported.";
        }
        return false;
    }
    // Asked through the same resolver the request uses, so "can we ask?" and "where?"
    // cannot disagree. A base that fails validation here fails there too.
    std::wstring baseError;
    if (ResolveProviderBaseUrl(profile, &baseError).empty()) {
        if (error) *error = baseError;
        return false;
    }
    return true;
}

bool SupportsModelListing(const TranslationProviderProfile& profile) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) return false;
    // Same rule as ValidateListingTarget, so a greyed-out button and a refusal message
    // cannot disagree: a complete request address has no derivable listing URL.
    if (EndpointIsCompleteRequestUrl(profile)) return false;
    if (preset->capabilities.family != TranslationProviderFamily::Llm) {
        return false;
    }
    const auto* option = FindProtocolOption(*preset, profile.adapterKind);
    if (!option ||
        option->modelListProtocol == ModelListProtocol::None ||
        option->modelListPath.empty()) {
        return false;
    }
    // Asked through the same resolver the request uses, so this answer and
    // PlanModelListFetch can never disagree: a protocol with no base URL of its own
    // falls back to the preset's endpoint, and a custom endpoint has no listing URL
    // until the user supplies one.
    std::wstring baseError;
    return !ResolveProviderBaseUrl(profile, &baseError).empty();
}

ModelListFetchPlan PlanModelListFetch(
    const TranslationProviderProfile& profile,
    const std::wstring& key,
    std::wstring* error) {
    ModelListFetchPlan plan;
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) {
        if (error) *error = L"Unknown translation provider preset.";
        return plan;
    }
    const auto* option = FindProtocolOption(*preset, profile.adapterKind);
    if (!option || option->modelListProtocol == ModelListProtocol::None ||
        option->modelListPath.empty()) {
        if (error) {
            // Diagnostics stay English here, like every other provider message this
            // module's callers surface; the settings page localizes only the
            // messages it authors itself.
            *error = L"This provider does not publish a model listing.";
        }
        return plan;
    }
    std::wstring baseError;
    // Same rule as the two functions above, and for the same reason: `base + path` on a
    // complete request address puts the listing path inside the query string (or invents
    // one), so there is nothing honest to request. The caller shows this message.
    if (EndpointIsCompleteRequestUrl(profile)) {
        if (error) {
            *error =
                L"This provider's endpoint is a complete request URL, so no model "
                L"listing address can be derived from it. Set a base URL first.";
        }
        return plan;
    }
    const std::wstring base = ResolveProviderBaseUrl(profile, &baseError);
    if (base.empty()) {
        if (error) *error = baseError;
        return plan;
    }
    plan.supported = true;
    plan.protocol = option->modelListProtocol;
    plan.url = base + option->modelListPath;
    if (plan.protocol == ModelListProtocol::GoogleModels) {
        // Google's list is paged -- 50 models by default, then `nextPageToken` -- and
        // the models this dialog offers are the first thing a user would call
        // incomplete. Asking for the documented maximum keeps it in one request; a
        // token still coming back is handled by marking the result incomplete, which
        // silences the "no longer offered" advice rather than acting on half a list.
        plan.url += plan.url.find(L'?') == std::wstring::npos ? L"?pageSize=1000"
                                                             : L"&pageSize=1000";
    }
    plan.headers.push_back(L"Accept: application/json");
    if (TranslationAuthUsesCredential(profile.authMode)) {
        if (key.empty()) {
            plan.supported = false;
            if (error) *error = L"Configure this provider's API key first.";
            return plan;
        }
        // One definition with the engines (see BuildProviderAuthHeader).
        plan.headers.insert(plan.headers.begin(),
            BuildProviderAuthHeader(profile.authMode, key));
    }
    return plan;
}

ModelListResult ParseModelListResponse(
    ModelListProtocol protocol, const HttpResponse& response) {
    ModelListResult result;
    if (!response.error.empty()) {
        result.error = response.error;
        return result;
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        std::wstring message = L"Translation provider request failed (" +
            std::to_wstring(response.statusCode) + L").";
        const std::wstring detail = ProviderErrorDetail(response);
        if (!detail.empty()) message += L" " + detail;
        result.error = std::move(message);
        return result;
    }
    if (!IsJsonContentType(response.contentType)) {
        result.error = L"The provider's model listing is not JSON.";
        return result;
    }
    try {
        const json outer = json::parse(response.body);
        switch (protocol) {
        case ModelListProtocol::OpenAiData: {
            if (!outer.is_object() || !outer.contains("data") ||
                !outer["data"].is_array()) {
                result.error = L"The provider's model listing has an unexpected shape.";
                return result;
            }
            std::vector<std::wstring> seen;
            for (const auto& entry : outer["data"]) {
                if (!entry.is_object()) continue;
                if (!entry.contains("id") || !entry["id"].is_string()) continue;
                std::wstring value = NormalizeListedModel(
                    entry["id"], ModelListProtocol::OpenAiData);
                if (value.empty()) continue;
                ModelNameEntry listed;
                listed.id = value;
                // `name` is the OpenAI extra field gateways use for a human name;
                // it is optional everywhere, so an absent one means "show the id".
                listed.label = entry.contains("name") && entry["name"].is_string()
                    ? NormalizeListedLabel(entry["name"], value)
                    : value;
                AppendUnique(result.models, seen, std::move(listed));
            }
            break;
        }
        case ModelListProtocol::GoogleModels:
            result = ParseNamedModels(outer, protocol,
                L"The provider's model listing has an unexpected shape.");
            break;
        case ModelListProtocol::OllamaTags:
            result = ParseNamedModels(outer, protocol,
                L"The provider's model listing has an unexpected shape.");
            break;
        case ModelListProtocol::None:
        default:
            result.error = L"The provider does not publish a model listing.";
            return result;
        }
        // One completeness answer for every protocol, so a listing that ran into the
        // cap cannot be read as "the vendor no longer offers the rest" -- the OpenAI
        // branch used to return with the default `true` and mis-report a seed that was
        // merely the 2001st entry. A branch that failed leaves `error` set and an empty
        // list, which the caller's error path already handles.
        if (result.error.empty() && result.models.size() >= kMaxListedModels) {
            result.complete = false;
        }
        return result;
    } catch (const json::exception&) {
        result.error = L"The provider's model listing could not be parsed.";
        return result;
    }
}

std::vector<std::wstring> UnlistedSeedModels(
    const TranslationProviderProfile& profile,
    const std::vector<ModelNameEntry>& fetched,
    bool complete) {
    std::vector<std::wstring> missing;
    if (!complete) return missing;
    if (fetched.empty()) return missing;
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) preset = FindBuiltInProviderPreset(profile.id);
    if (!preset) return missing;
    for (const auto& seed : preset->models) {
        if (seed.empty()) continue;
        const bool listed = std::any_of(fetched.begin(), fetched.end(),
            [&](const ModelNameEntry& entry) { return entry.id == seed; });
        if (listed) continue;
        if (std::find(missing.begin(), missing.end(), seed) == missing.end()) {
            missing.push_back(seed);
        }
    }
    return missing;
}

bool RestoreModelCatalogDefaults(TranslationProviderProfile& profile) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) return false;
    const auto capabilities = GetCapabilities(profile);
    profile.customModels.clear();
    // This is a pool writer, so it owes the display side table the same prune every
    // other writer does -- otherwise the names of the models just cleared survive in
    // the session (the codec would drop them on the next save, but the page would be
    // reading a table that describes nothing).
    PruneCustomModelLabels(profile);
    if (!capabilities.requiresModel) {
        // Machine translation carries no model at all.
        profile.model.clear();
        profile.customModel = false;
        return true;
    }
    if (!preset->models.empty()) {
        profile.model = preset->models.front();
        profile.customModel = false;
        return true;
    }
    // No finite catalog (OpenRouter, Ollama, a custom endpoint): the user's model
    // id is the only thing that can make this profile valid, so it is kept and the
    // mark that lets an unlisted id through stays set.
    profile.customModel = true;
    return true;
}

} // namespace translation
