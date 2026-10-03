#pragma once

#include "LlmModelPolicy.h"
#include "core/Settings.h"

#include <set>
#include <string>
#include <vector>

namespace translation {

enum class TranslationProviderFamily {
    Llm,
    DirectMt,
};

enum class MachineTranslationProtocol {
    None,
    GoogleCloudV2,
    DeepLJson,
    AzureV3,
    MicrosoftCommunity,
    GoogleCommunity,
    DeepLX,
};

enum class ProviderMaturity {
    Supported,
    Experimental,
    SelfHosted,
};

// How a provider publishes "which models do you offer". The listing request is a
// GET whose shape differs per vendor family, so the protocol is catalog data
// rather than a per-engine branch.
enum class ModelListProtocol {
    None,
    // `{"data": [{"id": "..."}]}` -- OpenAI, DeepSeek, OpenRouter, Groq, ...
    OpenAiData,
    // `{"models": [{"name": "models/gemini-..."}]}` -- Generative Language API.
    // The `models/` prefix is stripped so ids match the preset catalog.
    GoogleModels,
    // `{"models": [{"name": "llama3:latest"}]}` -- Ollama tags, no prefix.
    OllamaTags,
};

// A model id plus the display name a provider's model listing reported for it.
// `id` is the only part any contract uses (request value, pool membership,
// de-duplication, catalog membership); `label` is display metadata and may be
// empty, in which case the id is what to show.
struct ModelNameEntry {
    std::wstring id;
    std::wstring label;
};

// One selectable "API protocol" for a provider preset. A preset keeps its native
// protocol as the first entry, so every existing profile keeps validating; the
// extra entries are the compatible wire formats the same vendor (or the same base
// URL) also accepts -- Gemini over the OpenAI-compatible surface, OpenAI over
// Chat Completions instead of Responses, and so on.
//
// `authModes` empty means "inherit the preset's auth modes"; a protocol that
// needs different credentials (Gemini's OpenAI-compatible surface wants a bearer
// token, not the native `x-goog-api-key`) overrides them here.
struct ProviderProtocolOption {
    TranslationAdapterKind adapter =
        TranslationAdapterKind::OpenAIChatCompletions;
    const wchar_t* label = L"OpenAI Chat Completions";
    // Default base URL for this protocol. Empty means the user must supply one
    // (`custom-openai-compatible`). Always normalized to end with '/'.
    std::wstring baseUrl;
    // Path appended to the base URL to list the vendor's models
    // (`models`, `api/tags`). Empty means this protocol cannot list models.
    std::wstring modelListPath;
    ModelListProtocol modelListProtocol = ModelListProtocol::None;
    // Empty means "inherit the preset's auth modes"; a protocol that needs
    // different credentials (Gemini's OpenAI-compatible surface wants a bearer
    // token, not the native `x-goog-api-key`) overrides them here.
    std::set<TranslationAuthMode> authModes;
};

struct ProviderCapabilities {
    std::set<TranslationReasoningMode> reasoningModes;
    std::set<TranslationAuthMode> authModes;
    TranslationReasoningMode defaultReasoning = TranslationReasoningMode::Off;
    ReasoningWireFormat reasoningWireFormat = ReasoningWireFormat::None;
    TranslationProviderFamily family = TranslationProviderFamily::Llm;
    MachineTranslationProtocol machineProtocol = MachineTranslationProtocol::None;
    ProviderMaturity maturity = ProviderMaturity::Supported;
    bool requiresApiKey = true;
    bool requiresModel = true;
    bool usesPromptProfile = true;
    bool allowsCustomBaseUrl = false;
    bool allowsCustomModel = false;
    bool supportsTemperature = false;
    // Applied when the profile carries no temperature of its own.
    std::optional<double> defaultTemperature;
    bool supportsBatch = true;
    bool acceptsRegion = false;
    LlmOutputMode outputMode = LlmOutputMode::PromptJson;
    InstructionChannel instructionChannel = InstructionChannel::System;
    TokenLimitKind tokenLimitKind = TokenLimitKind::MaxTokens;
    size_t maxSegmentsPerRequest = 0;
    int policyRevision = 1;
    bool loopbackHttpOnly = false;
    std::wstring endpoint;
    std::wstring dataHost;
};

struct TranslationProviderPreset {
    std::wstring kind;
    std::wstring displayName;
    std::wstring adapterName;
    TranslationAdapterKind adapterKind = TranslationAdapterKind::DeepSeekChat;
    std::wstring endpoint;
    std::wstring dataHost;
    // The *display seeds*: the model ids this preset offers before the user has
    // fetched anything. This list is one click away from a live request, so a
    // retired id here is a 404 waiting to happen -- it is deliberately kept to
    // the head of `modelPolicyIds` (see below) and is what the page's Model combo,
    // the result window's menu, the model picker's "Built-in" source, the
    // "catalog ids never enter the custom-model pool" rule and `Restore defaults`
    // are all about.
    //
    // It is NOT the policy judge: see `modelPolicyIds`.
    std::vector<std::wstring> models;
    // The *policy catalog*: every id this preset's model-level request policy
    // claims (output mode, temperature, token/segment limits, reasoning tiers and
    // dialect). Superset of `models`, never shown to the user.
    //
    // The split exists because one list cannot do both jobs: a retired id in
    // `models` breaks a user's click, while a retired id here is inert -- it can
    // never become a request by itself, and dropping it would silently downgrade
    // an existing profile onto the conservative policy (prompt-JSON output, no
    // temperature) the moment its model stopped being offered.
    std::vector<std::wstring> modelPolicyIds;
    ProviderCapabilities capabilities;
    // Selectable API protocols; the first entry is this preset's native one.
    std::vector<ProviderProtocolOption> protocols;
};

// The protocol option a profile is currently on, or nullptr when its stored
// adapter is not offered by its preset (a stale value written by an older build
// or by hand). Callers repair by falling back to `protocols.front()`.
const ProviderProtocolOption* FindProtocolOption(
    const TranslationProviderPreset& preset, TranslationAdapterKind adapter);

// Auth modes the profile's preset offers *for the protocol it is on*. This is the
// single definition: the page's default-auth repair and the capabilities the
// engines see must not disagree.
std::set<TranslationAuthMode> ProviderAuthModes(
    const TranslationProviderPreset& preset, TranslationAdapterKind adapter);

// The header that carries a credential for this auth mode
// (`X-Goog-Api-Key: <key>` or `Authorization: Bearer <key>`), or an empty string
// for a mode that sends none. One definition for every request this module issues:
// the two LLM engines and the model listing each used to build it themselves, which
// is the same shape of duplication that let the reasoning dialect drift away from
// the surface it was sent to.
std::wstring BuildProviderAuthHeader(
    TranslationAuthMode mode, const std::wstring& key);

// The adapter a profile is repaired to when its stored value is not one this
// preset offers (a transport written before the protocol table existed, or a
// hand-edited file): the preset's native protocol. One definition, shared by the
// persistence codec and the settings page, so a value one of them accepts cannot
// be silently rewritten by the other.
TranslationAdapterKind NormalizeProviderAdapter(
    const TranslationProviderPreset& preset, TranslationAdapterKind stored);

// The base URL a profile actually requests, normalized to end with '/'. For a
// profile on a machine-translation preset this is the complete request URL
// (those presets keep the "endpoint" semantics they have always had).
std::wstring ResolveProviderBaseUrl(
    const TranslationProviderProfile& profile,
    std::wstring* error = nullptr);

// Strips a known request path and normalizes the base to end with '/'. This is
// independent of the selected protocol; migration must verify that composing
// the candidate base reproduces the exact old URL before adopting it.
// `llmFamily == false` keeps machine-translation endpoints unchanged.
std::wstring BaseUrlFromRequestEndpoint(
    const std::wstring& endpoint, bool llmFamily);

const TranslationProviderPreset* FindTranslationProviderPreset(
    const std::wstring& presetKind);

// Built-in profiles are stable saved connections. Their preset is fixed by
// profile id; custom profiles may choose any preset.
const TranslationProviderPreset* FindBuiltInProviderPreset(
    const std::wstring& profileId);

std::vector<TranslationProviderPreset> ListTranslationProviderPresets();

// Add only offers presets that are not already represented by a built-in
// profile. A second connection for an existing built-in starts from Copy.
std::vector<TranslationProviderPreset> ListAddableTranslationProviderPresets(
    const TranslationSettings& settings);

// Whether the shipped built-in entry should be added to `settings` -- the seeding
// half of the manager's self-heal. Two reasons not to: the exact id is already
// there, or a user-created profile already covers the same preset. The Add menu
// refuses the mirror-image case (ListAddableTranslationProviderPresets hides the
// presets that have a built-in), so seeding one for a preset the user already
// configured is exactly the duplicate those two rules exist to prevent: two rows
// for one provider, one of them system-owned and therefore undeletable. The
// built-in comes back by itself when nothing covers the preset any more.
bool ShouldAddBuiltInProviderProfile(
    const TranslationSettings& settings,
    const TranslationProviderProfile& builtIn);

// Whether another profile in `settings` uses the same preset as `id`. Two rows for
// one provider are legitimate -- Copy gives a vendor's second account its own
// connection -- so the page labels the pair instead of hiding one of them.
bool SharesProviderPreset(
    const TranslationSettings& settings, const std::wstring& id);

// Whether the manager may delete the profile `id`. Built-in connections are
// system-owned and refused, with one exception: a built-in whose preset another
// profile already covers is redundant (a user profile may predate the shipped
// entry, and the Add menu cannot prevent what it does not yet offer), so refusing
// would leave a row the user can neither use nor remove.
bool CanDeleteProviderProfile(
    const TranslationSettings& settings, const std::wstring& id);

// Creates the persisted user profile used by the Add Provider flow. Catalog
// presets describe availability; newly added profiles are intentionally
// disabled until the user configures and explicitly enables them.
TranslationProviderProfile CreateTranslationProviderProfile(
    const TranslationProviderPreset& preset,
    const std::wstring& profileId);

const TranslationProviderProfile* FindActiveTranslationProvider(
    const TranslationSettings& settings);

TranslationProviderProfile* FindActiveTranslationProvider(
    TranslationSettings& settings);

ProviderCapabilities GetCapabilities(
    const TranslationProviderProfile& profile);

// Protocol constraints may strengthen a conservative model policy without
// changing that policy or its prompt. Shared by wire construction and diagnostics.
LlmOutputMode EffectiveWireOutputMode(const TranslationProviderProfile &profile,
                                      const ProviderCapabilities &capabilities);

// "Listed" means the profile's preset *offers* this exact model id: it is a
// display seed. The flag `customModel` records "the active id is not one of the
// offered seeds" (it is what the page renders and what lets an unlisted id pass
// validation), and it no longer influences the request shape at all.
bool IsListedProviderModel(
    const TranslationProviderProfile& profile,
    const std::wstring& model);

// Whether the preset's model-level policy claims this id, i.e. whether the id
// takes the model-level request policy instead of the conservative one. This is
// the single judge for that decision (see `GetCapabilities`), and it is a
// superset of the offered seeds: an id that stopped being offered -- because the
// seed list was slimmed, or because the user fetched a model we never listed --
// keeps the request policy its vendor's models need.
bool IsModelPolicyKnown(
    const TranslationProviderPreset& preset,
    const std::wstring& model);

// The characters a model id may not contain. An id is interpolated into a request
// URL -- and, on Gemini's native surface, into a path segment of its own
// (`models/<id>:generateContent`) -- so `?`/`#` would turn the rest of the URL into
// a query or a fragment and whitespace would rely on the transport to escape it.
// '/' ':' '.' are legitimate, which is why this is a character rule rather than a
// "safe string" filter.
//
// This is the only definition of that rule: the pool writers, the listing parser,
// the model picker and the settings validator all ask it, so an id the page accepts
// is an id the loader can read back.
std::wstring SanitizeModelIdentifier(const std::wstring& model);

// Whether a pool of `kept` entries plus `added` more fits the cap (0 = unlimited).
// Split out because the model picker cannot ask the pool writer first: the active
// model it is about to set is added by the *caller* after the pool write, and a
// full pool answers that addition by evicting its oldest entry. Counting only the
// checkboxes would promise "nothing of yours is evicted silently" and then evict.
bool PoolFitsWithinCapacity(size_t kept, size_t added, size_t capacity);

// Whether making `active` the active model will *add it to the pool*. The page applies
// the active model after the pool write, and the writer's answer to a full pool is to
// drop its oldest entry -- so the capacity decision depends on this, not only on the
// checkboxes. It is a predicate here rather than dialog arithmetic so that decision is
// testable: an id that is a catalog model, or already in the kept pool, or on a profile
// that refuses custom models, joins nothing.
bool ActiveModelJoinsPool(
    const std::wstring& active,
    bool allowsCustomModel,
    const std::vector<std::wstring>& catalogIds,
    const std::vector<std::wstring>& poolIds);

// Whether `model` may be stored as it is: non-empty, and unchanged by
// `SanitizeModelIdentifier`. Callers that take a value from a user or from a vendor
// listing use this to refuse it. The persistence reader deliberately does not: a
// stored id an older build was allowed to write (it only checked the length) is
// repaired with the sanitizer, because refusing it there failed the *whole*
// translation section rather than one id.
bool IsStorableModelIdentifier(const std::wstring& model);

// Applies a user-chosen model id: stores it, derives `customModel` from the
// catalog (a listed id is never custom) and remembers an unlisted id in the
// FIFO pool. Returns whether the id is a catalog entry. Callers clamp the
// reasoning mode afterwards.
bool ApplyTranslationModelChoice(
    TranslationProviderProfile& profile,
    const std::wstring& model);

// Remembers `profile.model` in the custom-model pool when it is an unlisted id
// and always drops catalog entries from the pool, so a pool written by an older
// build cannot keep a model the catalog now publishes. FIFO: the oldest id
// makes room once the pool is full.
void RememberCustomModel(TranslationProviderProfile& profile);

// Replaces the user's model pool with `models`, applying the pool contract in one
// place: catalog-published ids are dropped, empty and over-long ids are dropped,
// duplicates collapse in order, and the FIFO cap holds. The settings page's model
// picker is the only other writer besides RememberCustomModel(), and it writes
// through here so the two cannot drift apart. Returns whether the pool changed.
//
// Labels of ids that did not survive the contract are pruned as well, so the
// display side table can never outlive the pool it describes.
bool SetCustomModelPool(
    TranslationProviderProfile& profile, const std::vector<std::wstring>& models);

// Merges the display names a model listing reported into the display side table.
// Display-only: it changes no membership, no order and no request. The table is
// pruned to the pool first (an id that is not remembered cannot keep a name) and
// then the given entries are stored, so a caller may pass only the names it just
// learned without wiping the others. Ids and labels are trimmed and length-capped,
// and a label that is empty or equal to its id is not stored at all. Returns
// whether anything changed. Call it after the pool write it belongs to, so a name
// can never precede the id it describes.
bool RememberCustomModelLabels(
    TranslationProviderProfile& profile,
    const std::vector<ModelNameEntry>& labels);

// Drops labels whose id is no longer in the pool. Every pool writer ends with this
// call, and so does the persistence reader, so neither a FIFO eviction nor a
// hand-edited file can leave a name behind for an id the pool does not hold.
void PruneCustomModelLabels(TranslationProviderProfile& profile);

bool IsSupportedProviderProfile(
    const TranslationProviderProfile& profile,
    std::wstring* error = nullptr);

// The URL this profile's requests are actually sent to. For an LLM profile it is
// composed from the base URL and the selected API protocol
// (`<base>chat/completions`, `<base>responses`, `<base>models/<model>:generateContent`,
// `<base>api/chat`), so this function is idempotent over its own output: a known request
// suffix is stripped before it is appended again.
//
// A *pre-v8* profile that still holds a complete request URL keeps working through the
// reader's migration rather than through that idempotence (see
// InterpretStoredEndpoint and the codec): a stored value becomes a base only when
// re-composing it here reproduces the address byte for byte; otherwise the profile is
// marked complete and this function returns the stored address unchanged.
// EndpointIsCompleteRequestUrl is the one predicate the resolver, the settings page and
// the listing module all ask about that state.
// Machine-translation presets keep the complete-URL semantics.
std::wstring ResolveProviderEndpoint(
    const TranslationProviderProfile& profile,
    std::wstring* error = nullptr);

// What a stored custom endpoint means, and what the field should hold from then on.
struct StoredEndpointMeaning {
    // True when the value is a complete request URL that base + protocol path cannot
    // reproduce, so it is sent exactly as stored (`https://gateway.example/invoke`).
    // Nothing in such a value says which part is the protocol path, which is why it is
    // never guessed apart.
    bool verbatim = false;
    std::wstring base;
};

// Proposes a base for a recognized request path; query-bearing and opaque URLs
// keep their complete-address semantics. The codec accepts a proposed base only
// if the profile's protocol reconstructs the exact stored URL; otherwise it keeps
// the complete address and the page refuses a protocol switch until it is edited.
StoredEndpointMeaning InterpretStoredEndpoint(const std::wstring& value);

// Whether this profile's stored endpoint is a *complete request address* rather than a
// base, i.e. composition cannot produce it: the reader marked it, or it carries a query
// string (a version-pinned address whose path belongs to one protocol). One definition,
// because the resolver and the page must agree on the same question -- the resolver
// keeps such an address as it stands, so switching the API protocol while it holds
// would send the new protocol's body to the old protocol's address. `Profile` with an
// empty or non-custom override is never one.
bool EndpointIsCompleteRequestUrl(const TranslationProviderProfile& profile);

// The model id as a *request* needs it. Gemini's native surface names the model in
// its path and deliberately accepts either spelling, while every OpenAI-shaped body
// carries the bare id -- Google's compatible surface does not know the `models/`
// prefix. One definition, so the two surfaces of one preset cannot disagree about
// what "the model" is: a profile stored as `models/gemini-3.8-flash` (legal on the
// native surface, and what the native listings used to report) used to send that
// prefix verbatim in the compatible body.
std::wstring RequestModelId(const TranslationProviderProfile& profile);

bool IsReasoningModeSupported(
    const ProviderCapabilities& capabilities,
    TranslationReasoningMode mode);

bool RequiresSingleSegmentRequests(
    const TranslationProviderProfile& profile);

} // namespace translation
