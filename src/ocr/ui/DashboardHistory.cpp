#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrUtils.h"      // GetOcrImageDir, NormalizeEditText (transitively includes JsonUtils.h)
#include "JsonUtils.h"     // EscapeJsonString, UnescapeJsonString, ExtractJsonField
#include "OcrBlockJson.h"
#include "Strings.h"       // S::IsChinese()
#include "core/ClipboardUtils.h"
#include "ocr/ui/dashboard/DashboardHistoryStore.h"
#include "ocr/ui/dashboard/DashboardHistoryRepository.h"
#include "ocr/ui/dashboard/DashboardHistoryModel.h"
#include "ocr/ui/dashboard/DashboardHistoryCache.h"
#include "ocr/ui/dashboard/DashboardResultProjection.h"
#include "ocr/ui/dashboard/DashboardController.h"
#include "ocr/ui/dashboard/DashboardSelectionState.h"
#include "ocr/ui/dashboard/DashboardPreviewSecurity.h"
#include "dashboard/DashboardFileTypes.h"
#include "core/WideStringUtils.h"
#include "core/WideMarkdownUtils.h"
#include "translation/TranslationCoordinator.h"
#include "ocr/OcrDocumentAlignment.h"

#include <shlwapi.h>       // PathCanonicalizeW, PathRemoveFileSpecW, PathAppendW, PathFileExistsW
#include <gdiplus.h>       // Gdiplus::Image, Gdiplus::Ok
#include <fstream>
#include <vector>
#include <set>
#include <algorithm>
#include <cstdint>
#include <climits>
#include <cwctype>

// Timer ID shared with OcrDashboardWindow.cpp (defined there too; separate TU)
#define TIMER_STATUS_CLEAR 1

// ---------------------------------------------------------------------------
// static free functions
// ---------------------------------------------------------------------------

// Thin wrappers keep existing call sites; body is pure dual-write helper.
static DashboardItemKey MakeHistorySourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    int historyIndex)
{
    return DashboardMakeHistorySourceKey(historyItems, historyIndex);
}

static int HistoryIndexFromSourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const DashboardItemKey& key)
{
    return DashboardHistoryIndexFromSourceKey(historyItems, key);
}

static std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string str(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
    return str;
}

// OWN-73: thin wrapper over pure DashboardWideStartsWithNoCase.
static bool DashboardPreviewStartsWithNoCase(const std::wstring& value, const std::wstring& prefix) {
    return DashboardWideStartsWithNoCase(value, prefix);
}

static bool DashboardPreviewCanonicalizePath(std::wstring path, std::wstring& out) {
    for (wchar_t& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
    wchar_t full[MAX_PATH] = {};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    if (len == 0 || len >= MAX_PATH) return false;
    wchar_t canonical[MAX_PATH] = {};
    if (!PathCanonicalizeW(canonical, full)) return false;
    out = canonical;
    return true;
}

static bool DashboardPreviewIsPathUnderDirectory(const std::wstring& path, const std::wstring& dir) {
    std::wstring fullPath;
    std::wstring fullDir;
    if (!DashboardPreviewCanonicalizePath(path, fullPath) ||
        !DashboardPreviewCanonicalizePath(dir, fullDir)) {
        return false;
    }
    if (!fullDir.empty() && fullDir.back() != L'\\') fullDir += L'\\';
    // OWN-74/75: pure lower + pure path-under via WideStringUtils.
    fullPath = WideToLower(std::move(fullPath));
    fullDir = WideToLower(std::move(fullDir));
    return WideIsPathStrictlyUnderDirectory(fullPath, fullDir);
}

static bool DashboardPreviewIsSafeAssetPath(std::wstring relPath, std::wstring& normalized) {
    return DashboardPreviewIsSafeRelativeAssetPath(std::move(relPath), normalized);
}

static constexpr const wchar_t* kDashboardPreviewOutputAssetUrlPrefix =
    L"https://zencrop-preview-output.invalid/";

static uint64_t DashboardPreviewHashCanonicalRoot(const std::wstring& canonicalRoot) {
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    uint64_t hash = kFnvOffsetBasis;
    // OWN-79: pure lower then FNV (same as per-char towlower fold).
    const std::wstring lower = WideToLower(canonicalRoot);
    for (wchar_t ch : lower) {
        hash ^= static_cast<uint16_t>(ch);
        hash *= kFnvPrime;
    }
    return hash;
}

static bool DashboardPreviewBuildAssetVersion(
    const std::wstring& canonicalOutput,
    const std::wstring& canonicalAsset,
    std::wstring& version)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(
            canonicalAsset.c_str(), GetFileExInfoStandard, &attributes) ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }

    ULARGE_INTEGER lastWrite = {};
    lastWrite.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    lastWrite.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    ULARGE_INTEGER fileSize = {};
    fileSize.LowPart = attributes.nFileSizeLow;
    fileSize.HighPart = attributes.nFileSizeHigh;

    // OWN-113: pure multi-hash composition (WideStringUtils).
    version =
        WideFormatHash016(static_cast<unsigned long long>(
            DashboardPreviewHashCanonicalRoot(canonicalOutput)))
        + L"-"
        + WideFormatHash016(static_cast<unsigned long long>(lastWrite.QuadPart))
        + L"-"
        + WideFormatHash016(static_cast<unsigned long long>(fileSize.QuadPart));
    return true;
}

static bool DashboardPreviewBuildAssetVirtualUrl(
    const std::wstring& outputDir,
    const std::wstring& relPath,
    std::wstring& assetUrl)
{
    assetUrl.clear();
    if (outputDir.empty()) return false;

    std::wstring normalizedRel;
    if (!DashboardPreviewIsSafeAssetPath(relPath, normalizedRel)) return false;

    // OWN-96: pure path join (WideStringUtils).
    const std::wstring fullPath = WideJoinPath(outputDir, normalizedRel);
    if (fullPath.empty()) return false;

    std::wstring canonicalOutput;
    std::wstring canonicalAsset;
    if (!DashboardPreviewCanonicalizePath(outputDir, canonicalOutput) ||
        !DashboardPreviewCanonicalizePath(fullPath, canonicalAsset) ||
        !DashboardPreviewIsPathUnderDirectory(canonicalAsset, canonicalOutput)) {
        return false;
    }

    std::wstring assetVersion;
    if (!DashboardPreviewBuildAssetVersion(canonicalOutput, canonicalAsset, assetVersion)) {
        return false;
    }

    std::wstring urlPath = normalizedRel;
    for (wchar_t& ch : urlPath) {
        if (ch == L'\\') ch = L'/';
    }
    assetUrl = std::wstring(kDashboardPreviewOutputAssetUrlPrefix) +
        UrlEncode(urlPath) + L"?v=" + assetVersion;
    return true;
}

static std::wstring DashboardPreviewRewriteHtmlAssetImages(
    const std::wstring& markdown,
    const std::wstring& outputDir)
{
    std::wstring out;
    out.reserve(markdown.size());
    size_t pos = 0;

    while (pos < markdown.size()) {
        size_t src = markdown.find(L"src", pos);
        if (src == std::wstring::npos) {
            out.append(markdown, pos, std::wstring::npos);
            break;
        }

        out.append(markdown, pos, src - pos);
        size_t cursor = src + 3;
        while (cursor < markdown.size() && iswspace(markdown[cursor])) cursor++;
        if (cursor >= markdown.size() || markdown[cursor] != L'=') {
            out.append(markdown, src, cursor - src);
            pos = cursor;
            continue;
        }
        cursor++;
        while (cursor < markdown.size() && iswspace(markdown[cursor])) cursor++;
        if (cursor >= markdown.size() || (markdown[cursor] != L'"' && markdown[cursor] != L'\'')) {
            out.append(markdown, src, cursor - src);
            pos = cursor;
            continue;
        }

        wchar_t quote = markdown[cursor];
        size_t valueStart = cursor + 1;
        size_t valueEnd = markdown.find(quote, valueStart);
        if (valueEnd == std::wstring::npos) {
            out.append(markdown, src, std::wstring::npos);
            break;
        }

        std::wstring value = markdown.substr(valueStart, valueEnd - valueStart);
        std::wstring assetUrl;
        if (DashboardPreviewBuildAssetVirtualUrl(outputDir, value, assetUrl)) {
            out.append(markdown, src, valueStart - src);
            out += assetUrl;
            out.push_back(quote);
        } else {
            out.append(markdown, src, valueEnd + 1 - src);
        }
        pos = valueEnd + 1;
    }

    return out;
}

// OWN-74: pure markdown image find via WideStringUtils.
static bool DashboardPreviewFindNextMarkdownImage(
    const std::wstring& markdown,
    size_t start,
    size_t& marker,
    size_t& altClose)
{
    return WideFindNextMarkdownImage(markdown, start, marker, altClose);
}

static std::wstring DashboardPreviewRewriteMarkdownAssetImages(
    const std::wstring& markdown,
    const std::wstring& outputDir)
{
    std::wstring out;
    out.reserve(markdown.size());
    size_t pos = 0;

    while (pos < markdown.size()) {
        size_t marker = std::wstring::npos;
        size_t altClose = std::wstring::npos;
        if (!DashboardPreviewFindNextMarkdownImage(markdown, pos, marker, altClose)) {
            out.append(markdown, pos, std::wstring::npos);
            break;
        }

        size_t valueStart = altClose + 2;
        size_t valueEnd = markdown.find(L')', valueStart);
        if (valueEnd == std::wstring::npos) {
            out.append(markdown, pos, std::wstring::npos);
            break;
        }

        out.append(markdown, pos, valueStart - pos);
        std::wstring value = markdown.substr(valueStart, valueEnd - valueStart);
        std::wstring assetUrl;
        if (DashboardPreviewBuildAssetVirtualUrl(outputDir, value, assetUrl)) {
            out += assetUrl;
        } else {
            out += value;
        }
        out.push_back(L')');
        pos = valueEnd + 1;
    }

    return out;
}

static std::wstring DashboardPreviewRewriteAssetImages(
    const std::wstring& markdown,
    const std::wstring& outputDir)
{
    return DashboardPreviewRewriteMarkdownAssetImages(
        DashboardPreviewRewriteHtmlAssetImages(markdown, outputDir),
        outputDir);
}

static std::wstring Utf8ToWString(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
    return wstr;
}








// 加载历史记录。
// 返回值语义：
//   - 文件不存在：返回 true，items 为空（调用方可安全写入新数据）
//   - 文件存在且解析成功：返回 true，items 填充
//   - 文件存在但解析失败（损坏/截断/编码异常）：返回 false，items 为空，
//     并把原文件复制到唯一 .bad.* 备份；调用方绝不能用单条新数据覆盖。

// 原子写入历史记录：写 .tmp → flush+close 检查 → MoveFileExW 替换正式文件。
// 避免进程崩溃/磁盘满/杀软拦截中途导致 ocr_history.json 被截断损坏。

// D-C-S2: history path canonicalize/under/normalize sole in DashboardHistoryCache.
static bool CanonicalizePath(std::wstring path, std::wstring& out) {
    return DashboardHistoryCacheCanonicalizePath(std::move(path), out);
}

static bool IsPathUnderDirectory(const std::wstring& path, const std::wstring& dir) {
    return DashboardHistoryCacheIsPathUnderDirectory(path, dir);
}

static std::wstring NormalizeDashboardHistoryPath(std::wstring path) {
    return DashboardHistoryCacheNormalizePath(std::move(path));
}












// OWN-73: pure case-insensitive equality (no CompareStringOrdinal).
static bool SameNonEmptyTextNoCase(const std::wstring& left, const std::wstring& right) {
    return !left.empty() && !right.empty() && DashboardWideEqualsNoCase(left, right);
}

// D-C-S8: path equality sole in DashboardHistoryCache.
static bool SameDashboardPath(const std::wstring& left, const std::wstring& right) {
    return DashboardHistoryCacheSamePath(left, right);
}

// D-C-PERSIST: Load/Save/Dismiss disk ops sole in DashboardHistorySession free functions.
// Window methods deleted — call sites use DashboardHistorySession* APIs directly.

static bool SameDashboardImageJobKey(const BatchOcrImageJob& left, const BatchOcrImageJob& right) {
    return DashboardSameImageJobIdentity(left, right);
}

static bool SameDashboardPdfJobKey(const BatchOcrPdfJob& left, const BatchOcrPdfJob& right) {
    return DashboardSamePdfJobIdentity(left, right);
}

// OWN-75: map product enum → pure running-token check via BatchOcrTaskStatusToString.
static bool DashboardStatusIsRunning(BatchOcrTaskStatus status) {
    return WideIsRunningBatchStatusToken(BatchOcrTaskStatusToString(status));
}

static std::wstring DashboardHistoryPdfPagePauseKey(const BatchOcrPdfJob& job, int pageIndex) {
    return WidePdfPagePauseKey(DashboardPdfJobTreeKey(job), pageIndex);
}

static void EraseDashboardPdfKey(std::vector<std::wstring>& keys, const std::wstring& key) {
    if (key.empty()) return;
    keys.erase(
        std::remove_if(keys.begin(), keys.end(),
            [&](const std::wstring& existing) {
                return DashboardPdfJobTreeKeyEquals(existing, key);
            }),
        keys.end());
}

