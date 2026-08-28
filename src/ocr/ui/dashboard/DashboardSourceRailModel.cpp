#include "ocr/ui/dashboard/DashboardSourceRailModel.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <windows.h>

// D-F-2: pure SourceRail model helpers / builders (no HWND / GDI / S::).

namespace {

void AppendSearchField(std::wstring& blob, const std::wstring& value) {
    if (value.empty()) return;
    if (!blob.empty()) blob.push_back(L'\n');
    blob += value;
}

void AppendSearchField(std::wstring& blob, const wchar_t* value) {
    if (!value || !*value) return;
    AppendSearchField(blob, std::wstring(value));
}

std::wstring FileNameOrPath(const std::wstring& path) {
    std::wstring name = DashboardFileNameFromPath(path);
    return name.empty() ? path : name;
}

bool SearchBlobMatches(std::wstring blob, const std::wstring& needleLower) {
    if (needleLower.empty()) return true;
    blob = DashboardToLowerWide(std::move(blob));
    return blob.find(needleLower) != std::wstring::npos;
}

bool PdfTreeKeyExpanded(
    const std::set<std::wstring>& expandedPdfTreeKeys,
    const BatchOcrPdfJob& job)
{
    if (!DashboardPdfHasVisiblePageChildren(job)) return false;
    return expandedPdfTreeKeys.find(DashboardPdfJobTreeKey(job)) != expandedPdfTreeKeys.end();
}

} // namespace

BatchOcrTaskStatus DashboardSourceRailSummarizePdfJobStatus(const BatchOcrPdfJob& job) {
    if (job.pages.empty()) return job.status;
    int pending = 0;
    int recognizing = 0;
    int writing = 0;
    int completed = 0;
    int failed = 0;
    int canceled = 0;
    for (const auto& page : job.pages) {
        switch (page.status) {
        case BatchOcrTaskStatus::Recognizing: recognizing++; break;
        case BatchOcrTaskStatus::Writing: writing++; break;
        case BatchOcrTaskStatus::Completed: completed++; break;
        case BatchOcrTaskStatus::Failed: failed++; break;
        case BatchOcrTaskStatus::Canceled: canceled++; break;
        case BatchOcrTaskStatus::Pending:
        default:
            pending++;
            break;
        }
    }
    if (recognizing > 0) return BatchOcrTaskStatus::Recognizing;
    if (writing > 0) return BatchOcrTaskStatus::Writing;
    if (failed > 0) return BatchOcrTaskStatus::Failed;
    if (canceled > 0) return BatchOcrTaskStatus::Canceled;
    if (pending > 0) return BatchOcrTaskStatus::Pending;
    return completed > 0 ? BatchOcrTaskStatus::Completed : job.status;
}

std::wstring DashboardSourceRailBatchTaskStatusLabel(BatchOcrTaskStatus status, bool zh) {
    switch (status) {
    case BatchOcrTaskStatus::Pending:
        return zh ? L"等待" : L"Pending";
    case BatchOcrTaskStatus::Recognizing:
        return zh ? L"识别中" : L"OCR";
    case BatchOcrTaskStatus::Writing:
        return zh ? L"写入" : L"Writing";
    case BatchOcrTaskStatus::Completed:
        return zh ? L"完成" : L"Done";
    case BatchOcrTaskStatus::Failed:
        return zh ? L"失败" : L"Failed";
    case BatchOcrTaskStatus::Canceled:
        return zh ? L"取消" : L"Canceled";
    default:
        return zh ? L"未知" : L"Unknown";
    }
}

bool DashboardSourceRailBatchTaskMatchesFilter(
    const DashboardBatchTaskItem& task,
    const std::wstring& needleLower,
    bool zh)
{
    std::wstring blob;
    AppendSearchField(blob, task.job.baseName);
    AppendSearchField(blob, FileNameOrPath(task.job.sourcePath));
    AppendSearchField(blob, task.job.sourcePath);
    AppendSearchField(blob, task.job.sourceImagePath);
    AppendSearchField(blob, task.job.outputRoot);
    AppendSearchField(blob, task.job.outputDir);
    AppendSearchField(blob, task.job.markdownPath);
    AppendSearchField(blob, task.job.textPath);
    AppendSearchField(blob, task.job.contentJsonPath);
    AppendSearchField(blob, task.job.manifestPath);
    AppendSearchField(blob, task.job.createdAt);
    AppendSearchField(blob, task.error);
    AppendSearchField(blob, task.job.error);
    AppendSearchField(blob, BatchOcrTaskStatusToString(task.status));
    AppendSearchField(blob, DashboardSourceRailBatchTaskStatusLabel(task.status, zh));
    AppendSearchField(blob, DashboardFormatElapsedShort(static_cast<unsigned long>(task.elapsedMs)));
    AppendSearchField(blob, WideFormatIntLabel(task.job.index + 1));
    return SearchBlobMatches(std::move(blob), needleLower);
}

