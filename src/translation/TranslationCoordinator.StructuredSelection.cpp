#include "TranslationCoordinator.h"

#include "TranslationProviderCatalog.h"
#include "core/Strings.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace translation {
namespace {

constexpr size_t kMaximumStructuredBlockRequestChars = 10000;

std::wstring StageText(const wchar_t* chinese, const wchar_t* english) {
    return S::IsChinese() ? chinese : english;
}

} // namespace

bool TranslationCoordinator::RequestPreviewSelection(
    HWND topLevelWindow,
    uint64_t requestGeneration,
    std::function<void(selection::SelectionContent)> callback) {
    if (shuttingDown_ || !callback || !resultWindow_ ||
        !resultWindow_->IsValid() ||
        resultWindow_->WindowHandle() != topLevelWindow) {
        return false;
    }
    return resultWindow_->RequestPreviewSelection(
        requestGeneration,
        [callback = std::move(callback)](
            const std::wstring& token, uint64_t planGeneration,
            bool success, const std::wstring& planJson,
            const std::wstring&) mutable {
            selection::SelectionContent content;
            if (success && !planJson.empty()) {
                content.kind = selection::SelectionContentKind::ZenCropPreview;
                content.fidelity = selection::SelectionFidelity::Semantic;
                content.requestToken = token;
                content.requestGeneration = planGeneration;
                content.structuredPlanJson = planJson;
            }
            callback(std::move(content));
        });
}

void TranslationCoordinator::HandlePreparedStructuredSelection(
    uint64_t workflowGeneration,
    selection::SelectionContent content,
    const std::wstring& token,
    uint64_t planGeneration,
    bool success,
    const std::wstring& planJson,
    const std::wstring& errorCode) {
    if (shuttingDown_ || workflowGeneration != generation_ ||
        !active_ || !resultWindow_ || !resultWindow_->IsValid()) {
        return;
    }
    selection::StructuredSelectionPlan parsed;
    std::wstring diagnostic;
    if (!success || token != content.requestToken ||
        planGeneration != content.requestGeneration ||
        !selection::ParseStructuredSelectionPlan(
            planJson, content.requestToken, content.requestGeneration,
            content.kind, content.fidelity, parsed, &diagnostic)) {
        OutputDebugStringW((L"[SelectionTranslation] structured conversion " +
            (errorCode.empty() ? diagnostic : errorCode) + L"\n").c_str());
        if (!content.plainText.empty()) {
            resultWindow_->SetSourceText(content.plainText);
            StartTranslationForSource(content.plainText,
                settings_.sourceLanguage, settings_.targetLanguage);
        } else {
            ShowError(StageText(
                L"无法保留此选区的结构，且没有可用的纯文本。",
                L"The selection structure could not be preserved and no plain text is available."));
        }
        return;
    }
    auto plan = std::make_shared<const selection::StructuredSelectionPlan>(
        std::move(parsed));
    resultWindow_->SetSourceText(plan->sourceMarkdown);
    StartStructuredTranslation(
        std::move(plan), settings_.sourceLanguage,
        settings_.targetLanguage);
}