static void EraseDashboardPdfPageKeysForJob(std::vector<std::wstring>& keys, const BatchOcrPdfJob& job) {
    std::wstring prefix = DashboardPdfJobTreeKey(job);
    if (prefix.empty()) return;
    prefix += L"#page:";
    // OWN-74: pure lower via WideStringUtils.
    const std::wstring normalizedPrefix = WideToLower(prefix);
    keys.erase(
        std::remove_if(keys.begin(), keys.end(),
            [&](std::wstring existing) {
                return WideToLower(std::move(existing)).rfind(normalizedPrefix, 0) == 0;
            }),
        keys.end());
}

// OWN-75: thin wrappers over pure WideStringUtils helpers.
static std::wstring TrimTrailingLineBreaks(std::wstring text) {
    return WideTrimTrailingLineBreaks(std::move(text));
}

static void AppendPlainLineBreak(std::wstring& out) {
    WideAppendPlainLineBreak(out);
}

static bool IsFormatMarker(wchar_t ch) {
    return WideIsFormatMarker(ch);
}

static bool ReadUtf8FileToWide(const std::wstring& path, std::wstring& out) {
    out.clear();
    if (path.empty() || !PathFileExistsW(path.c_str())) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    out = Utf8ToWString(bytes);
    return true;
}

// D-C-S3: Build*Json / Build*SummaryText / StripMarkdown sole in DashboardResultProjection.
// Call sites use DashboardResultProjection* names directly (no local alias wrappers).

// ---------------------------------------------------------------------------
// OcrDashboardWindow member functions
// ---------------------------------------------------------------------------

bool OcrDashboardWindow::AddHistoryItem(const OcrDashboardHistoryItem& item) {
    int newIndex = -1;
    OcrDashboardHistoryItem replaced;
    bool replacedExisting = false;
    if (IsValidBatchOcrSourceInstanceId(item.sourceInstanceId) &&
        !item.originManifestPath.empty()) {
        for (int index = 0; index < static_cast<int>(m_history.model.items.size()); ++index) {
            const auto& existing = m_history.model.items[static_cast<size_t>(index)];
            if (DashboardProjectionTextEquals(existing.sourceInstanceId, item.sourceInstanceId) &&
                DashboardProjectionTextEquals(existing.originManifestPath, item.originManifestPath)) {
                replaced = existing;
                m_history.model.items[static_cast<size_t>(index)] = item;
                newIndex = index;
                replacedExisting = true;
                break;
            }
        }
    }
    if (!replacedExisting) {
        m_history.model.items.push_back(item);
        newIndex = static_cast<int>(m_history.model.items.size()) - 1;
    }
    // Keep pure model aligned before filter/persist (write dual-write).
    DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
    ApplyFilter(DashboardStateFilterText(m_dashboardState));
    if (!DashboardHistorySessionSaveItems(m_history, m_dashboardState)) {
        if (replacedExisting) {
            m_history.model.items[static_cast<size_t>(newIndex)] = std::move(replaced);
        } else {
            m_history.model.items.pop_back();
        }
        // Save failed before dual-write; restore pure model to Window authority.
        DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
        ApplyFilter(DashboardStateFilterText(m_dashboardState));
        return false;
    }
    if (replacedExisting) {
        // Metadata is durable now. The old transient cache can only be
        // released after the replacement record is visible to future loads.
        DeleteCacheImagesForItems({replaced});
    }
    if (IsValidBatchOcrSourceInstanceId(item.sourceInstanceId)) {
        for (int taskIndex = 0; taskIndex < static_cast<int>(m_batch.batchTasks.size()); ++taskIndex) {
            if (DashboardProjectionTextEquals(
                    m_batch.batchTasks[static_cast<size_t>(taskIndex)].job.sourceInstanceId,
                    item.sourceInstanceId)) {
                const auto& candidateJob = m_batch.batchTasks[static_cast<size_t>(taskIndex)].job;
                if (!item.originManifestPath.empty() && !candidateJob.manifestPath.empty() &&
                    !DashboardProjectionTextEquals(
                        item.originManifestPath,
                        candidateJob.manifestPath)) {
                    continue;
                }
                bool sameTaskWasActive = DashboardStateHasImageTaskSelection(m_dashboardState) &&
                    IsImageTaskSelectionForTask(m_batch.batchTasks[static_cast<size_t>(taskIndex)]);
                bool nothingWasActive = DashboardStateHasNoTaskSelection(m_dashboardState) &&
                    DashboardStateSelectedHistoryIndex(m_dashboardState) < 0;
                if (sameTaskWasActive || nothingWasActive) {
                    ActivateSourceRailImageTask(taskIndex);
                    // OCR 完成后默认展示 Preview（host 不可用时 SetTextMode 会 fallback Source）。
                    SetTextMode(DashboardTextMode::Preview);
                } else {
                    UpdateSourceRailScrollInfo();
                    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
                }
                return true;
            }
        }
    }
    if (std::find(DashboardStateVisibleHistoryIndices(m_dashboardState).begin(),
            DashboardStateVisibleHistoryIndices(m_dashboardState).end(), newIndex) ==
            DashboardStateVisibleHistoryIndices(m_dashboardState).end() && m_searchEdit) {
        SetWindowTextW(m_searchEdit, L"");
    }
    SelectHistoryItem(newIndex);
    // OCR 完成后默认展示 Preview（host 不可用时 SetTextMode 会 fallback Source）。
    SetTextMode(DashboardTextMode::Preview);
    return true;
}

void OcrDashboardWindow::SetHistoryEditText(const std::wstring& text) {
    if (!m_edit) return;
    SendMessageW(m_edit, EM_SETREADONLY, FALSE, 0);
    SetWindowTextW(m_edit, text.c_str());
    SendMessageW(m_edit, EM_SETREADONLY, TRUE, 0);
}

void OcrDashboardWindow::AppendHistoryEditText(const std::wstring& text) {
    if (!m_edit) return;
    SendMessageW(m_edit, EM_SETREADONLY, FALSE, 0);
    int textLen = GetWindowTextLengthW(m_edit);
    SendMessageW(m_edit, EM_SETSEL, textLen, textLen);
    SendMessageW(m_edit, EM_REPLACESEL, TRUE, (LPARAM)text.c_str());
    SendMessageW(m_edit, EM_SETREADONLY, TRUE, 0);
}

std::wstring OcrDashboardWindow::BuildPreviewText(const std::wstring& text, int maxLines, size_t maxChars, bool& truncated) const {
    // D-C-S8: Host only measures edit metrics; pure truncate in DashboardHistoryBuildPreviewText.
    size_t effectiveMaxChars = maxChars;
    if (m_edit && maxLines > 0) {
        RECT rc = {};
        GetClientRect(m_edit, &rc);
        HDC hdc = GetDC(m_edit);
        if (hdc) {
            HFONT hFont = (HFONT)SendMessage(m_edit, WM_GETFONT, 0, 0);
            HFONT oldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;
            TEXTMETRIC tm = {};
            GetTextMetrics(hdc, &tm);
            if (oldFont) SelectObject(hdc, oldFont);
            ReleaseDC(m_edit, hdc);

            int usableWidth = max(m_metrics.previewMinWidth, rc.right - rc.left - m_metrics.previewPaddingX);
            int avgCharWidth = max(1, tm.tmAveCharWidth);
            size_t visualLimit = (size_t)maxLines * (size_t)max(m_metrics.previewMinChars, usableWidth / avgCharWidth);
            effectiveMaxChars = (std::min)(maxChars, visualLimit);
        }
    }
    return DashboardHistoryBuildPreviewText(text, maxLines, effectiveMaxChars, truncated);
}

void OcrDashboardWindow::RebuildHistoryText(bool preserveScroll) {
    if (!m_edit) return;
    m_historyRanges.clear();
    m_previewInfos.clear();
    m_actionButtons.clear();
    // D-D-6: hoveredActionBtn sole on DashboardState.
    DashboardStateSetHoveredActionBtn(m_dashboardState, -1);

    if (!DashboardStateHasPdfSelection(m_dashboardState) &&
        !DashboardStateHasImageTaskSelection(m_dashboardState) &&
        m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState)) == nullptr) {
        SetHistoryEditText(L"");
        return;
    }

    std::wstring text = GetCurrentResultText();
    SetHistoryEditText(text);
    if (!preserveScroll) {
        SendMessageW(m_edit, EM_SETSEL, 0, 0);
        SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
    }
    InvalidateRect(m_edit, nullptr, FALSE);
}

void OcrDashboardWindow::ApplyFilter(const std::wstring& filterText) {
    // Pure selectedSourceKey is read authority for previous active key.
    DashboardItemKey previousHistoryActiveKey = DashboardStateSelectedSourceKey(m_dashboardState);
    // D-D-1: pure filter/visible rebuild via Controller; Host does UI rebuilds.
    const std::vector<int> skipLinked = DashboardControllerProjectionLinkedHistoryIndices(
        m_batch.batchTasks, m_batch.activePdfJobs, m_history.model.items);
    DashboardControllerApplyFilter(
        m_dashboardState, m_history.model, skipLinked, filterText);
    RebuildSourceList();

    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        RebuildHistoryText(true);
        RenderSelectedItemPreview();
        RefreshCurrentBlocks();
        UpdatePreviewControls();
        return;
    }

    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        RebuildHistoryText(true);
        RenderSelectedItemPreview();
        UpdatePreviewControls();
        return;
    }

    int stableActiveHistoryIndex = DashboardHistoryIndexFromSourceKey(
        m_history.model.items, previousHistoryActiveKey);
    if (stableActiveHistoryIndex >= 0) {
        SetSelectedHistoryIndex(stableActiveHistoryIndex);
    }
    bool selectedVisible = std::find(
            DashboardStateVisibleHistoryIndices(m_dashboardState).begin(),
            DashboardStateVisibleHistoryIndices(m_dashboardState).end(),
            DashboardStateSelectedHistoryIndex(m_dashboardState)) != DashboardStateVisibleHistoryIndices(m_dashboardState).end();
    if (!DashboardStateHasVisibleHistory(m_dashboardState)) {
        SelectHistoryItem(-1);
    } else if (!selectedVisible) {
        SelectHistoryItem(DashboardStateLastVisibleHistoryIndex(m_dashboardState));
    } else {
        SelectHistoryItem(DashboardStateSelectedHistoryIndex(m_dashboardState));
    }
    UpdatePreviewControls();
}

// D-C-S6: GetSourceListPositionForHistoryIndex deleted —
// call sites use DashboardStateVisibleHistoryPosition.

void OcrDashboardWindow::RebuildSourceList() {
    if (!m_sourceList) return;

    // D-D-4: updatingSourceList sole on DashboardState (Window dual-write field deleted).
    DashboardStateSetUpdatingSourceList(m_dashboardState, true);
    UpdateSourceRailHeader();
    UpdateSourceRailScrollInfo();
    DashboardStateSetUpdatingSourceList(m_dashboardState, false);
    SyncSourceListSelectionToActive(false);
}

void OcrDashboardWindow::SyncSourceListSelectionToActive(bool preserveMultiSelection) {
    if (!m_sourceList) return;
    const int selected = DashboardStateSelectedHistoryIndex(m_dashboardState);
    int pos = DashboardStateVisibleHistoryPosition(m_dashboardState, selected);
    DashboardStateSetUpdatingSourceList(m_dashboardState, true);
    if (!preserveMultiSelection) {
        // D-D-3: selectedSourceKeys / anchor sole on DashboardState.
        DashboardStateClearSelectedSourceKeys(m_dashboardState);
        DashboardStateClearSelectedSourceAnchor(m_dashboardState);
    }
    if (pos >= 0) {
        // Pure selectedSourceKey is read authority; rebuild if empty.
        DashboardItemKey key = DashboardStateHasSelectedSourceKey(m_dashboardState)
            ? DashboardStateSelectedSourceKey(m_dashboardState)
            : DashboardMakeHistorySourceKey(m_history.model.items, selected);
        std::vector<DashboardItemKey> keys = DashboardStateSelectedSourceKeys(m_dashboardState);
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
        }
        DashboardStateSetSelectedSourceKey(m_dashboardState, key);
        if (!DashboardStateHasSelectedSourceAnchor(m_dashboardState)) {
            DashboardStateSetSelectedSourceAnchor(m_dashboardState, key);
        }
        DashboardStateSetSelectedSourceKeys(m_dashboardState, std::move(keys));
        EnsureSourceRailItemVisible(selected);
    }
    DashboardStateSetUpdatingSourceList(m_dashboardState, false);
    InvalidateRect(m_sourceList, nullptr, FALSE);
}

// D-C-S7: GetSelectedSourceHistoryIndices deleted —
// call sites use DashboardHistorySelectedIndices(items, keys, fallbackIndex).

void OcrDashboardWindow::OnSourceListSelectionChanged() {
    if (DashboardStateIsUpdatingSourceList(m_dashboardState) || !m_sourceList) return;
    InvalidateRect(m_sourceList, nullptr, FALSE);
}