bool DashboardSourceRailPdfJobMatchesFilter(
    const BatchOcrPdfJob& job,
    const std::wstring& needleLower,
    bool zh)
{
    BatchOcrTaskStatus status = DashboardSourceRailSummarizePdfJobStatus(job);
    std::wstring blob;
    AppendSearchField(blob, job.baseName);
    AppendSearchField(blob, FileNameOrPath(job.sourcePath));
    AppendSearchField(blob, job.sourcePath);
    AppendSearchField(blob, job.outputRoot);
    AppendSearchField(blob, job.outputDir);
    AppendSearchField(blob, job.pagesDir);
    AppendSearchField(blob, job.pageImagesDir);
    AppendSearchField(blob, job.assetsDir);
    AppendSearchField(blob, job.markdownPath);
    AppendSearchField(blob, job.textPath);
    AppendSearchField(blob, job.contentJsonPath);
    AppendSearchField(blob, job.manifestPath);
    AppendSearchField(blob, job.createdAt);
    AppendSearchField(blob, job.pageRange);
    AppendSearchField(blob, job.error);
    AppendSearchField(blob, BatchOcrTaskStatusToString(status));
    AppendSearchField(blob, DashboardSourceRailBatchTaskStatusLabel(status, zh));
    AppendSearchField(blob, DashboardFormatElapsedShort(static_cast<unsigned long>(job.elapsedMs)));
    AppendSearchField(blob, WideFormatIntLabel(job.index + 1));
    AppendSearchField(blob, WideFormatIntLabel(job.sourcePageCount));
    AppendSearchField(blob, WideFormatIntLabel(job.pdfRenderDpi));
    if (job.requiresPassword) {
        AppendSearchField(blob, L"requires password");
        AppendSearchField(blob, L"password");
    }
    return SearchBlobMatches(std::move(blob), needleLower);
}

bool DashboardSourceRailPdfPageMatchesFilter(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page,
    const std::wstring& needleLower,
    bool zh)
{
    std::wstring blob;
    AppendSearchField(blob, WideFormatPageLabel(page.pageIndex));
    AppendSearchField(blob, WideFormatCountPrefix(L"page_", page.pageIndex));
    AppendSearchField(blob, job.baseName);
    AppendSearchField(blob, FileNameOrPath(job.sourcePath));
    AppendSearchField(blob, page.sourceImagePath);
    AppendSearchField(blob, page.markdownPath);
    AppendSearchField(blob, page.textPath);
    AppendSearchField(blob, page.contentJsonPath);
    AppendSearchField(blob, page.engineMode);
    AppendSearchField(blob, page.error);
    AppendSearchField(blob, page.markdown);
    AppendSearchField(blob, page.plainText);
    AppendSearchField(blob, BatchOcrTaskStatusToString(page.status));
    AppendSearchField(blob, DashboardSourceRailBatchTaskStatusLabel(page.status, zh));
    AppendSearchField(blob, DashboardFormatElapsedShort(static_cast<unsigned long>(page.elapsedMs)));
    AppendSearchField(blob, WideFormatIntLabel(page.pageIndex));
    AppendSearchField(blob, WideFormatIntLabel(page.width));
    AppendSearchField(blob, WideFormatIntLabel(page.height));
    if (page.scaledDown) AppendSearchField(blob, L"scaled down");
    if (page.skippedTooLarge) AppendSearchField(blob, L"too large");
    for (const auto& asset : page.assets) {
        AppendSearchField(blob, asset);
    }
    return SearchBlobMatches(std::move(blob), needleLower);
}