void TranslationCoordinator::StartStructuredTranslation(
    std::shared_ptr<const selection::StructuredSelectionPlan> plan,
    const std::wstring& sourceLanguage,
    const std::wstring& targetLanguage) {
    if (shuttingDown_ || !plan || !resultWindow_ ||
        !resultWindow_->IsValid()) {
        return;
    }
    CancelActiveTranslation();
    selectedSourceLanguage_ = NormalizeLanguageCode(sourceLanguage, true);
    selectedTargetLanguage_ = NormalizeLanguageCode(targetLanguage, false);
    std::wstring detectionText;
    for (const auto& leaf : plan->leaves) {
        if (!detectionText.empty()) detectionText.push_back(L' ');
        detectionText += leaf.text;
        if (detectionText.size() >= 4096) break;
    }
    resolvedTargetLanguage_ = ResolveTargetLanguageForText(
        selectedTargetLanguage_, selectedSourceLanguage_, detectionText);
    if (selectedSourceLanguage_ != L"auto" &&
        selectedSourceLanguage_ == resolvedTargetLanguage_) {
        ShowError(StageText(L"源语言和目标语言不能相同。",
                            L"Source and target languages must be different."));
        return;
    }

    structuredPlan_ = std::move(plan);
    structuredMarkerNonce_ = selection::MakeSelectionRequestToken();
    structuredBlockLeaves_.clear();
    structuredLeafMarkerIndexes_.clear();
    structuredLeafTranslations_.clear();
    structuredInvalidBlocks_.clear();
    structuredRetryAttempted_ = false;
    segmentBreaksAfter_.clear();
    translationLeadingBreaks_.clear();
    translationTrailingBreaks_.clear();
    request_ = {};
    request_.sourceLanguage = selectedSourceLanguage_;
    request_.targetLanguage = resolvedTargetLanguage_;
    request_.preserveParagraphs = true;

    const auto* profile = FindActiveTranslationProvider(settings_);
    const bool directMt = profile &&
        GetCapabilities(*profile).family == TranslationProviderFamily::DirectMt;
    if (directMt) {
        structuredTranslationMode_ = StructuredTranslationMode::DirectLeaves;
        for (const auto& leaf : structuredPlan_->leaves) {
            request_.segments.push_back({leaf.id, leaf.text});
        }
    } else {
        structuredTranslationMode_ = StructuredTranslationMode::LlmBlocks;
        std::unordered_map<std::wstring,
            std::vector<const selection::StructuredSelectionLeaf*>> blockLeaves;
        std::vector<std::wstring> blockOrder;
        for (size_t index = 0; index < structuredPlan_->leaves.size(); ++index) {
            const auto& leaf = structuredPlan_->leaves[index];
            auto [found, inserted] = blockLeaves.try_emplace(
                leaf.blockId,
                std::vector<const selection::StructuredSelectionLeaf*>{});
            if (inserted) blockOrder.push_back(leaf.blockId);
            found->second.push_back(&leaf);
            structuredLeafMarkerIndexes_[leaf.id] = index + 1;
        }
        size_t blockIndex = 0;
        const auto flushBlock = [&](std::vector<std::wstring>& leafIds,
                                    std::wstring& text) {
            if (leafIds.empty()) return;
            const std::wstring requestId =
                L"zb" + std::to_wstring(++blockIndex);
            structuredBlockLeaves_[requestId] = std::move(leafIds);
            request_.segments.push_back({requestId, std::move(text)});
            leafIds.clear();
            text.clear();
        };
        for (const auto& blockId : blockOrder) {
            std::wstring text;
            std::vector<std::wstring> leafIds;
            for (const auto* leaf : blockLeaves[blockId]) {
                if (!leaf) continue;
                const std::wstring marker = L"ZC" + structuredMarkerNonce_ +
                    L"M" + std::to_wstring(
                        structuredLeafMarkerIndexes_[leaf->id]);
                const std::wstring wrapped =
                    marker + L"O" + leaf->text + marker + L"C";
                if (!leafIds.empty() && text.size() + 1 + wrapped.size() >
                        kMaximumStructuredBlockRequestChars) {
                    flushBlock(leafIds, text);
                }
                if (!text.empty()) text.push_back(L' ');
                text += wrapped;
                leafIds.push_back(leaf->id);
            }
            flushBlock(leafIds, text);
        }
    }

    resultWindow_->SetTranslationText(L"");
    resultWindow_->ClearTranslationElapsed();
    resultWindow_->SetBusy(true);
    resultWindow_->SetStage(StageText(L"正在翻译…", L"Translating..."));
    if (request_.segments.empty()) {
        FinalizeStructuredTranslation(false);
        return;
    }
    BeginTranslation(generation_);
}

