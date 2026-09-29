#include "OpenRouterReasoningCatalog.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace translation {
namespace {

// Generated from `GET https://openrouter.ai/api/v1/models`:
// every id whose `reasoning.mandatory` was true. Sorted, ASCII, lower case.
//
// >>> GENERATED: mandatory-reasoning model ids (scripts/generate_openrouter_reasoning_table.ps1) >>>
constexpr const wchar_t* kMandatoryReasoningModelIds[] = {
    L"~anthropic/claude-fable-latest",
    L"~anthropic/claude-opus-latest",
    L"~google/gemini-flash-latest",
    L"~google/gemini-pro-latest",
    L"~openai/gpt-astra-latest",
    L"~x-ai/grok-latest",
    L"~z-ai/glm-flash-latest",
    L"~z-ai/glm-latest",
    L"aion-labs/aion-2.0",
    L"aion-labs/aion-3.0",
    L"aion-labs/aion-3.0-mini",
    L"aion-labs/aion-3.5",
    L"aion-labs/aion-3.5-mini",
    L"anthropic/claude-fable-5",
    L"anthropic/claude-fable-5:batch",
    L"anthropic/claude-fable-5.1",
    L"anthropic/claude-fable-5.1:batch",
    L"anthropic/claude-opus-5.5",
    L"anthropic/claude-opus-5.5:batch",
    L"arcee-ai/trinity-large-thinking",
    L"deepseek/deepseek-r1",
    L"deepseek/deepseek-r1-0528",
    L"google/gemini-2.5-pro",
    L"google/gemini-2.5-pro-preview",
    L"google/gemini-2.5-pro:batch",
    L"google/gemini-3-pro-image",
    L"google/gemini-3-pro-image-preview",
    L"google/gemini-3.1-pro-preview",
    L"google/gemini-3.1-pro-preview-customtools",
    L"google/gemini-3.1-pro-preview:batch",
    L"google/gemini-3.5-flash",
    L"google/gemini-3.5-flash-lite",
    L"google/gemini-3.5-flash-lite:batch",
    L"google/gemini-3.5-flash:batch",
    L"google/gemini-3.6-flash",
    L"google/gemini-3.6-flash:batch",
    L"google/gemini-3.7-flash",
    L"google/gemini-3.7-flash:batch",
    L"google/gemini-3.8-flash",
    L"google/gemini-3.8-flash:batch",
    L"liquid/lfm-2.5-2.6b:free",
    L"meta/muse-glimmer-30b",
    L"meta/muse-spark-1.1",
    L"meta/muse-spark-1.2",
    L"meta/muse-spark-1.2-contributor",
    L"meta/muse-spark-1.3",
    L"meta/muse-spark-1.3-contributor",
    L"minimax/minimax-m2",
    L"minimax/minimax-m2.1",
    L"minimax/minimax-m2.5",
    L"minimax/minimax-m2.7",
    L"moonshotai/kimi-k2-thinking",
    L"moonshotai/kimi-k2.7-code",
    L"openai/gpt-5",
    L"openai/gpt-5-image",
    L"openai/gpt-5-image-mini",
    L"openai/gpt-5-mini",
    L"openai/gpt-5-mini:batch",
    L"openai/gpt-5-nano",
    L"openai/gpt-5-nano:batch",
    L"openai/gpt-5-pro",
    L"openai/gpt-5-pro:batch",
    L"openai/gpt-5:batch",
    L"openai/gpt-5.1-codex",
    L"openai/gpt-5.1-codex-max",
    L"openai/gpt-5.2-codex",
    L"openai/gpt-5.2-pro",
    L"openai/gpt-5.2-pro:batch",
    L"openai/gpt-5.4-pro",
    L"openai/gpt-5.4-pro:batch",
    L"openai/gpt-5.5-pro",
    L"openai/gpt-5.5-pro:batch",
    L"openai/gpt-6-astra",
    L"openai/gpt-6-astra-pro",
    L"openai/gpt-6-astra-pro:batch",
    L"openai/gpt-6-astra:batch",
    L"openai/gpt-oss-120b",
    L"openai/gpt-oss-120b:batch",
    L"openai/gpt-oss-20b",
    L"openai/gpt-oss-20b:batch",
    L"openai/gpt-oss-safeguard-20b",
    L"openai/o3-mini-high",
    L"openai/o4-mini-high",
    L"perplexity/sonar-pro-search",
    L"qwen/qwen3-235b-a22b-thinking-2507",
    L"qwen/qwen3-30b-a3b-thinking-2507",
    L"qwen/qwen3-next-80b-a3b-thinking",
    L"qwen/qwen3-vl-235b-a22b-thinking",
    L"qwen/qwen3-vl-30b-a3b-thinking",
    L"qwen/qwen3-vl-8b-thinking",
    L"qwen/qwen3.8-2.4t-a95b",
    L"qwen/qwen3.8-max-0902",
    L"qwen/qwen3.8-max-prime",
    L"rekaai/reka-flash-3",
    L"sakana/fugu-max",
    L"sakana/fugu-ultra",
    L"sakana/fugu-ultra-v2",
    L"stealth/space-bunny-alpha",
    L"stepfun/step-3.5-flash",
    L"stepfun/step-3.7-flash",
    L"x-ai/grok-4.20-multi-agent",
    L"x-ai/grok-4.5",
    L"x-ai/grok-4.6",
    L"x-ai/grok-4.7",
    L"x-ai/grok-build-0.1",
    L"z-ai/glm-5.3",
    L"z-ai/glm-5.3-flash",
    L"z-ai/glm-5.3-flash:batch",
    L"z-ai/glm-5.3-flashx",
    L"z-ai/glm-5.3-prime",
    L"z-ai/glm-5.3:batch",
};

// <<< GENERATED <<<

std::wstring NormalizeModelId(std::wstring_view modelId) {
    size_t begin = 0;
    size_t end = modelId.size();
    while (begin < end && (modelId[begin] == L' ' || modelId[begin] == L'\t' ||
                           modelId[begin] == L'\r' || modelId[begin] == L'\n')) {
        ++begin;
    }
    while (end > begin && (modelId[end - 1] == L' ' || modelId[end - 1] == L'\t' ||
                           modelId[end - 1] == L'\r' || modelId[end - 1] == L'\n')) {
        --end;
    }
    std::wstring normalized;
    normalized.reserve(end - begin);
    for (size_t index = begin; index < end; ++index) {
        wchar_t character = modelId[index];
        // Model ids are ASCII; fold case so a hand-typed "OpenAI/GPT-5.5-Pro"
        // still matches the catalog entry.
        if (character >= L'A' && character <= L'Z') {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
        normalized.push_back(character);
    }
    return normalized;
}

bool ContainsMandatoryId(const std::wstring& modelId) {
    // A linear scan over ~111 entries. Deliberately not a binary search: that
    // would make the lookup depend on the generated array being sorted by the
    // *same* collation the compiler uses (ordinal), while the generator sorts
    // with PowerShell's culture-aware comparison -- a mismatch would silently
    // turn every mandatory endpoint back into an `enabled:false` 400. This runs
    // once per policy resolution, not per segment.
    return std::any_of(
        std::begin(kMandatoryReasoningModelIds),
        std::end(kMandatoryReasoningModelIds),
        [&modelId](const wchar_t* entry) {
            return std::wstring_view(entry) == std::wstring_view(modelId);
        });
}

} // namespace

bool IsOpenRouterReasoningMandatory(std::wstring_view modelId) {
    const std::wstring normalized = NormalizeModelId(modelId);
    if (normalized.empty()) return false;
    if (ContainsMandatoryId(normalized)) return true;
    // Variant fallback: a `:free` / `:batch` / `:<variant>` slug that is not in the
    // table inherits the base model's answer. Erring toward "mandatory" is the safe
    // direction -- the request then carries the lowest effort tier instead of an
    // `enabled:false` that the endpoint may reject outright.
    const size_t colon = normalized.rfind(L':');
    if (colon == std::wstring::npos || colon == 0) return false;
    return ContainsMandatoryId(normalized.substr(0, colon));
}

} // namespace translation