std::vector<DashboardSourceRailTaskRow> DashboardSourceRailBuildTaskRows(
    const std::vector<DashboardBatchTaskItem>& batchTasks,
    const std::vector<BatchOcrPdfJob>& activePdfJobs,
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const std::wstring& filterText,
    const std::set<std::wstring>& expandedPdfTreeKeys,
    bool zh)
{
    std::vector<DashboardSourceRailTaskRow> rows;
    rows.reserve(batchTasks.size() + activePdfJobs.size() * 2);
    const auto projection = BuildDashboardSourceProjection(
        batchTasks, activePdfJobs, historyItems);

    std::wstring needle = DashboardToLowerWide(filterText);
    const bool filterActive = !needle.empty();

    for (const auto& source : projection) {
        int i = source.refs.imageTaskIndex;
        if (i < 0 || i >= (int)batchTasks.size()) continue;
        int linkedHistoryIndex = source.refs.historyIndex;
        bool linkedHistoryMatch = false;
        if (filterActive && linkedHistoryIndex >= 0 &&
            linkedHistoryIndex < (int)historyItems.size()) {
            const auto& history = historyItems[(size_t)linkedHistoryIndex];
            std::wstring historyHaystack = DashboardToLowerWide(
                history.text + L"\n" + history.timestamp + L"\n" + history.imagePath);
            linkedHistoryMatch = historyHaystack.find(needle) != std::wstring::npos;
        }
        if (!filterActive ||
            DashboardSourceRailBatchTaskMatchesFilter(batchTasks[(size_t)i], needle, zh) ||
            linkedHistoryMatch) {
            DashboardSourceRailTaskRow row;
            row.kind = DashboardSourceRailTaskRowKind::ImageTask;
            row.imageTaskIndex = i;
            row.linkedHistoryIndex = linkedHistoryIndex;
            row.stableSourceKey = source.stableSourceKey;
            rows.push_back(row);
        }
    }

    for (const auto& source : projection) {
        int jobIndex = source.refs.pdfJobIndex;
        if (jobIndex < 0 || jobIndex >= (int)activePdfJobs.size()) continue;
        const auto& job = activePdfJobs[(size_t)jobIndex];
        bool jobMatch = !filterActive || DashboardSourceRailPdfJobMatchesFilter(job, needle, zh);
        std::vector<int> matchingPageIndexes;
        if (filterActive) {
            for (int pageOffset = 0; pageOffset < (int)job.pages.size(); ++pageOffset) {
                if (DashboardSourceRailPdfPageMatchesFilter(
                        job, job.pages[(size_t)pageOffset], needle, zh)) {
                    matchingPageIndexes.push_back(pageOffset);
                }
            }
        }

        if (filterActive && !jobMatch && matchingPageIndexes.empty()) {
            continue;
        }

        DashboardSourceRailTaskRow jobRow;
        jobRow.kind = DashboardSourceRailTaskRowKind::PdfJob;
        jobRow.pdfJobIndex = jobIndex;
        jobRow.stableSourceKey = source.stableSourceKey;
        rows.push_back(jobRow);

        if (!DashboardPdfHasVisiblePageChildren(job)) continue;

        if (!filterActive) {
            if (!PdfTreeKeyExpanded(expandedPdfTreeKeys, job)) continue;
            for (const auto& page : job.pages) {
                if (page.pageIndex <= 1) continue;
                DashboardSourceRailTaskRow pageRow;
                pageRow.kind = DashboardSourceRailTaskRowKind::PdfPage;
                pageRow.pdfJobIndex = jobIndex;
                pageRow.pageIndex = page.pageIndex;
                pageRow.stableSourceKey = source.stableSourceKey +
                    WideFormatColonPageKey(page.pageIndex);
                rows.push_back(pageRow);
            }
            continue;
        }

        if (jobMatch && PdfTreeKeyExpanded(expandedPdfTreeKeys, job)) {
            for (const auto& page : job.pages) {
                if (page.pageIndex <= 1) continue;
                DashboardSourceRailTaskRow pageRow;
                pageRow.kind = DashboardSourceRailTaskRowKind::PdfPage;
                pageRow.pdfJobIndex = jobIndex;
                pageRow.pageIndex = page.pageIndex;
                pageRow.stableSourceKey = source.stableSourceKey +
                    WideFormatColonPageKey(page.pageIndex);
                rows.push_back(pageRow);
            }
            continue;
        }

        for (int pageOffset : matchingPageIndexes) {
            if (pageOffset < 0 || pageOffset >= (int)job.pages.size()) continue;
            if (job.pages[(size_t)pageOffset].pageIndex <= 1) continue;
            DashboardSourceRailTaskRow pageRow;
            pageRow.kind = DashboardSourceRailTaskRowKind::PdfPage;
            pageRow.pdfJobIndex = jobIndex;
            pageRow.pageIndex = job.pages[(size_t)pageOffset].pageIndex;
            pageRow.stableSourceKey = source.stableSourceKey +
                WideFormatColonPageKey(pageRow.pageIndex);
            rows.push_back(pageRow);
        }
    }

    return rows;
}