bool TranslationCoordinator::AcceptStructuredTranslation(
    const TranslationSegment& translation) {
    if (!structuredPlan_) return false;
    if (structuredTranslationMode_ == StructuredTranslationMode::DirectLeaves ||
        structuredTranslationMode_ == StructuredTranslationMode::LeafRetry) {
        structuredLeafTranslations_[translation.id] = translation.text;
        return true;
    }

    const auto block = structuredBlockLeaves_.find(translation.id);
    if (block == structuredBlockLeaves_.end()) return false;
    const std::wstring markerPrefix = L"ZC" + structuredMarkerNonce_ + L"M";
    size_t markerOccurrences = 0;
    for (size_t cursor = translation.text.find(markerPrefix);
         cursor != std::wstring::npos;
         cursor = translation.text.find(markerPrefix,
             cursor + markerPrefix.size())) {
        ++markerOccurrences;
    }
    if (markerOccurrences != block->second.size() * 2) return false;

    struct MarkerRange {
        size_t start = 0;
        size_t end = 0;
        std::wstring leafId;
        std::wstring text;
    };
    std::vector<MarkerRange> ranges;
    for (const auto& leafId : block->second) {
        const auto markerIndex = structuredLeafMarkerIndexes_.find(leafId);
        if (markerIndex == structuredLeafMarkerIndexes_.end()) return false;
        const std::wstring marker = markerPrefix +
            std::to_wstring(markerIndex->second);
        const std::wstring open = marker + L"O";
        const std::wstring close = marker + L"C";
        const size_t openAt = translation.text.find(open);
        const size_t closeAt = translation.text.find(close);
        if (openAt == std::wstring::npos || closeAt == std::wstring::npos ||
            translation.text.find(open, openAt + open.size()) !=
                std::wstring::npos ||
            translation.text.find(close, closeAt + close.size()) !=
                std::wstring::npos ||
            closeAt < openAt + open.size()) {
            return false;
        }
        std::wstring translated = translation.text.substr(
            openAt + open.size(), closeAt - openAt - open.size());
        if (!selection::HasNonWhitespace(translated)) return false;
        ranges.push_back({openAt, closeAt + close.size(), leafId,
                          std::move(translated)});
    }
    std::sort(ranges.begin(), ranges.end(),
        [](const MarkerRange& left, const MarkerRange& right) {
            return left.start < right.start;
        });
    for (size_t index = 1; index < ranges.size(); ++index) {
        if (ranges[index - 1].end > ranges[index].start) return false;
    }
    for (auto& range : ranges) {
        structuredLeafTranslations_[range.leafId] = std::move(range.text);
    }
    return true;
}

bool TranslationCoordinator::BeginStructuredLeafRetry(uint64_t generation) {
    if (generation != generation_ || !structuredPlan_ ||
        structuredInvalidBlocks_.empty() ||
        structuredRetryAttempted_) {
        return false;
    }
    request_.segments.clear();
    std::unordered_set<std::wstring> retryLeafIds;
    for (const auto& blockId : structuredInvalidBlocks_) {
        const auto block = structuredBlockLeaves_.find(blockId);
        if (block == structuredBlockLeaves_.end()) continue;
        retryLeafIds.insert(block->second.begin(), block->second.end());
    }
    for (const auto& leaf : structuredPlan_->leaves) {
        if (retryLeafIds.contains(leaf.id)) {
            request_.segments.push_back({leaf.id, leaf.text});
        }
    }
    if (request_.segments.empty()) return false;
    structuredRetryAttempted_ = true;
    structuredTranslationMode_ = StructuredTranslationMode::LeafRetry;
    structuredInvalidBlocks_.clear();
    nextSegmentIndex_ = 0;
    currentBatchRequestId_.clear();
    completedBatchRequestIds_.clear();
    completedTranslations_.clear();
    translatedBuffer_.clear();
    ++generation_;
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetWorkflowGeneration(generation_);
        resultWindow_->SetStage(StageText(
            L"正在重试格式安全分段…",
            L"Retrying format-safe segments..."));
    }
    BeginTranslation(generation_);
    return true;
}

void TranslationCoordinator::FinalizeStructuredTranslation(bool degraded) {
    if (!structuredPlan_) return;
    for (const auto& leaf : structuredPlan_->leaves) {
        if (!structuredLeafTranslations_.contains(leaf.id)) {
            structuredLeafTranslations_[leaf.id] = leaf.text;
            degraded = true;
        }
    }
    translatedBuffer_ = selection::ProjectStructuredSelection(
        *structuredPlan_, structuredLeafTranslations_);
    if (translatedBuffer_.empty()) {
        ShowError(StageText(
            L"无法重建格式化译文。",
            L"The formatted translation could not be reconstructed."));
        return;
    }
    currentBatchRequestId_.clear();
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetBusy(false);
        const ULONGLONG elapsed = translationStartedTick_ == 0 ? 0 :
            GetTickCount64() - translationStartedTick_;
        resultWindow_->SetTranslationElapsed(static_cast<DWORD>((std::min)(
            elapsed, static_cast<ULONGLONG>(MAXDWORD))));
        resultWindow_->SetStage(degraded
            ? StageText(L"部分内容保留原文（格式已保护）",
                        L"Some text was kept in the source language; formatting is intact")
            : StageText(L"就绪", L"Ready"));
        resultWindow_->SetTranslationText(translatedBuffer_);
    }
}

} // namespace translation