void OcrDashboardWindow::SelectHistoryItem(int index, bool syncSourceList) {
    const bool changesTranslationContext =
        DashboardStateHasImageTaskSelection(m_dashboardState) ||
        DashboardStateHasPdfSelection(m_dashboardState) ||
        DashboardStateSelectedHistoryIndex(m_dashboardState) != index;
    if (changesTranslationContext) StopDashboardTranslation();
    // P2.2: 切换选中项时清空 block 单独预览内容，避免残留
    DashboardStateClearPreviewBlockContent(m_dashboardState);
    if (index < 0 || index >= (int)m_history.model.items.size()) {
        ClearImageTaskSelection();
        ClearPdfSelection();
        m_dashboardState.selectedBatchRows.clear();
        m_dashboardState.batchSelectionAnchor = {};
        SetSelectedHistoryIndex(-1);
        DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
        // D-D-3: history multi-select sole on DashboardState.
        DashboardStateClearSelectedSourceKeys(m_dashboardState);
        DashboardStateClearSelectedSourceAnchor(m_dashboardState);
        ReleaseGdiplusImages();
        RebuildHistoryText(true);
        RenderSelectedItemPreview();
        RefreshCurrentBlocks();
        UpdatePreviewControls();
        if (syncSourceList) SyncSourceListSelectionToActive(false);
        InvalidateRect(m_imageArea, nullptr, TRUE);
        return;
    }

    bool selectionChanged = (DashboardStateSelectedHistoryIndex(m_dashboardState) != index);
    const bool sameSelection = !DashboardStateHasImageTaskSelection(m_dashboardState) &&
        !DashboardStateHasPdfSelection(m_dashboardState) &&
        !selectionChanged;
    ClearPdfSelection();
    ClearImageTaskSelection();
    m_dashboardState.selectedBatchRows.clear();
    m_dashboardState.batchSelectionAnchor = {};
    SetSelectedHistoryIndex(index);
    // D-D-3: selectedSourceKey already sole authority via SetSelectedHistoryIndex.
    if (selectionChanged) {
        DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
    }
    // Prefer pure model when dual-write is intact; Window remains write authority.
    const auto* itemPtr = m_history.model.itemAt(index);
    if (itemPtr == nullptr) return;
    const auto& item = *itemPtr;

    // Load and update image (P1.4: 走 LoadImageIntoCanvas 以应用 4K 下采样)
    const bool reuseCanvas = CanReuseCanvasForActivation(sameSelection, item.imagePath);
    if (!reuseCanvas) {
        LoadImageIntoCanvas(item.imagePath, false);
    }

    if (!reuseCanvas) ShowImageHint();

    // Refresh ImageArea
    RefreshCurrentBlocks();
    InvalidateRect(m_imageArea, nullptr, FALSE);
    RebuildHistoryText(false);
    RenderSelectedItemPreview();
    UpdatePreviewControls();
    if (syncSourceList) SyncSourceListSelectionToActive(false);
}

void OcrDashboardWindow::ToggleHistoryExpansionAtPoint(int x, int y) {
    if (!m_edit) return;

    POINT pt = { x, y };
    int charIndex = LOWORD(SendMessageW(m_edit, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, pt.y)));
    int itemIndex = -1;
    for (const auto& rng : m_historyRanges) {
        if (charIndex >= rng.startChar && charIndex <= rng.endChar) {
            itemIndex = rng.itemIndex;
            break;
        }
    }
    if (itemIndex < 0) return;

    if (DashboardStateSelectedHistoryIndex(m_dashboardState) != itemIndex) {
        SelectHistoryItem(itemIndex);
    }

    DashboardStateSetExpandedHistoryIndex(m_dashboardState, (DashboardStateExpandedHistoryIndex(m_dashboardState) == itemIndex) ? -1 : itemIndex);
    RebuildHistoryText(true);

    for (const auto& rng : m_historyRanges) {
        if (rng.itemIndex == itemIndex) {
            SendMessageW(m_edit, EM_SETSEL, rng.startChar, rng.startChar);
            SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
            break;
        }
    }
}

void OcrDashboardWindow::CopyHistoryItem(int index) {
    const auto* historyItem = m_history.model.itemAt(index);
    if (!historyItem) return;
    const std::wstring& text = historyItem->text;
    if (text.empty()) return;

    if (!CopyTextToClipboard(m_hwnd, text)) return;
    // OWN-124: pure hash index label (WideStringUtils).
    UpdateStatus(S::IsChinese() ?
        L"✓ 已复制 " + WideFormatHashIndex(index + 1) + L" 的文本" :
        L"✓ Copied " + WideFormatHashIndex(index + 1) + L" text");
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
}

void OcrDashboardWindow::StopDashboardTranslation() {
    if (m_dashboardTranslation) {
        m_dashboardTranslation->Shutdown();
        m_dashboardTranslation.reset();
    }
    ClearTranslationProjection(true);
}

std::wstring OcrDashboardWindow::RewritePreviewAssetImages(
    const std::wstring& markdown,
    const std::wstring& assetRoot) const
{
    return DashboardPreviewRewriteAssetImages(markdown, assetRoot);
}

void OcrDashboardWindow::StartCurrentTranslation(bool forceRefresh) {
    const auto selectedRows = GetSelectedSourceRailRows();
    const bool singleSelection = selectedRows.size() == 1;
    const bool hasHistorySelection =
        m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState)) != nullptr;
    const bool hasImageTaskSelection = DashboardStateHasImageTaskSelection(m_dashboardState) &&
        GetSelectedImageTask() != nullptr;
    const bool hasPdfSelection = DashboardStateHasPdfSelection(m_dashboardState);
    const bool hasResultSelection = singleSelection &&
        (DashboardCanCopyResultSelection(hasHistorySelection, hasPdfSelection) ||
         hasImageTaskSelection);
    const bool jsonMode =
        DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Json;

    if (!hasResultSelection || jsonMode) {
        UpdateStatus(S::IsChinese()
            ? L"请选择一条已完成的 OCR 结果（JSON 模式不可翻译）"
            : L"Select one completed OCR result (JSON mode cannot be translated)");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2200, nullptr);
        UpdatePreviewControls();
        return;
    }

    const std::wstring sourceMarkdown = CanonicalizeOcrMarkdownSource(
        GetCurrentPreviewSourceMarkdown());
    if (WideTrim(DashboardResultProjectionPrepareTranslationText(sourceMarkdown)).empty()) {
        UpdateStatus(S::IsChinese()
            ? L"当前结果没有可翻译的文字"
            : L"The current result has no translatable text");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2200, nullptr);
        UpdatePreviewControls();
        return;
    }

    if (m_dashboardTranslation) {
        m_dashboardTranslation->Shutdown();
        m_dashboardTranslation.reset();
    }
    ClearTranslationProjection(false);
    m_layout.translationVisible = true;
    m_translationBusy = true;
    m_translationSourceMarkdown = sourceMarkdown;
    m_translationMarkdown = sourceMarkdown;
    m_translationAssetRoot = GetCurrentPreviewAssetRoot();
    RefreshCurrentBlocks();
    m_translationBlocks = m_canvas.currentBlocks;

    std::vector<translation::TranslationSegment> segments;
    std::vector<OcrLayoutBlock> layoutBlocks;
    layoutBlocks.reserve(m_canvas.currentBlocks.size());
    for (const auto& block : m_canvas.currentBlocks) {
        OcrLayoutBlock item;
        item.id = block.id;
        item.pageIndex = block.pageIndex;
        item.order = block.order;
        item.label = block.label;
        item.content = block.content;
        item.bbox = block.bbox;
        item.polygon = block.polygon;
        item.confidence = block.confidence;
        item.source = block.source;
        item.groupId = block.groupId;
        item.edited = block.edited;
        item.editBaseline = block.editBaseline;
        layoutBlocks.push_back(std::move(item));
    }
    std::vector<OcrBlockSourceMapEntry> sourceMap;
    std::wstring revision;
    std::wstring alignmentError;
    OcrAlignmentState semanticState = OcrAlignmentState::NotChecked;
    if (!layoutBlocks.empty()) {
        BuildVerifiedBlockSourceMap(
            sourceMarkdown, layoutBlocks, sourceMap, revision,
            semanticState, alignmentError);
    }
    for (const auto& entry : sourceMap) {
        if (entry.relation != OcrBlockSourceRelation::Direct ||
            entry.sourceStart < 0 || entry.sourceEnd <= entry.sourceStart ||
            static_cast<size_t>(entry.sourceEnd) > sourceMarkdown.size()) continue;
        const std::wstring source = sourceMarkdown.substr(
            static_cast<size_t>(entry.sourceStart),
            static_cast<size_t>(entry.sourceEnd - entry.sourceStart));
        if (source.find(L"![") != std::wstring::npos ||
            source.find(L"<img") != std::wstring::npos ||
            source.find(L"zencrop-asset://") != std::wstring::npos) continue;
        const std::wstring prepared =
            DashboardResultProjectionPrepareTranslationText(source);
        if (WideTrim(prepared).empty()) continue;
        const std::wstring segmentId = L"b" + std::to_wstring(segments.size() + 1);
        segments.push_back({segmentId, prepared});
        m_translationRanges.push_back({
            segmentId, entry.contentOwnerId.empty() ? entry.blockId : entry.contentOwnerId,
            entry.sourceStart, entry.sourceEnd});
    }
    // Keep verified ranges when available. Any source text without a verified
    // range remains unchanged in the translated Markdown, so its original
    // block identity is still valid. Only the no-range case needs the
    // line-based projection, which cannot preserve block mapping.
    if (segments.empty()) {
        // Without a verified block-to-source map, do not pass the original
        // block model to the translated preview: its text no longer matches
        // the translated Markdown and would create false block highlights.
        m_translationBlocks.clear();
        size_t lineStart = 0;
        while (lineStart < sourceMarkdown.size()) {
            size_t lineEnd = sourceMarkdown.find(L'\n', lineStart);
            if (lineEnd == std::wstring::npos) lineEnd = sourceMarkdown.size();
            const std::wstring line = sourceMarkdown.substr(lineStart, lineEnd - lineStart);
            const bool containsImage = line.find(L"![") != std::wstring::npos ||
                line.find(L"<img") != std::wstring::npos ||
                line.find(L"zencrop-asset://") != std::wstring::npos;
            const std::wstring prepared = containsImage
                ? L"" : DashboardResultProjectionPrepareTranslationText(line);
            if (!WideTrim(prepared).empty()) {
                const std::wstring segmentId = L"l" + std::to_wstring(segments.size() + 1);
                segments.push_back({segmentId, prepared});
                m_translationRanges.push_back({
                    segmentId, L"", static_cast<int64_t>(lineStart),
                    static_cast<int64_t>(lineEnd)});
            }
            if (lineEnd >= sourceMarkdown.size()) break;
            lineStart = lineEnd + 1;
        }
    }
    if (segments.empty()) {
        m_translationBusy = false;
        m_translationError = S::IsChinese()
            ? L"当前结果没有可翻译的文字"
            : L"The current result has no translatable text";
        EnsureTranslationPreviewHost();
        LayoutControls();
        RenderTranslationPreview();
        UpdatePreviewControls();
        return;
    }

    const TranslationSettings translationSettings = LoadTranslationSettings();
    std::wstring cacheError;
    DashboardTranslationCacheBuildKey(
        sourceMarkdown, translationSettings,
        m_translationCacheKey, m_translationSourceRevisionSha256, cacheError);

    if (!EnsureTranslationPreviewHost()) {
        m_translationBusy = false;
        m_translationError = S::IsChinese()
            ? L"翻译预览不可用"
            : L"Translation preview is unavailable";
        LayoutControls();
        RenderTranslationPreview();
        UpdatePreviewControls();
        return;
    }
    LayoutControls();
    RenderTranslationPreview();

    if (!forceRefresh && !m_translationCacheKey.empty()) {
        DashboardTranslationCacheEntry cached;
        bool valid = DashboardTranslationCacheLoad(
            m_translationCacheKey, m_translationSourceRevisionSha256, cached);
        if (valid && cached.translations.size() == segments.size()) {
            for (size_t i = 0; i < segments.size(); ++i) {
                if (cached.translations[i].id != segments[i].id ||
                    cached.translations[i].text.empty()) {
                    valid = false;
                    break;
                }
            }
        } else {
            valid = false;
        }
        if (valid) {
            ApplyTranslationSegments(cached.translations, true);
            UpdatePreviewControls();
            return;
        }
    }

    RECT sourceRect = {};
    if (m_hwnd) GetWindowRect(m_hwnd, &sourceRect);
    if (!m_dashboardTranslation) {
        m_dashboardTranslation = std::make_unique<translation::TranslationCoordinator>();
    }
    if (!m_dashboardTranslation->StartEmbeddedSegments(
            m_hwnd, sourceRect, segments, this)) {
        UpdatePreviewControls();
        return;
    }

    UpdateStatus(S::IsChinese()
        ? L"正在翻译当前 OCR 结果…"
        : L"Translating the current OCR result…");
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2200, nullptr);
    UpdatePreviewControls();
}