bool DashboardSourceRailParseAddedDate(
    const std::wstring& value,
    uint64_t& sortTime,
    std::wstring& displayText)
{
    sortTime = 0;
    displayText.clear();
    if (value.empty()) return false;

    std::wstring normalized = value;
    bool utc = false;
    if (!normalized.empty() && (normalized.back() == L'Z' || normalized.back() == L'z')) {
        utc = true;
        normalized.pop_back();
    }
    std::replace(normalized.begin(), normalized.end(), L'T', L' ');

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    int parsed = WideTryParseDateTimeParts(
        normalized, year, month, day, hour, minute, second, millisecond);
    if (parsed < 5) return false;

    SYSTEMTIME systemTime = {};
    systemTime.wYear = static_cast<WORD>(year);
    systemTime.wMonth = static_cast<WORD>(month);
    systemTime.wDay = static_cast<WORD>(day);
    systemTime.wHour = static_cast<WORD>(hour);
    systemTime.wMinute = static_cast<WORD>(minute);
    systemTime.wSecond = static_cast<WORD>(parsed >= 6 ? second : 0);
    systemTime.wMilliseconds = static_cast<WORD>(parsed >= 7 ? millisecond : 0);
    FILETIME fileTime = {};
    if (!SystemTimeToFileTime(&systemTime, &fileTime)) return false;

    if (utc) {
        FILETIME localFileTime = {};
        SYSTEMTIME localTime = {};
        if (!FileTimeToLocalFileTime(&fileTime, &localFileTime) ||
            !FileTimeToSystemTime(&localFileTime, &localTime) ||
            !SystemTimeToFileTime(&localTime, &fileTime)) {
            return false;
        }
        systemTime = localTime;
    }

    ULARGE_INTEGER bits = {};
    bits.LowPart = fileTime.dwLowDateTime;
    bits.HighPart = fileTime.dwHighDateTime;
    sortTime = bits.QuadPart;
    displayText = WideFormatDateTimeMinuteParts(
        systemTime.wYear, systemTime.wMonth, systemTime.wDay,
        systemTime.wHour, systemTime.wMinute);
    return true;
}

namespace {

bool SetContains(const std::set<std::wstring>& keys, const std::wstring& key) {
    return !key.empty() && keys.find(key) != keys.end();
}

std::wstring MetaWithElapsed(std::wstring text, DWORD elapsedMs) {
    std::wstring elapsed = DashboardFormatElapsedShort(static_cast<unsigned long>(elapsedMs));
    if (!elapsed.empty()) {
        if (!text.empty()) text += L" \x00b7 ";
        text += elapsed;
    }
    return text;
}

std::wstring MetaWithEngineAndElapsed(
    std::wstring text,
    const std::wstring& engineMode,
    DWORD elapsedMs)
{
    text = MetaWithElapsed(std::move(text), elapsedMs);
    if (!engineMode.empty()) {
        if (!text.empty()) text += L" \x00b7 ";
        text += DashboardOcrModeLabel(engineMode);
    }
    return text;
}

DashboardSourceRailSelectableRow MakeSelection(
    DashboardSourceRailRowKind kind,
    const DashboardSourceProjectionEntry& source)
{
    DashboardSourceRailSelectableRow row;
    row.kind = kind;
    row.imageTaskIndex = source.refs.imageTaskIndex;
    row.pdfJobIndex = source.refs.pdfJobIndex;
    row.historyIndex = source.refs.historyIndex;
    row.linkedHistoryIndex = source.refs.historyIndex;
    row.stableSourceKey = source.stableSourceKey;
    return row;
}

} // namespace