std::wstring OcrDashboardWindow::GetCurrentResultText() const {
    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        const DashboardBatchTaskItem* task = GetSelectedImageTask();
        if (!task) {
            return S::IsChinese()
                ? L"图片任务不在当前列表中。"
                : L"The selected image task is no longer available.";
        }

        std::wstring fileText;
        std::wstring summary = DashboardResultProjectionImageTaskSummaryText(*task);
        const bool taskOutputEligible = task->status == BatchOcrTaskStatus::Completed;
        int linkedHistoryIndex = FindLinkedHistoryIndexForImageTask(task->job);
        const OcrDashboardHistoryItem* linkedHistory = m_history.model.itemAt(linkedHistoryIndex);
        switch (DashboardStateTextModeEffective(m_dashboardState)) {
        case DashboardTextMode::Text:
            if (taskOutputEligible && ReadUtf8FileToWide(task->job.textPath, fileText)) {
                return WideNormalizeAndTrimEditText(fileText);
            }
            if (taskOutputEligible && ReadUtf8FileToWide(task->job.markdownPath, fileText)) {
                return DashboardResultProjectionStripMarkdown(fileText);
            }
            if (linkedHistory && !linkedHistory->text.empty()) {
                return DashboardResultProjectionStripMarkdown(linkedHistory->text);
            }
            return DashboardResultProjectionStripMarkdown(summary);
        case DashboardTextMode::Json:
            if (taskOutputEligible && ReadUtf8FileToWide(task->job.contentJsonPath, fileText)) {
                return WideNormalizeAndTrimEditText(fileText);
            }
            if (linkedHistory) {
                return DashboardResultProjectionHistoryItemJson(*linkedHistory, linkedHistoryIndex);
            }
            return DashboardResultProjectionImageTaskJson(*task);
        case DashboardTextMode::Preview:
        case DashboardTextMode::Source:
        default:
            if (taskOutputEligible && ReadUtf8FileToWide(task->job.markdownPath, fileText)) {
                return WideNormalizeAndTrimEditText(fileText);
            }
            if (linkedHistory && !linkedHistory->text.empty()) {
                return WideNormalizeAndTrimEditText(linkedHistory->text);
            }
            return summary;
        }
    }

    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
        if (!job) {
            return S::IsChinese()
                ? L"PDF 任务不在当前列表中。"
                : L"The selected PDF job is no longer available.";
        }

        std::wstring fileText;
        if (DashboardStatePdfSelectionPageIndex(m_dashboardState) <= 0) {
            std::wstring summary = DashboardResultProjectionPdfJobSummaryText(*job);
            switch (DashboardStateTextModeEffective(m_dashboardState)) {
            case DashboardTextMode::Text:
                if (ReadUtf8FileToWide(job->textPath, fileText)) {
                    return WideNormalizeAndTrimEditText(fileText);
                }
                return DashboardResultProjectionStripMarkdown(summary);
            case DashboardTextMode::Json:
                if (ReadUtf8FileToWide(job->contentJsonPath, fileText)) {
                    return WideNormalizeAndTrimEditText(fileText);
                }
                return DashboardResultProjectionPdfJobJson(*job);
            case DashboardTextMode::Preview:
            case DashboardTextMode::Source:
            default:
                if (ReadUtf8FileToWide(job->markdownPath, fileText)) {
                    return WideNormalizeAndTrimEditText(fileText);
                }
                return summary;
            }
        }

        const BatchOcrPdfPageJob* page = DashboardFindPdfSelectionPage(*job, DashboardStatePdfSelectionPageIndex(m_dashboardState));
        if (!page) {
            return S::IsChinese()
                ? L"PDF 页面不在当前任务中。"
                : L"The selected PDF page is no longer available.";
        }

        std::wstring summary = DashboardResultProjectionPdfPageSummaryText(*job, *page);
        switch (DashboardStateTextModeEffective(m_dashboardState)) {
        case DashboardTextMode::Text:
            if (!page->plainText.empty()) {
                return WideNormalizeAndTrimEditText(page->plainText);
            }
            if (ReadUtf8FileToWide(page->textPath, fileText)) {
                return WideNormalizeAndTrimEditText(fileText);
            }
            if (!page->markdown.empty()) {
                return DashboardResultProjectionStripMarkdown(page->markdown);
            }
            if (ReadUtf8FileToWide(page->markdownPath, fileText)) {
                return DashboardResultProjectionStripMarkdown(fileText);
            }
            return DashboardResultProjectionStripMarkdown(summary);
        case DashboardTextMode::Json:
            if (ReadUtf8FileToWide(page->contentJsonPath, fileText)) {
                return WideNormalizeAndTrimEditText(fileText);
            }
            return DashboardResultProjectionPdfPageJson(*job, *page);
        case DashboardTextMode::Preview:
        case DashboardTextMode::Source:
        default:
            if (!page->markdown.empty()) {
                return WideNormalizeAndTrimEditText(page->markdown);
            }
            if (ReadUtf8FileToWide(page->markdownPath, fileText)) {
                return WideNormalizeAndTrimEditText(fileText);
            }
            return summary;
        }
    }

    if (const auto* itemPtr = m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState))) {
        // D-C-S9: pure history-item display text; Host only supplies i18n missing-output message.
        const std::wstring missingOutput = S::IsChinese()
            ? L"Output 缺失或尚未载入。请打开原 Output 目录以恢复此历史记录。"
            : L"Output is missing or not loaded. Open the original Output folder to restore this history record.";
        return DashboardResultProjectionHistoryItemDisplayText(
            *itemPtr,
            DashboardStateSelectedHistoryIndex(m_dashboardState),
            DashboardStateTextModeEffective(m_dashboardState),
            missingOutput);
    }

    if (!m_edit) return L"";
    int len = GetWindowTextLengthW(m_edit);
    if (len <= 0) return L"";
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(m_edit, &text[0], len + 1);
    text.resize(len);
    return text;
}

std::wstring OcrDashboardWindow::GetCurrentPreviewSourceMarkdown() const {
    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        const DashboardBatchTaskItem* task = GetSelectedImageTask();
        if (task && task->status == BatchOcrTaskStatus::Completed &&
            !task->job.contentJsonPath.empty()) {
            std::wstring contentJson;
            if (ReadUtf8FileToWide(task->job.contentJsonPath, contentJson)) {
                // D-C-S9: pure markdown extract from content JSON.
                std::wstring markdown;
                if (DashboardResultProjectionTryExtractMarkdownFromContentJson(
                        contentJson, markdown)) {
                    return markdown;
                }
            }
        }
    }

    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
        const int previewPageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0
            ? DashboardStatePdfSelectionPageIndex(m_dashboardState)
            : 1;
        const BatchOcrPdfPageJob* page = job && previewPageIndex > 0
            ? DashboardFindPdfSelectionPage(*job, previewPageIndex)
            : nullptr;
        if (page) {
            if (!page->markdown.empty()) return page->markdown;
            if (!page->contentJsonPath.empty()) {
                std::wstring contentJson;
                if (ReadUtf8FileToWide(page->contentJsonPath, contentJson)) {
                    std::wstring markdown;
                    if (DashboardResultProjectionTryExtractMarkdownFromContentJson(
                            contentJson, markdown)) {
                        return markdown;
                    }
                }
            }
        }
    }

    return GetCurrentResultText();
}

std::wstring OcrDashboardWindow::GetCurrentPreviewMarkdown() const {
    std::wstring markdown = GetCurrentPreviewSourceMarkdown();
    std::wstring outputDir = GetCurrentPreviewAssetRoot();
    if (outputDir.empty()) return markdown;
    return RewritePreviewAssetImages(markdown, outputDir);
}

std::wstring OcrDashboardWindow::GetCurrentPreviewAssetRoot() const {
    std::wstring outputDir;

    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        const DashboardBatchTaskItem* task = GetSelectedImageTask();
        if (task) outputDir = task->job.outputDir;
    } else if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
        if (job) outputDir = job->outputDir;
    }

    return outputDir;
}

bool OcrDashboardWindow::DeleteCacheImageIfUnreferenced(const std::wstring& imagePath, int excludingIndex) {
    // D-C-S2: pure decision in DashboardHistoryCache; Window only DeleteFileW.
    if (!DashboardHistoryCacheShouldDeleteUnreferenced(
            m_history.model, imagePath, GetOcrImageDir(), excludingIndex)) {
        return false;
    }
    return DeleteFileW(imagePath.c_str()) != FALSE;
}

void OcrDashboardWindow::DeleteCacheImagesForItems(const std::vector<OcrDashboardHistoryItem>& items) {
    // D-C-S2: collect unreferenced under OCR dir via pure cache owner; Window deletes.
    const std::set<std::wstring> unreferenced = DashboardHistoryCacheCollectUnreferencedPaths(
        m_history.model, items, GetOcrImageDir());
    for (const auto& path : unreferenced) {
        DeleteFileW(path.c_str());
    }
}

void OcrDashboardWindow::DeleteHistoryItem(int index) {
    if (index < 0 || index >= (int)m_history.model.items.size()) return;
    // OWN-124: pure hash index label (WideStringUtils).
    std::wstring msg = S::IsChinese() ?
        (L"确定要删除 " + WideFormatHashIndex(index + 1) + L" 这条 OCR 历史记录及其缓存图片吗？") :
        (L"Delete OCR history " + WideFormatHashIndex(index + 1) + L" and its cached image?");
    if (MessageBoxW(m_hwnd, msg.c_str(), L"ZenCrop", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    if (!DeleteHistoryItemsByIndices({ index })) return;
    UpdateStatus(S::IsChinese() ? L"✓ 已删除记录" : L"✓ Record deleted");
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
}

bool OcrDashboardWindow::IsBatchOutputRootInUse(const std::wstring& outputRoot) const {
    // D-C-S8: Host collects candidate roots; pure path match in DashboardHistoryCache.
    if (outputRoot.empty()) return false;
    std::vector<std::wstring> candidates;
    candidates.reserve(
        m_batch.batchTasks.size() + m_batch.activePdfJobs.size() + m_batch.failedBatchJobs.size() +
        m_batch.failedPdfJobs.size() + m_batch.failedPdfPages.size() + m_batch.dropQueue.size() * 2);
    for (const auto& task : m_batch.batchTasks) {
        if (!task.job.outputRoot.empty()) candidates.push_back(task.job.outputRoot);
    }
    for (const auto& job : m_batch.activePdfJobs) {
        if (!job.outputRoot.empty()) candidates.push_back(job.outputRoot);
    }
    for (const auto& job : m_batch.failedBatchJobs) {
        if (!job.outputRoot.empty()) candidates.push_back(job.outputRoot);
    }
    for (const auto& job : m_batch.failedPdfJobs) {
        if (!job.outputRoot.empty()) candidates.push_back(job.outputRoot);
    }
    for (const auto& retry : m_batch.failedPdfPages) {
        if (!retry.job.outputRoot.empty()) candidates.push_back(retry.job.outputRoot);
    }
    for (const auto& queued : m_batch.dropQueue) {
        if (queued.hasBatchJob && !queued.batchJob.outputRoot.empty()) {
            candidates.push_back(queued.batchJob.outputRoot);
        }
        if (queued.hasPdfPageJob && !queued.pdfJob.outputRoot.empty()) {
            candidates.push_back(queued.pdfJob.outputRoot);
        }
    }
    return DashboardHistoryCacheOutputRootInUse(outputRoot, candidates);
}

void OcrDashboardWindow::ForgetBatchOutputRootIfUnused(const std::wstring& outputRoot) {
    if (outputRoot.empty() || IsBatchOutputRootInUse(outputRoot)) return;

    // D-B-3: sole write authority is DashboardState batch output roots.
    std::wstring preferred = DashboardStatePreferredBatchOutputRoot(m_dashboardState);
    std::wstring last = DashboardStateLastBatchOutputRoot(m_dashboardState);
    std::vector<std::wstring> recent = DashboardStateRecentBatchOutputRoots(m_dashboardState);
    recent.erase(
        std::remove_if(recent.begin(), recent.end(),
            [&](const std::wstring& existing) {
                return SameDashboardPath(existing, outputRoot);
            }),
        recent.end());

    if (SameDashboardPath(last, outputRoot)) {
        last = recent.empty() ? L"" : recent.front();
    }
    DashboardStateApplyBatchOutputRoots(
        m_dashboardState,
        std::move(preferred),
        std::move(last),
        std::move(recent));
}

bool OcrDashboardWindow::DeleteSelectedBatchSource(bool skipConfirm) {
    std::vector<DashboardSourceRailSelectableRow> selectedRows = GetSelectedBatchRows();
    if (selectedRows.empty()) {
        return false;
    }

    auto denyRunningDelete = [&]() {
        UpdateStatus(S::IsChinese()
            ? L"该批量记录仍在运行，请先取消或等待完成"
            : L"This batch record is still running; cancel or wait before deleting it");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
        return true;
    };

    auto denyPageOnlyDelete = [&]() {
        UpdateStatus(S::IsChinese()
            ? L"PDF 页面不能单独从 Dashboard 移除；请选择 PDF 根项"
            : L"PDF pages cannot be removed separately; select the PDF root");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2800, nullptr);
        return true;
    };

    auto imageJobActiveOrQueued = [&](const BatchOcrImageJob& job, BatchOcrTaskStatus status) {
        if (DashboardStatusIsRunning(status)) return true;
        return std::any_of(m_batch.dropQueue.begin(), m_batch.dropQueue.end(),
            [&](const DashboardQueuedOcr& queued) {
                return (queued.hasImageTask && SameDashboardImageJobKey(queued.imageTaskJob, job)) ||
                    (queued.hasBatchJob && SameDashboardImageJobKey(queued.batchJob, job));
            });
    };

    auto confirmDelete = [&](const std::wstring& message) {
        if (skipConfirm) return true;
        bool confirmed = true;
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
        confirmed = m_testAutoConfirmDelete;
#else
        confirmed = false;
#endif
        return confirmed ||
            MessageBoxW(m_hwnd, message.c_str(), L"ZenCrop", MB_YESNO | MB_ICONQUESTION) == IDYES;
    };

    auto persistDismissals = [&](const std::vector<std::wstring>& manifestKeys) {
        if (DashboardHistorySessionDismissKeys(m_history, m_dashboardState, manifestKeys)) return true;
        UpdateStatus(S::IsChinese()
            ? L"删除失败：无法安全保存 Dashboard 删除记录"
            : L"Remove failed: Dashboard removal metadata could not be saved safely");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3200, nullptr);
        return false;
    };

    auto finishDelete = [&](const std::wstring& outputRoot, const std::wstring& status) {
        ForgetBatchOutputRootIfUnused(outputRoot);
        ClearImageTaskSelection();
        ClearPdfSelection();
        SetSelectedHistoryIndex(-1);
        DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
        // D-D-3: history multi-select sole on DashboardState.
        DashboardStateClearSelectedSourceKeys(m_dashboardState);
        DashboardStateClearSelectedSourceAnchor(m_dashboardState);
        if (!DashboardStateIsOcrBusy(m_dashboardState) && DashboardStatePdfRenderInFlight(m_dashboardState) <= 0 && m_batch.dropQueue.empty()) {
        DashboardStateSetBatchPaused(m_dashboardState, false);
    DashboardStateSyncBatchProgress(m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
        }
        LoadImageIntoCanvas(L"", false);
        RebuildHistoryText(false);
        RenderSelectedItemPreview();
        UpdatePreviewControls();
        RefreshSourceRailBatchSection();
        LayoutControls();
        UpdateRetryFailedButton();
        UpdateCloseCancelButtonText();
        if (m_openOutputBtn) {
            EnableWindow(m_openOutputBtn, !GetCurrentOutputFolder().empty());
        }
        SaveBatchSessionState();
        UpdateStatus(status);
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2200, nullptr);
        return true;
    };

    if (selectedRows.size() > 1 || (DashboardStateHasNoTaskSelection(m_dashboardState))) {
        struct PdfPageDelete {
            BatchOcrPdfJob job;
            int pageIndex = 0;
        };

        std::vector<BatchOcrImageJob> imageJobs;
        std::vector<BatchOcrPdfJob> pdfJobs;
        std::vector<PdfPageDelete> pdfPages;
        std::vector<std::wstring> outputRoots;

        auto addOutputRoot = [&](const std::wstring& outputRoot) {
            if (outputRoot.empty()) return;
            if (std::find_if(outputRoots.begin(), outputRoots.end(),
                    [&](const std::wstring& existing) {
                        return SameDashboardPath(existing, outputRoot);
                    }) == outputRoots.end()) {
                outputRoots.push_back(outputRoot);
            }
        };
        auto hasImageJob = [&](const BatchOcrImageJob& job) {
            return std::find_if(imageJobs.begin(), imageJobs.end(),
                [&](const BatchOcrImageJob& existing) {
                    return SameDashboardImageJobKey(existing, job);
                }) != imageJobs.end();
        };
        auto hasPdfJob = [&](const std::vector<BatchOcrPdfJob>& jobs, const BatchOcrPdfJob& job) {
            return std::find_if(jobs.begin(), jobs.end(),
                [&](const BatchOcrPdfJob& existing) {
                    return SameDashboardPdfJobKey(existing, job);
                }) != jobs.end();
        };
        auto hasPdfPage = [&](const BatchOcrPdfJob& job, int pageIndex) {
            return std::find_if(pdfPages.begin(), pdfPages.end(),
                [&](const PdfPageDelete& existing) {
                    return existing.pageIndex == pageIndex &&
                        SameDashboardPdfJobKey(existing.job, job);
                }) != pdfPages.end();
        };
        auto pdfRowRunning = [&](const BatchOcrPdfJob& job, int pageIndex) {
            bool queued = std::any_of(m_batch.dropQueue.begin(), m_batch.dropQueue.end(),
                [&](const DashboardQueuedOcr& candidate) {
                    return candidate.hasPdfPageJob &&
                        SameDashboardPdfJobKey(candidate.pdfJob, job) &&
                        (pageIndex <= 0 || candidate.pdfPage.pageIndex == pageIndex);
                });
            if (queued) return true;
            if (pageIndex <= 0 && std::any_of(m_batch.pdfRenderPending.begin(), m_batch.pdfRenderPending.end(),
                    [&](const DashboardPendingPdfRender& pending) {
                        return SameDashboardPdfJobKey(pending.job, job);
                    })) {
                return true;
            }
            if (pageIndex > 0) {
                auto pageIt = std::find_if(job.pages.begin(), job.pages.end(),
                    [&](const BatchOcrPdfPageJob& page) {
                        return page.pageIndex == pageIndex;
                    });
                return pageIt != job.pages.end() && DashboardStatusIsRunning(pageIt->status);
            }
            return DashboardStatusIsRunning(job.status) ||
                std::any_of(job.pages.begin(), job.pages.end(),
                    [](const BatchOcrPdfPageJob& page) {
                        return DashboardStatusIsRunning(page.status);
                    }) ||
                (DashboardStatePdfRenderInFlight(m_dashboardState) > 0 && job.pages.empty() && job.status == BatchOcrTaskStatus::Pending);
        };

        for (const auto& row : selectedRows) {
            switch (row.kind) {
            case DashboardSourceRailRowKind::ImageTask:
                if (row.imageTaskIndex < 0 || row.imageTaskIndex >= (int)m_batch.batchTasks.size()) break;
                if (imageJobActiveOrQueued(
                        m_batch.batchTasks[(size_t)row.imageTaskIndex].job,
                        m_batch.batchTasks[(size_t)row.imageTaskIndex].status)) {
                    return denyRunningDelete();
                }
                if (!hasImageJob(m_batch.batchTasks[(size_t)row.imageTaskIndex].job)) {
                    imageJobs.push_back(m_batch.batchTasks[(size_t)row.imageTaskIndex].job);
                    addOutputRoot(m_batch.batchTasks[(size_t)row.imageTaskIndex].job.outputRoot);
                }
                break;
            case DashboardSourceRailRowKind::PdfJob:
                if (row.pdfJobIndex < 0 || row.pdfJobIndex >= (int)m_batch.activePdfJobs.size()) break;
                if (pdfRowRunning(m_batch.activePdfJobs[(size_t)row.pdfJobIndex], 0)) {
                    return denyRunningDelete();
                }
                if (!hasPdfJob(pdfJobs, m_batch.activePdfJobs[(size_t)row.pdfJobIndex])) {
                    pdfJobs.push_back(m_batch.activePdfJobs[(size_t)row.pdfJobIndex]);
                    addOutputRoot(m_batch.activePdfJobs[(size_t)row.pdfJobIndex].outputRoot);
                }
                break;
            case DashboardSourceRailRowKind::PdfPage:
                if (row.pdfJobIndex < 0 || row.pdfJobIndex >= (int)m_batch.activePdfJobs.size()) break;
                if (!DashboardFindPdfSelectionPage(m_batch.activePdfJobs[(size_t)row.pdfJobIndex], row.pageIndex)) break;
                if (pdfRowRunning(m_batch.activePdfJobs[(size_t)row.pdfJobIndex], row.pageIndex)) {
                    return denyRunningDelete();
                }
                if (!hasPdfPage(m_batch.activePdfJobs[(size_t)row.pdfJobIndex], row.pageIndex)) {
                    PdfPageDelete pageDelete;
                    pageDelete.job = m_batch.activePdfJobs[(size_t)row.pdfJobIndex];
                    pageDelete.pageIndex = row.pageIndex;
                    pdfPages.push_back(pageDelete);
                    addOutputRoot(m_batch.activePdfJobs[(size_t)row.pdfJobIndex].outputRoot);
                }
                break;
            default:
                break;
            }
        }

        pdfPages.erase(
            std::remove_if(pdfPages.begin(), pdfPages.end(),
                [&](const PdfPageDelete& pageDelete) {
                    return hasPdfJob(pdfJobs, pageDelete.job);
                }),
            pdfPages.end());

        if (!pdfPages.empty()) {
            return denyPageOnlyDelete();
        }

        size_t deleteCount = imageJobs.size() + pdfJobs.size() + pdfPages.size();
        if (deleteCount == 0) {
            SetBatchSelectionRows({});
            RefreshSourceRailBatchSection();
            return true;
        }

        // OWN-124: pure int labels (WideStringUtils).
        std::wstring msg = deleteCount == 1
            ? (S::IsChinese()
                ? L"从 Dashboard 删除选中的批量记录？磁盘上的输出文件会保留。"
                : L"Remove the selected batch record from Dashboard? Output files on disk will be kept.")
            : (S::IsChinese()
                ? (L"从 Dashboard 删除选中的 " + WideFormatIntLabel(deleteCount) + L" 个批量记录？磁盘上的输出文件会保留。")
                : (L"Remove " + WideFormatIntLabel(deleteCount) +
                    L" selected batch records from Dashboard? Output files on disk will be kept."));
        if (!confirmDelete(msg)) return true;

        std::vector<std::wstring> dismissedManifests;
        dismissedManifests.reserve(imageJobs.size() + pdfJobs.size());
        for (const auto& job : imageJobs) {
            dismissedManifests.push_back(DashboardHistoryBuildImageDismissalKey(
                job.manifestPath, job.sourceInstanceId, job.createdAt, job.sourcePath));
        }
        for (const auto& job : pdfJobs) {
            dismissedManifests.push_back(DashboardHistoryBuildPdfDismissalKey(
                job.manifestPath, job.createdAt, job.sourcePath));
        }
        if (!persistDismissals(dismissedManifests)) return true;

        for (const auto& job : imageJobs) {
            m_batch.batchTasks.erase(
                std::remove_if(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
                    [&](const DashboardBatchTaskItem& task) {
                        return SameDashboardImageJobKey(task.job, job);
                    }),
                m_batch.batchTasks.end());
            m_batch.failedBatchJobs.erase(
                std::remove_if(m_batch.failedBatchJobs.begin(), m_batch.failedBatchJobs.end(),
                    [&](const BatchOcrImageJob& failed) {
                        return SameDashboardImageJobKey(failed, job);
                    }),
                m_batch.failedBatchJobs.end());
        }

        for (const auto& job : pdfJobs) {
            m_batch.activePdfJobs.erase(
                std::remove_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
                    [&](const BatchOcrPdfJob& existing) {
                        return SameDashboardPdfJobKey(existing, job);
                    }),
                m_batch.activePdfJobs.end());
            m_batch.failedPdfJobs.erase(
                std::remove_if(m_batch.failedPdfJobs.begin(), m_batch.failedPdfJobs.end(),
                    [&](const BatchOcrPdfJob& failed) {
                        return SameDashboardPdfJobKey(failed, job);
                    }),
                m_batch.failedPdfJobs.end());
            EraseDashboardPdfKey(m_dashboardState.expandedPdfJobKeys, DashboardPdfJobTreeKey(job));
            EraseDashboardPdfKey(m_dashboardState.pausedPdfJobKeys, DashboardPdfJobTreeKey(job));
            EraseDashboardPdfPageKeysForJob(m_dashboardState.pausedPdfPageKeys, job);
        }

        std::vector<BatchOcrPdfJob> emptiedPdfJobs;
        for (const auto& pageDelete : pdfPages) {
            auto jobIt = std::find_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
                [&](const BatchOcrPdfJob& existing) {
                    return SameDashboardPdfJobKey(existing, pageDelete.job);
                });
            if (jobIt == m_batch.activePdfJobs.end()) continue;

            jobIt->pages.erase(
                std::remove_if(jobIt->pages.begin(), jobIt->pages.end(),
                    [&](const BatchOcrPdfPageJob& page) {
                        return page.pageIndex == pageDelete.pageIndex;
                    }),
                jobIt->pages.end());
            if (jobIt->pages.empty()) {
                emptiedPdfJobs.push_back(pageDelete.job);
                m_batch.activePdfJobs.erase(jobIt);
                m_batch.failedPdfJobs.erase(
                    std::remove_if(m_batch.failedPdfJobs.begin(), m_batch.failedPdfJobs.end(),
                        [&](const BatchOcrPdfJob& failed) {
                            return SameDashboardPdfJobKey(failed, pageDelete.job);
                        }),
                    m_batch.failedPdfJobs.end());
                EraseDashboardPdfKey(m_dashboardState.expandedPdfJobKeys, DashboardPdfJobTreeKey(pageDelete.job));
                EraseDashboardPdfKey(m_dashboardState.pausedPdfJobKeys, DashboardPdfJobTreeKey(pageDelete.job));
                EraseDashboardPdfPageKeysForJob(m_dashboardState.pausedPdfPageKeys, pageDelete.job);
            } else {
                EraseDashboardPdfKey(
                    m_dashboardState.pausedPdfPageKeys,
                    DashboardHistoryPdfPagePauseKey(pageDelete.job, pageDelete.pageIndex));
            }
        }
        m_batch.failedPdfPages.erase(
            std::remove_if(m_batch.failedPdfPages.begin(), m_batch.failedPdfPages.end(),
                [&](const DashboardPdfRetryPage& failed) {
                    if (hasPdfJob(pdfJobs, failed.job) || hasPdfJob(emptiedPdfJobs, failed.job)) {
                        return true;
                    }
                    return std::find_if(pdfPages.begin(), pdfPages.end(),
                        [&](const PdfPageDelete& pageDelete) {
                            return pageDelete.pageIndex == failed.page.pageIndex &&
                                SameDashboardPdfJobKey(pageDelete.job, failed.job);
                        }) != pdfPages.end();
                }),
            m_batch.failedPdfPages.end());

        size_t oldQueueCount = m_batch.dropQueue.size();
        m_batch.dropQueue.erase(
            std::remove_if(m_batch.dropQueue.begin(), m_batch.dropQueue.end(),
                [&](const DashboardQueuedOcr& queued) {
                    if ((queued.hasBatchJob && hasImageJob(queued.batchJob)) ||
                        (queued.hasImageTask && hasImageJob(queued.imageTaskJob))) {
                        return true;
                    }
                    if (!queued.hasPdfPageJob) return false;
                    if (hasPdfJob(pdfJobs, queued.pdfJob) ||
                        hasPdfJob(emptiedPdfJobs, queued.pdfJob)) {
                        return true;
                    }
                    return std::find_if(pdfPages.begin(), pdfPages.end(),
                        [&](const PdfPageDelete& pageDelete) {
                            return pageDelete.pageIndex == queued.pdfPage.pageIndex &&
                                SameDashboardPdfJobKey(pageDelete.job, queued.pdfJob);
                        }) != pdfPages.end();
                }),
            m_batch.dropQueue.end());
        size_t removedQueued = oldQueueCount - m_batch.dropQueue.size();
        if (removedQueued > 0) {
            {
                int dropDone = DashboardStateDropDone(m_dashboardState);
                int dropTotal = max(dropDone, DashboardStateDropTotal(m_dashboardState) - (int)removedQueued);
                DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), dropTotal, dropDone, DashboardStatePdfRenderInFlight(m_dashboardState));
            }
        }

        for (const auto& outputRoot : outputRoots) {
            ForgetBatchOutputRootIfUnused(outputRoot);
        }
        // OWN-124: pure int labels (WideStringUtils).
        std::wstring status = deleteCount == 1
            ? (S::IsChinese()
                ? L"已从 Dashboard 删除批量记录"
                : L"Removed batch record from Dashboard")
            : (S::IsChinese()
                ? (L"已从 Dashboard 删除 " + WideFormatIntLabel(deleteCount) + L" 个批量记录")
                : (L"Removed " + WideFormatIntLabel(deleteCount) + L" batch records from Dashboard"));
        return finishDelete(L"", status);
    }

    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        const DashboardBatchTaskItem* selectedTask = GetSelectedImageTask();
        if (!selectedTask) {
            ClearImageTaskSelection();
            RefreshSourceRailBatchSection();
            return true;
        }
        if (imageJobActiveOrQueued(selectedTask->job, selectedTask->status)) {
            return denyRunningDelete();
        }

        BatchOcrImageJob job = selectedTask->job;
        std::wstring msg = S::IsChinese()
            ? L"从 Dashboard 删除这个批量图片记录？磁盘上的输出文件会保留。"
            : L"Remove this image batch record from Dashboard? Output files on disk will be kept.";
        if (!confirmDelete(msg)) return true;
        if (!persistDismissals({DashboardHistoryBuildImageDismissalKey(
                job.manifestPath, job.sourceInstanceId, job.createdAt, job.sourcePath)})) return true;

        size_t oldTaskCount = m_batch.batchTasks.size();
        m_batch.batchTasks.erase(
            std::remove_if(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
                [&](const DashboardBatchTaskItem& task) {
                    return SameDashboardImageJobKey(task.job, job);
                }),
            m_batch.batchTasks.end());
        m_batch.failedBatchJobs.erase(
            std::remove_if(m_batch.failedBatchJobs.begin(), m_batch.failedBatchJobs.end(),
                [&](const BatchOcrImageJob& failed) {
                    return SameDashboardImageJobKey(failed, job);
                }),
            m_batch.failedBatchJobs.end());

        size_t oldQueueCount = m_batch.dropQueue.size();
        m_batch.dropQueue.erase(
            std::remove_if(m_batch.dropQueue.begin(), m_batch.dropQueue.end(),
                [&](const DashboardQueuedOcr& queued) {
                return (queued.hasBatchJob && SameDashboardImageJobKey(queued.batchJob, job)) ||
                    (queued.hasImageTask && SameDashboardImageJobKey(queued.imageTaskJob, job));
                }),
            m_batch.dropQueue.end());
        size_t removedQueued = oldQueueCount - m_batch.dropQueue.size();
        if (removedQueued > 0) {
            {
                int dropDone = DashboardStateDropDone(m_dashboardState);
                int dropTotal = max(dropDone, DashboardStateDropTotal(m_dashboardState) - (int)removedQueued);
                DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), dropTotal, dropDone, DashboardStatePdfRenderInFlight(m_dashboardState));
            }
        }

        bool removed = oldTaskCount != m_batch.batchTasks.size() || removedQueued > 0;
        return finishDelete(
            job.outputRoot,
            removed
                ? (S::IsChinese() ? L"✓ 已从 Dashboard 删除批量图片记录" : L"✓ Removed image batch record from Dashboard")
                : (S::IsChinese() ? L"该批量图片记录已经不在列表中" : L"That image batch record is no longer in the list"));
    }

    DashboardPdfSelectionKey key;
    key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
    key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
    key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
    key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);

    auto jobIt = std::find_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
        [&](const BatchOcrPdfJob& job) {
            return DashboardSamePdfSelectionKey(job, key);
        });
    if (jobIt == m_batch.activePdfJobs.end()) {
        ClearPdfSelection();
        RefreshSourceRailBatchSection();
        return true;
    }

    BatchOcrPdfJob job = *jobIt;
    int pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
    if (pageIndex > 0) {
        return denyPageOnlyDelete();
    }
    bool running = false;
    if (pageIndex > 0) {
        auto pageIt = std::find_if(job.pages.begin(), job.pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == pageIndex;
            });
        running = pageIt != job.pages.end() && DashboardStatusIsRunning(pageIt->status);
    } else {
        running = DashboardStatusIsRunning(job.status) ||
            std::any_of(job.pages.begin(), job.pages.end(),
                [](const BatchOcrPdfPageJob& page) {
                    return DashboardStatusIsRunning(page.status);
                }) ||
            (DashboardStatePdfRenderInFlight(m_dashboardState) > 0 && job.pages.empty() && job.status == BatchOcrTaskStatus::Pending);
    }
    if (!running) {
        running = std::any_of(m_batch.dropQueue.begin(), m_batch.dropQueue.end(),
            [&](const DashboardQueuedOcr& candidate) {
                return candidate.hasPdfPageJob && SameDashboardPdfJobKey(candidate.pdfJob, job) &&
                    (pageIndex <= 0 || candidate.pdfPage.pageIndex == pageIndex);
            });
    }
    if (!running && pageIndex <= 0) {
        running = std::any_of(m_batch.pdfRenderPending.begin(), m_batch.pdfRenderPending.end(),
            [&](const DashboardPendingPdfRender& pending) {
                return SameDashboardPdfJobKey(pending.job, job);
            });
    }
    if (running) {
        return denyRunningDelete();
    }

    std::wstring msg = S::IsChinese()
        ? (pageIndex > 0
            ? L"从 Dashboard 删除这个 PDF 页面记录？磁盘上的输出文件会保留。"
            : L"从 Dashboard 删除这个 PDF 批量记录？磁盘上的输出文件会保留。")
        : (pageIndex > 0
            ? L"Remove this PDF page record from Dashboard? Output files on disk will be kept."
            : L"Remove this PDF batch record from Dashboard? Output files on disk will be kept.");
    if (!confirmDelete(msg)) return true;
    if (!persistDismissals({DashboardHistoryBuildPdfDismissalKey(
            job.manifestPath, job.createdAt, job.sourcePath)})) return true;

    bool deleteWholeJob = pageIndex <= 0;
    if (deleteWholeJob) {
        m_batch.activePdfJobs.erase(
            std::remove_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
                [&](const BatchOcrPdfJob& existing) {
                    return SameDashboardPdfJobKey(existing, job);
                }),
            m_batch.activePdfJobs.end());
        m_batch.failedPdfJobs.erase(
            std::remove_if(m_batch.failedPdfJobs.begin(), m_batch.failedPdfJobs.end(),
                [&](const BatchOcrPdfJob& failed) {
                    return SameDashboardPdfJobKey(failed, job);
                }),
            m_batch.failedPdfJobs.end());
        EraseDashboardPdfKey(m_dashboardState.expandedPdfJobKeys, DashboardPdfJobTreeKey(job));
        EraseDashboardPdfKey(m_dashboardState.pausedPdfJobKeys, DashboardPdfJobTreeKey(job));
        EraseDashboardPdfPageKeysForJob(m_dashboardState.pausedPdfPageKeys, job);
    } else {
        jobIt->pages.erase(
            std::remove_if(jobIt->pages.begin(), jobIt->pages.end(),
                [&](const BatchOcrPdfPageJob& page) {
                    return page.pageIndex == pageIndex;
                }),
            jobIt->pages.end());
        deleteWholeJob = jobIt->pages.empty();
        if (deleteWholeJob) {
            m_batch.activePdfJobs.erase(jobIt);
            m_batch.failedPdfJobs.erase(
                std::remove_if(m_batch.failedPdfJobs.begin(), m_batch.failedPdfJobs.end(),
                    [&](const BatchOcrPdfJob& failed) {
                        return SameDashboardPdfJobKey(failed, job);
                    }),
                m_batch.failedPdfJobs.end());
            EraseDashboardPdfKey(m_dashboardState.expandedPdfJobKeys, DashboardPdfJobTreeKey(job));
            EraseDashboardPdfKey(m_dashboardState.pausedPdfJobKeys, DashboardPdfJobTreeKey(job));
            EraseDashboardPdfPageKeysForJob(m_dashboardState.pausedPdfPageKeys, job);
        } else {
            EraseDashboardPdfKey(m_dashboardState.pausedPdfPageKeys, DashboardHistoryPdfPagePauseKey(job, pageIndex));
        }
    }
    m_batch.failedPdfPages.erase(
        std::remove_if(m_batch.failedPdfPages.begin(), m_batch.failedPdfPages.end(),
            [&](const DashboardPdfRetryPage& failed) {
                return SameDashboardPdfJobKey(failed.job, job) &&
                    (pageIndex <= 0 || failed.page.pageIndex == pageIndex);
            }),
        m_batch.failedPdfPages.end());

    size_t oldQueueCount = m_batch.dropQueue.size();
    m_batch.dropQueue.erase(
        std::remove_if(m_batch.dropQueue.begin(), m_batch.dropQueue.end(),
            [&](const DashboardQueuedOcr& queued) {
                return queued.hasPdfPageJob &&
                    SameDashboardPdfJobKey(queued.pdfJob, job) &&
                    (pageIndex <= 0 || queued.pdfPage.pageIndex == pageIndex);
            }),
        m_batch.dropQueue.end());
    size_t removedQueued = oldQueueCount - m_batch.dropQueue.size();
    if (removedQueued > 0) {
            {
                int dropDone = DashboardStateDropDone(m_dashboardState);
                int dropTotal = max(dropDone, DashboardStateDropTotal(m_dashboardState) - (int)removedQueued);
                DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), dropTotal, dropDone, DashboardStatePdfRenderInFlight(m_dashboardState));
            }
    }

    return finishDelete(
        job.outputRoot,
        pageIndex > 0
            ? (S::IsChinese() ? L"✓ 已从 Dashboard 删除 PDF 页面记录" : L"✓ Removed PDF page record from Dashboard")
            : (S::IsChinese() ? L"✓ 已从 Dashboard 删除 PDF 批量记录" : L"✓ Removed PDF batch record from Dashboard"));
}