std::vector<DashboardSourceRailViewRow> DashboardSourceRailBuildViewRows(
    const std::vector<DashboardBatchTaskItem>& batchTasks,
    const std::vector<BatchOcrPdfJob>& activePdfJobs,
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const std::wstring& filterText,
    const std::set<std::wstring>& expandedPdfTreeKeys,
    const std::set<std::wstring>& pausedPdfJobKeys,
    const std::set<std::wstring>& pausedPdfPageKeys,
    const std::set<std::wstring>& renderingPdfTreeKeys,
    bool sortNewestFirst,
    bool zh,
    int childIndentPx)
{
    struct Root {
        DashboardSourceRailViewRow root;
        std::vector<DashboardSourceRailViewRow> children;
    };

    const auto projection = BuildDashboardSourceProjection(
        batchTasks, activePdfJobs, historyItems);
    const std::wstring needle = DashboardToLowerWide(filterText);
    const bool filterActive = !needle.empty();
    std::vector<Root> roots;
    roots.reserve(projection.size());

    auto setDate = [&](DashboardSourceRailViewRow& row, const std::wstring& value) {
        row.hasSortTime = DashboardSourceRailParseAddedDate(value, row.sortTime, row.addedDateText);
        if (!row.hasSortTime) {
            row.addedDateText = zh ? L"未知日期" : L"Unknown date";
        }
    };

    for (const auto& source : projection) {
        if (source.refs.imageTaskIndex >= 0 &&
            source.refs.imageTaskIndex < static_cast<int>(batchTasks.size())) {
            const int index = source.refs.imageTaskIndex;
            const auto& task = batchTasks[static_cast<size_t>(index)];
            bool linkedHistoryMatch = false;
            if (filterActive && source.refs.historyIndex >= 0 &&
                source.refs.historyIndex < static_cast<int>(historyItems.size())) {
                const auto& history = historyItems[static_cast<size_t>(source.refs.historyIndex)];
                std::wstring historyHaystack = DashboardToLowerWide(
                    history.text + L"\n" + history.timestamp + L"\n" + history.imagePath);
                linkedHistoryMatch = historyHaystack.find(needle) != std::wstring::npos;
            }
            if (filterActive &&
                !DashboardSourceRailBatchTaskMatchesFilter(task, needle, zh) &&
                !linkedHistoryMatch) {
                continue;
            }

            Root item;
            auto& row = item.root;
            row.selection = MakeSelection(DashboardSourceRailRowKind::ImageTask, source);
            row.title = source.display.displayName;
            if (row.title.empty()) row.title = WideFormatImageTitle(index + 1);
            row.thumbnailPath = source.display.thumbnailPath;
            row.error = source.display.error;
            row.status = task.status;
            row.statusText = DashboardSourceRailStatusText(row.status, false, false, false, zh);
            row.metaText = MetaWithEngineAndElapsed(
                L"Image", task.job.engineMode, source.display.elapsedMs);
            row.displayOrder = source.displayOrderKey;
            setDate(row, source.display.timestamp);
            roots.push_back(std::move(item));
            continue;
        }

        if (source.refs.pdfJobIndex >= 0 &&
            source.refs.pdfJobIndex < static_cast<int>(activePdfJobs.size())) {
            const int index = source.refs.pdfJobIndex;
            const auto& job = activePdfJobs[static_cast<size_t>(index)];
            const bool jobMatch =
                !filterActive || DashboardSourceRailPdfJobMatchesFilter(job, needle, zh);
            std::vector<int> matchingPages;
            if (filterActive) {
                for (const auto& page : job.pages) {
                    if (DashboardSourceRailPdfPageMatchesFilter(job, page, needle, zh)) {
                        matchingPages.push_back(page.pageIndex);
                    }
                }
            }
            if (filterActive && !jobMatch && matchingPages.empty()) continue;

            Root item;
            auto& row = item.root;
            row.selection = MakeSelection(DashboardSourceRailRowKind::PdfJob, source);
            row.title = source.display.displayName;
            if (row.title.empty()) row.title = WideFormatPdfTitle(index + 1);
            row.thumbnailPath = source.display.thumbnailPath;
            row.error = source.display.error;
            row.status = DashboardSourceRailSummarizePdfJobStatus(job);
            const std::wstring treeKey = DashboardPdfJobTreeKey(job);
            row.paused = SetContains(pausedPdfJobKeys, treeKey);
            row.requiresPassword = job.requiresPassword;
            const bool terminal = row.status == BatchOcrTaskStatus::Completed ||
                row.status == BatchOcrTaskStatus::Failed ||
                row.status == BatchOcrTaskStatus::Canceled;
            row.rendering = !terminal && job.pages.empty() && SetContains(renderingPdfTreeKeys, treeKey);
            row.statusText = DashboardSourceRailStatusText(
                row.status, row.paused, row.rendering, row.requiresPassword, zh);
            int completedPages = 0;
            for (const auto& page : job.pages) {
                if (page.status == BatchOcrTaskStatus::Completed) ++completedPages;
            }
            row.metaText = L"PDF";
            if (!job.pages.empty()) {
                row.metaText += WideFormatMiddotSlashCount(
                    completedPages, (int)job.pages.size());
            }
            row.metaText = MetaWithEngineAndElapsed(
                std::move(row.metaText), job.engineMode, job.elapsedMs);
            row.expandable = DashboardPdfHasVisiblePageChildren(job);
            row.expanded = row.expandable && PdfTreeKeyExpanded(expandedPdfTreeKeys, job);
            row.displayOrder = source.displayOrderKey;
            setDate(row, source.display.timestamp);

            std::vector<int> childPages;
            if (row.expanded) {
                for (const auto& page : job.pages) {
                    if (page.pageIndex > 1) childPages.push_back(page.pageIndex);
                }
            } else if (filterActive && !jobMatch) {
                childPages = matchingPages;
                childPages.erase(std::remove_if(childPages.begin(), childPages.end(),
                    [](int pageIndex) { return pageIndex <= 1; }), childPages.end());
            }
            std::sort(childPages.begin(), childPages.end());
            childPages.erase(std::unique(childPages.begin(), childPages.end()), childPages.end());
            for (int pageIndex : childPages) {
                auto pageIt = std::find_if(job.pages.begin(), job.pages.end(),
                    [&](const BatchOcrPdfPageJob& page) { return page.pageIndex == pageIndex; });
                if (pageIt == job.pages.end()) continue;
                DashboardSourceRailViewRow child;
                child.selection = MakeSelection(DashboardSourceRailRowKind::PdfPage, source);
                child.selection.pageIndex = pageIndex;
                child.selection.stableSourceKey = source.stableSourceKey +
                    WideFormatColonPageKey(pageIndex);
                child.title = WideFormatPageLabel(pageIndex);
                child.error = pageIt->error;
                child.status = pageIt->status;
                const std::wstring pagePauseKey = WidePdfPagePauseKey(treeKey, pageIndex);
                child.paused = row.paused || SetContains(pausedPdfPageKeys, pagePauseKey);
                child.statusText = DashboardSourceRailStatusText(
                    child.status, child.paused, false, false, zh);
                child.metaText = MetaWithEngineAndElapsed(
                    L"", pageIt->engineMode.empty() ? job.engineMode : pageIt->engineMode,
                    pageIt->elapsedMs);
                child.rootRow = false;
                child.pageRow = true;
                child.indent = childIndentPx;
                child.displayOrder = row.displayOrder;
                item.children.push_back(std::move(child));
            }
            roots.push_back(std::move(item));
            continue;
        }

        if (source.refs.historyIndex >= 0) {
            const int index = source.refs.historyIndex;
            if (index < 0 || index >= static_cast<int>(historyItems.size())) continue;
            const auto& history = historyItems[static_cast<size_t>(index)];
            if (filterActive) {
                std::wstring haystack = DashboardToLowerWide(
                    history.text + L"\n" + history.timestamp + L"\n" + history.imagePath);
                if (haystack.find(needle) == std::wstring::npos) continue;
            }
            Root item;
            auto& row = item.root;
            row.selection = MakeSelection(DashboardSourceRailRowKind::History, source);
            row.title = source.display.displayName;
            if (row.title.empty()) row.title = WideFormatCaptureTitle(index + 1);
            row.thumbnailPath = source.display.thumbnailPath;
            row.status = BatchOcrTaskStatus::Completed;
            row.statusText.clear();
            row.metaText = MetaWithEngineAndElapsed(
                source.display.kind == DashboardSourceKind::ImageFile ? L"Image" : L"Capture",
                history.engineMode,
                source.display.elapsedMs);
            row.displayOrder = source.displayOrderKey;
            setDate(row, source.display.timestamp);
            roots.push_back(std::move(item));
        }
    }

    std::stable_sort(roots.begin(), roots.end(),
        [&](const Root& left, const Root& right) {
            if (left.root.hasSortTime != right.root.hasSortTime) {
                return left.root.hasSortTime;
            }
            if (left.root.hasSortTime && left.root.sortTime != right.root.sortTime) {
                return sortNewestFirst
                    ? left.root.sortTime > right.root.sortTime
                    : left.root.sortTime < right.root.sortTime;
            }
            if (left.root.displayOrder != right.root.displayOrder) {
                return left.root.displayOrder < right.root.displayOrder;
            }
            return left.root.selection.stableSourceKey < right.root.selection.stableSourceKey;
        });

    std::vector<DashboardSourceRailViewRow> rows;
    for (auto& root : roots) {
        rows.push_back(std::move(root.root));
        for (auto& child : root.children) rows.push_back(std::move(child));
    }
    return rows;
}