void OcrDashboardWindow::DeleteSelectedSources() {
    std::vector<DashboardSourceRailSelectableRow> batchRows = GetSelectedBatchRows();
    std::vector<int> indices = DashboardHistorySelectedIndices(
        m_history.model.items,
        DashboardStateSelectedSourceKeys(m_dashboardState),
        DashboardStateSelectedHistoryIndex(m_dashboardState));
    const auto projection = BuildDashboardSourceProjection(
        m_batch.batchTasks, m_batch.activePdfJobs, m_history.model.items);
    for (const auto& row : batchRows) {
        if (row.kind != DashboardSourceRailRowKind::ImageTask ||
            row.imageTaskIndex < 0 || row.imageTaskIndex >= (int)m_batch.batchTasks.size()) {
            continue;
        }
        int linkedHistoryIndex = row.linkedHistoryIndex;
        if (linkedHistoryIndex < 0) {
            auto source = std::find_if(projection.begin(), projection.end(),
                [&](const DashboardSourceProjectionEntry& entry) {
                    return entry.refs.imageTaskIndex == row.imageTaskIndex;
                });
            if (source != projection.end()) linkedHistoryIndex = source->refs.historyIndex;
        }
        if (linkedHistoryIndex >= 0) indices.push_back(linkedHistoryIndex);
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    if (!batchRows.empty() && !indices.empty()) {
        auto batchTasksSnapshot = m_batch.batchTasks;
        auto failedBatchSnapshot = m_batch.failedBatchJobs;
        auto activePdfSnapshot = m_batch.activePdfJobs;
        auto failedPdfSnapshot = m_batch.failedPdfJobs;
        auto failedPdfPagesSnapshot = m_batch.failedPdfPages;
        auto dropQueueSnapshot = m_batch.dropQueue;
        auto expandedPdfSnapshot = m_dashboardState.expandedPdfJobKeys;
        auto pausedPdfSnapshot = m_dashboardState.pausedPdfJobKeys;
        auto pausedPdfPagesSnapshot = m_dashboardState.pausedPdfPageKeys;
        auto recentRootsSnapshot = DashboardStateRecentBatchOutputRoots(m_dashboardState);
        std::wstring lastRootSnapshot = DashboardStateLastBatchOutputRoot(m_dashboardState);
        std::wstring preferredRootSnapshot = DashboardStatePreferredBatchOutputRoot(m_dashboardState);
        // D-C-5: snapshot pure dismissed keys for rollback.
        auto dismissedManifestsSnapshot =
            DashboardStateDismissedBatchManifestKeys(m_dashboardState);
        size_t deleteCount = batchRows.size() + indices.size();
        // OWN-124: pure int labels (WideStringUtils).
        std::wstring msg = S::IsChinese()
            ? (L"确定要从 Dashboard 删除选中的 " + WideFormatIntLabel((int)deleteCount) + L" 个 Source Rail 项目吗？")
            : (L"Delete " + WideFormatIntLabel((int)deleteCount) + L" selected Source Rail items?");
        bool confirmed = true;
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
        confirmed = m_testAutoConfirmDelete;
#else
        confirmed = false;
#endif
        if (!confirmed &&
            MessageBoxW(m_hwnd, msg.c_str(), L"ZenCrop", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }

        DeleteSelectedBatchSource(true);
        if (!GetSelectedBatchRows().empty()) {
            return;
        }
        if (!DeleteHistoryItemsByIndices(indices)) {
            m_batch.batchTasks = std::move(batchTasksSnapshot);
            m_batch.failedBatchJobs = std::move(failedBatchSnapshot);
            m_batch.activePdfJobs = std::move(activePdfSnapshot);
            m_batch.failedPdfJobs = std::move(failedPdfSnapshot);
            m_batch.failedPdfPages = std::move(failedPdfPagesSnapshot);
            m_batch.dropQueue = std::move(dropQueueSnapshot);
            m_dashboardState.expandedPdfJobKeys = std::move(expandedPdfSnapshot);
            m_dashboardState.pausedPdfJobKeys = std::move(pausedPdfSnapshot);
            m_dashboardState.pausedPdfPageKeys = std::move(pausedPdfPagesSnapshot);
            DashboardStateApplyBatchOutputRoots(
                m_dashboardState,
                preferredRootSnapshot,
                std::move(lastRootSnapshot),
                std::move(recentRootsSnapshot));
            if (DashboardStateDismissedBatchManifestKeys(m_dashboardState) !=
                dismissedManifestsSnapshot) {
                DashboardStateSetDismissedBatchManifestKeys(
                    m_dashboardState, std::move(dismissedManifestsSnapshot));
                DashboardHistorySessionSaveDismissed(m_history, m_dashboardState);
            }
            ApplyFilter(DashboardStateFilterText(m_dashboardState));
            if (!batchRows.empty()) {
                ActivateSourceRailSelectableRowAfterSelection(batchRows.back());
            }
            SetSourceRailSelectionRows(batchRows);
            SaveBatchSessionState();
            RefreshSourceRailBatchSection();
            return;
        }
        // OWN-124: pure int labels (WideStringUtils).
        UpdateStatus(S::IsChinese() ?
            (L"✓ 已删除 " + WideFormatIntLabel((int)deleteCount) + L" 个 Source Rail 项目") :
            (L"✓ Deleted " + WideFormatIntLabel((int)deleteCount) + L" Source Rail items"));
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return;
    }

    if (!batchRows.empty()) {
        DeleteSelectedBatchSource();
        return;
    }

    if (indices.empty()) return;

    std::wstring msg;
    if (indices.size() == 1) {
        msg = S::IsChinese() ?
            (L"确定要删除这条 OCR 记录及其缓存图片吗？") :
            L"Delete this OCR record and its cached image?";
    } else {
        // OWN-124: pure int labels (WideStringUtils).
        msg = S::IsChinese() ?
            (L"确定要删除 " + WideFormatIntLabel((int)indices.size()) + L" 条 OCR 记录及其缓存图片吗？") :
            (L"Delete " + WideFormatIntLabel((int)indices.size()) + L" OCR records and their cached images?");
    }
    bool confirmed = true;
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    confirmed = m_testAutoConfirmDelete;
#else
    confirmed = false;
#endif
    if (!confirmed &&
        MessageBoxW(m_hwnd, msg.c_str(), L"ZenCrop", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    size_t deletedCount = indices.size();
    if (!DeleteHistoryItemsByIndices(indices)) return;
    // OWN-124: pure int labels (WideStringUtils).
    UpdateStatus(S::IsChinese() ?
        (L"✓ 已删除 " + WideFormatIntLabel((int)deletedCount) + L" 条记录") :
        (L"✓ Deleted " + WideFormatIntLabel((int)deletedCount) + L" records"));
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
}

bool OcrDashboardWindow::DeleteHistoryItemsByIndices(std::vector<int> indices) {
    if (indices.empty()) return true;
    indices.erase(std::remove_if(indices.begin(), indices.end(), [this](int index) {
        return index < 0 || index >= (int)m_history.model.items.size();
    }), indices.end());
    if (indices.empty()) return true;

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::vector<OcrDashboardHistoryItem> oldHistoryItems = m_history.model.items;
    int oldSelectedHistoryIndex = DashboardStateSelectedHistoryIndex(m_dashboardState);

    auto indexWillBeDeleted = [&](int index) {
        return std::binary_search(indices.begin(), indices.end(), index);
    };
    auto manifestHasRetainedBacking = [&](const std::wstring& manifestPath) {
        if (manifestPath.empty()) return false;
        if (std::any_of(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
                [&](const DashboardBatchTaskItem& task) {
                    return SameDashboardPath(task.job.manifestPath, manifestPath);
                })) {
            return true;
        }
        if (std::any_of(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
                [&](const BatchOcrPdfJob& job) {
                    return SameDashboardPath(job.manifestPath, manifestPath);
                })) {
            return true;
        }
        for (int i = 0; i < static_cast<int>(m_history.model.items.size()); ++i) {
            if (indexWillBeDeleted(i)) continue;
            const auto* itemPtr = m_history.model.itemAt(i);
            if (itemPtr != nullptr &&
                SameDashboardPath(itemPtr->originManifestPath, manifestPath)) {
                return true;
            }
        }
        return false;
    };

    std::vector<std::wstring> dismissedManifests;
    for (int index : indices) {
        const auto* itemPtr = m_history.model.itemAt(index);
        if (itemPtr == nullptr) continue;
        const std::wstring& manifestPath = itemPtr->originManifestPath;
        if (!manifestPath.empty() && !manifestHasRetainedBacking(manifestPath)) {
            dismissedManifests.push_back(DashboardHistoryBuildHistoryItemDismissalKey(
                itemPtr->originManifestPath, itemPtr->sourceInstanceId));
        }
    }
    // D-C-5: snapshot pure dismissed keys for rollback on SaveHistory failure.
    std::vector<std::wstring> dismissedManifestSnapshot =
        DashboardStateDismissedBatchManifestKeys(m_dashboardState);
    if (!DashboardHistorySessionDismissKeys(m_history, m_dashboardState, dismissedManifests)) {
        UpdateStatus(S::IsChinese()
            ? L"删除失败：无法安全保存 Dashboard 删除记录"
            : L"Delete failed: Dashboard removal metadata could not be saved safely");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3200, nullptr);
        return false;
    }

    // 关键修复：若当前选中项在待删列表中，必须先释放 GDI+ 持有的文件句柄，
    // 否则后续 DeleteFileW 会因 GDI+ 的 FILE_SHARE_READ（不含 FILE_SHARE_DELETE）
    // 句柄而静默失败（GetLastError=32, ERROR_SHARING_VIOLATION）。
    // SelectHistoryItem 在流程末尾才会重新加载，提前释放不会泄漏。
    if (DashboardStateSelectedHistoryIndex(m_dashboardState) >= 0 &&
        std::binary_search(indices.begin(), indices.end(), DashboardStateSelectedHistoryIndex(m_dashboardState))) {
        ReleaseGdiplusImages();
    }

    int nextVisiblePos = 0;
    for (size_t i = 0; i < DashboardStateVisibleHistoryIndices(m_dashboardState).size(); i++) {
        if (std::binary_search(indices.begin(), indices.end(), DashboardStateVisibleHistoryIndices(m_dashboardState)[i])) {
            nextVisiblePos = (int)i;
            break;
        }
        nextVisiblePos = (int)i;
    }

    // D-C-S2: snapshot removed items for pure cache ownership; delete after save.
    std::vector<OcrDashboardHistoryItem> removedItems;
    removedItems.reserve(indices.size());
    for (int index : indices) {
        const auto* itemPtr = m_history.model.itemAt(index);
        if (itemPtr != nullptr) removedItems.push_back(*itemPtr);
    }

    std::sort(indices.rbegin(), indices.rend());
    for (int index : indices) {
        m_history.model.items.erase(m_history.model.items.begin() + index);
    }

    SetSelectedHistoryIndex(-1);
    DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
    // D-D-3: history multi-select sole on DashboardState.
    DashboardStateClearSelectedSourceKeys(m_dashboardState);
    DashboardStateClearSelectedSourceAnchor(m_dashboardState);
    // Dual-write after erase so filter/SourceRail keys see pure model immediately.
    DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
    ApplyFilter(DashboardStateFilterText(m_dashboardState));
    if (DashboardStateHasVisibleHistory(m_dashboardState)) {
        int pos = min(nextVisiblePos, (int)DashboardStateVisibleHistoryIndices(m_dashboardState).size() - 1);
        SelectHistoryItem(DashboardStateVisibleHistoryIndices(m_dashboardState)[pos]);
    } else {
        SelectHistoryItem(-1);
    }
    if (!DashboardHistorySessionSaveItems(m_history, m_dashboardState)) {
        if (DashboardStateDismissedBatchManifestKeys(m_dashboardState) !=
            dismissedManifestSnapshot) {
            DashboardStateSetDismissedBatchManifestKeys(
                m_dashboardState, std::move(dismissedManifestSnapshot));
            DashboardHistorySessionSaveDismissed(m_history, m_dashboardState);
        }
        m_history.model.items = std::move(oldHistoryItems);
        DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
        ApplyFilter(DashboardStateFilterText(m_dashboardState));
        if (oldSelectedHistoryIndex >= 0 && oldSelectedHistoryIndex < (int)m_history.model.items.size()) {
            SelectHistoryItem(oldSelectedHistoryIndex);
        }
        UpdateStatus(S::IsChinese()
            ? L"删除失败：无法安全保存 Dashboard 元数据"
            : L"Delete failed: Dashboard metadata could not be saved safely");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3000, nullptr);
        return false;
    }

    // Pure collect unreferenced under OCR dir; Window only DeleteFileW.
    DeleteCacheImagesForItems(removedItems);
    return true;
}

void OcrDashboardWindow::ClearAllHistory() {
    if (MessageBoxW(m_hwnd,
        S::IsChinese()
            ? L"从 Dashboard 清理所有已完成、失败或取消的来源？原文件和输出目录会保留。"
            : L"Clear all completed, failed, or canceled Sources from the Dashboard? Original files and outputs are kept.",
        L"ZenCrop", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    ClearAllHistoryRecords();
}

void OcrDashboardWindow::ClearAllHistoryRecords() {
    auto terminal = [](BatchOcrTaskStatus status) {
        return status == BatchOcrTaskStatus::Completed ||
            status == BatchOcrTaskStatus::Failed ||
            status == BatchOcrTaskStatus::Canceled;
    };

    std::vector<BatchOcrImageJob> removedImageJobs;
    std::vector<std::wstring> removedOutputRoots;
    auto rememberRemovedOutputRoot = [&](const std::wstring& outputRoot) {
        if (outputRoot.empty()) return;
        if (std::find_if(removedOutputRoots.begin(), removedOutputRoots.end(),
                [&](const std::wstring& existing) {
                    return SameDashboardPath(existing, outputRoot);
                }) == removedOutputRoots.end()) {
            removedOutputRoots.push_back(outputRoot);
        }
    };
    for (const auto& task : m_batch.batchTasks) {
        if (terminal(task.status)) {
            removedImageJobs.push_back(task.job);
            rememberRemovedOutputRoot(task.job.outputRoot);
        }
    }
    std::vector<std::wstring> removedPdfKeys;
    std::vector<BatchOcrPdfJob> removedPdfJobs;
    for (const auto& job : m_batch.activePdfJobs) {
        bool hasRunningPage = std::any_of(job.pages.begin(), job.pages.end(),
            [&](const BatchOcrPdfPageJob& page) { return !terminal(page.status); });
        if (terminal(job.status) && !hasRunningPage) {
            removedPdfKeys.push_back(DashboardPdfJobTreeKey(job));
            removedPdfJobs.push_back(job);
            rememberRemovedOutputRoot(job.outputRoot);
        }
    }

    auto imageWillBeRemoved = [&](const BatchOcrImageJob& job) {
        return std::find_if(removedImageJobs.begin(), removedImageJobs.end(),
            [&](const BatchOcrImageJob& existing) {
                return SameDashboardImageJobKey(existing, job);
            }) != removedImageJobs.end();
    };
    auto pdfWillBeRemoved = [&](const BatchOcrPdfJob& job) {
        std::wstring key = DashboardPdfJobTreeKey(job);
        return DashboardPdfJobTreeKeyInList(removedPdfKeys, key);
    };

    auto historyLinkedToRetainedTask = [&](const OcrDashboardHistoryItem& history) {
        bool linkedToRetainedTask = false;
        if (IsValidBatchOcrSourceInstanceId(history.sourceInstanceId)) {
            for (const auto& task : m_batch.batchTasks) {
                if (DashboardProjectionTextEquals(
                        history.sourceInstanceId,
                        task.job.sourceInstanceId) &&
                    !imageWillBeRemoved(task.job)) {
                    linkedToRetainedTask = true;
                    break;
                }
            }
        }
        return linkedToRetainedTask;
    };

    std::vector<OcrDashboardHistoryItem> removedHistoryItems;
    for (const auto& history : m_history.model.items) {
        if (!historyLinkedToRetainedTask(history)) removedHistoryItems.push_back(history);
    }

    bool selectedImageRemoved = false;
    if (const DashboardBatchTaskItem* selectedTask = GetSelectedImageTask()) {
        selectedImageRemoved = imageWillBeRemoved(selectedTask->job);
    }
    bool selectedPdfRemoved = false;
    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey selectedKey;
        selectedKey.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        selectedKey.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        selectedKey.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        selectedKey.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        const BatchOcrPdfJob* selectedJob = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, selectedKey);
        selectedPdfRemoved = selectedJob && pdfWillBeRemoved(*selectedJob);
    }
    bool selectedHistoryRemoved = false;
    if (const auto* selectedHistory = m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState))) {
        selectedHistoryRemoved = !historyLinkedToRetainedTask(*selectedHistory);
    }

    auto manifestRetainedBySource = [&](const std::wstring& manifestPath) {
        if (manifestPath.empty()) return false;
        if (std::any_of(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
                [&](const DashboardBatchTaskItem& task) {
                    return !imageWillBeRemoved(task.job) &&
                        SameDashboardPath(task.job.manifestPath, manifestPath);
                })) {
            return true;
        }
        return std::any_of(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
            [&](const BatchOcrPdfJob& job) {
                return !pdfWillBeRemoved(job) &&
                    SameDashboardPath(job.manifestPath, manifestPath);
            });
    };
    std::vector<std::wstring> dismissedManifests;
    for (const auto& job : removedImageJobs) {
        dismissedManifests.push_back(DashboardHistoryBuildImageDismissalKey(
            job.manifestPath, job.sourceInstanceId, job.createdAt, job.sourcePath));
    }
    for (const auto& job : removedPdfJobs) {
        dismissedManifests.push_back(DashboardHistoryBuildPdfDismissalKey(
            job.manifestPath, job.createdAt, job.sourcePath));
    }
    for (const auto& history : removedHistoryItems) {
        if (!history.originManifestPath.empty() &&
            !manifestRetainedBySource(history.originManifestPath)) {
            dismissedManifests.push_back(DashboardHistoryBuildHistoryItemDismissalKey(
                history.originManifestPath, history.sourceInstanceId));
        }
    }

    auto historySnapshot = m_history.model.items;
    auto batchTasksSnapshot = m_batch.batchTasks;
    auto activePdfSnapshot = m_batch.activePdfJobs;
    auto failedBatchSnapshot = m_batch.failedBatchJobs;
    auto failedPdfSnapshot = m_batch.failedPdfJobs;
    auto failedPdfPagesSnapshot = m_batch.failedPdfPages;
    auto expandedPdfSnapshot = m_dashboardState.expandedPdfJobKeys;
    auto pausedPdfSnapshot = m_dashboardState.pausedPdfJobKeys;
    auto pausedPdfPagesSnapshot = m_dashboardState.pausedPdfPageKeys;
    // D-C-5: snapshot pure dismissed keys for rollback.
    auto dismissedManifestsSnapshot =
        DashboardStateDismissedBatchManifestKeys(m_dashboardState);

    if (!DashboardHistorySessionDismissKeys(m_history, m_dashboardState, dismissedManifests)) {
        UpdateStatus(S::IsChinese()
            ? L"清理失败：无法安全保存 Dashboard 删除记录"
            : L"Clear failed: Dashboard removal metadata could not be saved safely");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3200, nullptr);
        RefreshSourceRailBatchSection();
        return;
    }

    m_history.model.items.erase(
        std::remove_if(m_history.model.items.begin(), m_history.model.items.end(),
            [&](const OcrDashboardHistoryItem& history) {
                return !historyLinkedToRetainedTask(history);
            }),
        m_history.model.items.end());
    // Dual-write after bulk erase before SaveHistory/filter.
    DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
    m_batch.batchTasks.erase(
        std::remove_if(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
            [&](const DashboardBatchTaskItem& task) { return imageWillBeRemoved(task.job); }),
        m_batch.batchTasks.end());
    m_batch.activePdfJobs.erase(
        std::remove_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
            [&](const BatchOcrPdfJob& job) { return pdfWillBeRemoved(job); }),
        m_batch.activePdfJobs.end());
    m_batch.failedBatchJobs.erase(
        std::remove_if(m_batch.failedBatchJobs.begin(), m_batch.failedBatchJobs.end(), imageWillBeRemoved),
        m_batch.failedBatchJobs.end());
    m_batch.failedPdfJobs.erase(
        std::remove_if(m_batch.failedPdfJobs.begin(), m_batch.failedPdfJobs.end(), pdfWillBeRemoved),
        m_batch.failedPdfJobs.end());
    m_batch.failedPdfPages.erase(
        std::remove_if(m_batch.failedPdfPages.begin(), m_batch.failedPdfPages.end(),
            [&](const DashboardPdfRetryPage& page) { return pdfWillBeRemoved(page.job); }),
        m_batch.failedPdfPages.end());
    for (const auto& key : removedPdfKeys) {
        m_dashboardState.expandedPdfJobKeys.erase(
            std::remove_if(m_dashboardState.expandedPdfJobKeys.begin(), m_dashboardState.expandedPdfJobKeys.end(),
                [&](const std::wstring& existing) { return DashboardPdfJobTreeKeyEquals(existing, key); }),
            m_dashboardState.expandedPdfJobKeys.end());
        m_dashboardState.pausedPdfJobKeys.erase(
            std::remove_if(m_dashboardState.pausedPdfJobKeys.begin(), m_dashboardState.pausedPdfJobKeys.end(),
                [&](const std::wstring& existing) { return DashboardPdfJobTreeKeyEquals(existing, key); }),
            m_dashboardState.pausedPdfJobKeys.end());
    }
    for (const auto& job : removedPdfJobs) {
        EraseDashboardPdfPageKeysForJob(m_dashboardState.pausedPdfPageKeys, job);
    }
    if (!DashboardHistorySessionSaveItems(m_history, m_dashboardState)) {
        if (DashboardStateDismissedBatchManifestKeys(m_dashboardState) !=
            dismissedManifestsSnapshot) {
            DashboardStateSetDismissedBatchManifestKeys(
                m_dashboardState, std::move(dismissedManifestsSnapshot));
            DashboardHistorySessionSaveDismissed(m_history, m_dashboardState);
        }
        m_history.model.items = std::move(historySnapshot);
        DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
        m_batch.batchTasks = std::move(batchTasksSnapshot);
        m_batch.activePdfJobs = std::move(activePdfSnapshot);
        m_batch.failedBatchJobs = std::move(failedBatchSnapshot);
        m_batch.failedPdfJobs = std::move(failedPdfSnapshot);
        m_batch.failedPdfPages = std::move(failedPdfPagesSnapshot);
        m_dashboardState.expandedPdfJobKeys = std::move(expandedPdfSnapshot);
        m_dashboardState.pausedPdfJobKeys = std::move(pausedPdfSnapshot);
        m_dashboardState.pausedPdfPageKeys = std::move(pausedPdfPagesSnapshot);
        ApplyFilter(DashboardStateFilterText(m_dashboardState));
        UpdateStatus(S::IsChinese()
            ? L"清理失败：无法安全保存 Dashboard 元数据"
            : L"Clear failed: Dashboard metadata could not be saved safely");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3000, nullptr);
        RefreshSourceRailBatchSection();
        return;
    }

    for (const auto& outputRoot : removedOutputRoots) {
        ForgetBatchOutputRootIfUnused(outputRoot);
    }

    if (selectedImageRemoved || selectedPdfRemoved || selectedHistoryRemoved) {
        ReleaseGdiplusImages();
    }
    DeleteCacheImagesForItems(removedHistoryItems);
    m_historyRanges.clear();
    m_previewInfos.clear();
    m_actionButtons.clear();
    // D-D-6: hoveredActionBtn sole on DashboardState.
    DashboardStateSetHoveredActionBtn(m_dashboardState, -1);
    if (selectedImageRemoved) ClearImageTaskSelection();
    if (selectedPdfRemoved) ClearPdfSelection();
    if (selectedHistoryRemoved) {
        SetSelectedHistoryIndex(-1);
        DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
        // D-D-3: history multi-select sole on DashboardState.
        DashboardStateClearSelectedSourceKeys(m_dashboardState);
        DashboardStateClearSelectedSourceAnchor(m_dashboardState);
    }
    // SaveHistory already mirrored on success; re-sync after selection mutations.
    DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
    ApplyFilter(DashboardStateFilterText(m_dashboardState));
    bool preserveSelection = (DashboardStateHasImageTaskSelection(m_dashboardState) && GetSelectedImageTask()) ||
        (DashboardStateHasPdfSelection(m_dashboardState) && EnsurePdfSelectionStillValid(false)) ||
        m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState)) != nullptr;
    if (preserveSelection) RebuildHistoryText(false);
    else {
        LoadImageIntoCanvas(L"", false);
        SetHistoryEditText(L"");
    }
    RenderSelectedItemPreview();
    UpdatePreviewControls();
    SaveBatchSessionState();
    RefreshSourceRailBatchSection();
    UpdateStatus(S::IsChinese() ? L"✓ 已清理结束的来源" : L"✓ Finished Sources cleared");
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
}

// Static method to save a single record to history file without opening window
void OcrDashboardWindow::SaveToHistoryFile(const OcrDashboardHistoryItem& item) {
    // D-C-PERSIST: disk append sole in session free function.
    DashboardHistorySessionSaveItemToDefaultFile(item);
}

// D-C-PERSIST: SyncHistoryModelMirror deleted — call DashboardHistorySessionSyncSelection.

void OcrDashboardWindow::SetSelectedHistoryIndex(int index) {
    // D-D-5: pure selection transition via Controller (model + State key/index).
    DashboardControllerApplySelectHistoryIndex(
        m_dashboardState, m_history.model, index);
}

// D-C-S5: SetExpandedHistoryIndex / HistoryItemForRead / SelectedHistoryItemForRead /
// HistoryItemsForKeys deleted — call sites use DashboardStateSetExpandedHistoryIndex
// and m_history.model.itemAt / m_history.model.items directly.

// D-C-PERSIST: SaveHistory / LoadHistory deleted as pure wrappers.
// Save → DashboardHistorySessionSaveItems (+ SyncSelection on success at call sites).
// Load → UI clear + DashboardHistorySessionLoadItems + SyncSelection + ApplyFilter.
// Remaining Host-only LoadHistory orchestration kept as thin method for UI clear.

void OcrDashboardWindow::LoadHistory() {
    m_historyRanges.clear();
    m_actionButtons.clear();
    // D-D-6: hoveredActionBtn sole on DashboardState.
    DashboardStateSetHoveredActionBtn(m_dashboardState, -1);
    DashboardHistorySessionLoadItems(m_history, m_dashboardState);
    DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
    ApplyFilter(DashboardStateFilterText(m_dashboardState));
}

// Reformat all history text with current separator width (called on resize)
void OcrDashboardWindow::ReformatHistoryText() {
    RebuildHistoryText(true);
}
