#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardCanvasModel.h"
#include "dashboard/DashboardCanvasMath.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardFileTypes.h"
#include "core/ClipboardUtils.h"
#include "Strings.h"
#include "OcrBlockJson.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"
#include "core/JsonUtils.h"

#include <algorithm>
#include <fstream>
#include <gdiplus.h>
#include <limits>
#include <sstream>
#include <vector>
#include <windows.h>

// D-I-3: real TU (was Blocks.inl).

static std::wstring DashboardNormalizeBlockLabel(const std::wstring& label) {
    if (label.empty()) return L"text";
    std::wstring out = label;
    out = WideNormalizeLabelToken(std::move(out)); // OWN-79
    return out.empty() ? L"text" : out;
}

static std::wstring DashboardBlockDisplayLabel(const std::wstring& label) {
    std::wstring normalized = DashboardNormalizeBlockLabel(label);
    if (normalized == L"doc_title") return L"Title";
    if (normalized == L"paragraph_title") return L"Heading";
    if (normalized == L"display_formula") return L"Formula";
    if (normalized == L"inline_formula") return L"Formula";
    if (normalized == L"formula_number") return L"Formula";
    if (normalized == L"figure_title") return L"Figure title";
    if (normalized == L"vision_footnote") return L"Footnote";
    if (normalized == L"text_line") return L"Text line";

    std::wstring result;
    bool capNext = true;
    for (wchar_t ch : normalized) {
        if (ch == L'_') {
            result.push_back(L' ');
            capNext = true;
            continue;
        }
        result.push_back(capNext ? (wchar_t)towupper(ch) : ch);
        capNext = false;
    }
    return result.empty() ? L"Text" : result;
}

static std::wstring DashboardBlockPreviewText(const std::wstring& text, size_t maxChars = 220) {
    std::wstring out;
    out.reserve((std::min)(text.size(), maxChars));
    bool lastSpace = false;
    for (wchar_t ch : text) {
        wchar_t normalized = (ch == L'\r' || ch == L'\n' || ch == L'\t') ? L' ' : ch;
        if (iswspace(normalized)) {
            if (lastSpace) continue;
            lastSpace = true;
            normalized = L' ';
        } else {
            lastSpace = false;
        }
        out.push_back(normalized);
        if (out.size() >= maxChars) {
            out += L"...";
            break;
        }
    }
    return out;
}

// D-I-3: DashboardColorRefForBlockLabel free in DashboardHostUtils.h

// D-I-3: DashboardGdiColorForBlockLabel free in DashboardHostUtils.h

static bool DashboardRectIsValid(const RECT& r) {
    return r.right > r.left && r.bottom > r.top;
}

static LONG DashboardRectArea(const RECT& r) {
    if (!DashboardRectIsValid(r)) return 0;
    return max(0L, r.right - r.left) * max(0L, r.bottom - r.top);
}

static RECT DashboardIntersectRectValue(const RECT& a, const RECT& b) {
    RECT out = {
        max(a.left, b.left),
        max(a.top, b.top),
        min(a.right, b.right),
        min(a.bottom, b.bottom)
    };
    if (!DashboardRectIsValid(out)) return {0, 0, 0, 0};
    return out;
}

static LONG DashboardRectIntersectionArea(const RECT& a, const RECT& b) {
    return DashboardRectArea(DashboardIntersectRectValue(a, b));
}

static RECT DashboardUnionRectValue(const RECT& a, const RECT& b) {
    if (!DashboardRectIsValid(a)) return b;
    if (!DashboardRectIsValid(b)) return a;
    return {
        min(a.left, b.left),
        min(a.top, b.top),
        max(a.right, b.right),
        max(a.bottom, b.bottom)
    };
}

static RECT DashboardInflateRectValue(RECT r, int dx, int dy) {
    r.left -= dx;
    r.right += dx;
    r.top -= dy;
    r.bottom += dy;
    return r;
}

static RECT DashboardClampRectToBounds(RECT r, int width, int height, int margin) {
    int rectW = max(1, r.right - r.left);
    int rectH = max(1, r.bottom - r.top);
    int maxX = max(margin, width - rectW - margin);
    int maxY = max(margin, height - rectH - margin);
    int x = min(max(r.left, margin), maxX);
    int y = min(max(r.top, margin), maxY);
    return {x, y, x + rectW, y + rectH};
}

static POINT DashboardRectCenter(const RECT& r) {
    return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
}

static LONG DashboardDistanceSquared(POINT a, POINT b) {
    LONG dx = a.x - b.x;
    LONG dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static bool DashboardPointInRectInclusive(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static bool DashboardTextIsBlank(const std::wstring& text) {
    for (wchar_t ch : text) {
        if (!iswspace(ch)) return false;
    }
    return true;
}

// D-G-1: DashboardPointInPolygon lives in DashboardCanvasModel.h (pure free).

// OWN-72: free pure helper lives in DashboardFileTypes (DashboardPathWithSuffix).

static std::vector<OcrLayoutBlock> DashboardLayoutBlocksFromDashboardBlocks(
    const std::vector<DashboardOcrBlock>& blocks)
{
    std::vector<OcrLayoutBlock> out;
    out.reserve(blocks.size());
    for (const auto& src : blocks) {
        OcrLayoutBlock block;
        block.id = src.id;
        block.pageIndex = src.pageIndex;
        block.order = src.order;
        block.label = src.label;
        block.content = src.content;
        block.bbox = src.bbox;
        block.polygon = src.polygon;
        block.confidence = src.confidence;
        block.source = src.source;
        block.groupId = src.groupId;
        block.edited = src.edited;
        block.editBaseline = src.editBaseline;
        out.push_back(std::move(block));
    }
    return out;
}

static bool DashboardReadUtf8TextFile(const std::wstring& path, std::wstring& out) {
    out.clear();
    if (path.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 64LL * 1024LL * 1024LL) {
        CloseHandle(file);
        return false;
    }

    std::string bytes((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    bool ok = bytes.empty() ||
        ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != bytes.size()) return false;

    const char* data = bytes.data();
    int byteCount = (int)bytes.size();
    if (byteCount >= 3 &&
        (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        data += 3;
        byteCount -= 3;
    }
    if (byteCount == 0) return true;

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, data, byteCount, nullptr, 0);
    if (wideLen <= 0) return false;
    out.resize((size_t)wideLen);
    return MultiByteToWideChar(CP_UTF8, 0, data, byteCount, out.data(), wideLen) == wideLen;
}

static bool DashboardWriteUtf8TextFileAtomic(const std::wstring& path, const std::wstring& text) {
    if (path.empty()) return false;
    int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
        nullptr, 0, nullptr, nullptr);
    if (byteCount < 0) return false;
    std::string bytes((size_t)byteCount, '\0');
    if (byteCount > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
            bytes.data(), byteCount, nullptr, nullptr) != byteCount) {
        return false;
    }

    // OWN-124: pure tmp pid.tick (WideStringUtils).
    std::wstring tempPath = path + WideFormatTmpPidTick(GetCurrentProcessId(), GetTickCount64());
    HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    const char* cursor = bytes.data();
    DWORD remaining = (DWORD)bytes.size();
    bool ok = true;
    while (remaining > 0) {
        DWORD chunk = (std::min<DWORD>)(remaining, 1u << 20);
        DWORD written = 0;
        if (!WriteFile(file, cursor, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            break;
        }
        cursor += written;
        remaining -= written;
    }
    if (ok && !FlushFileBuffers(file)) ok = false;
    if (!CloseHandle(file)) ok = false;
    if (!ok) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    if (!MoveFileExW(tempPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

static bool DashboardReplaceJsonValue(
    std::wstring& json,
    const std::wstring& key,
    const std::wstring& valueJson)
{
    size_t field = OcrBlockJsonFindField(json, key);
    if (field == std::wstring::npos) return false;
    size_t colon = json.find(L':', field + key.size() + 2);
    if (colon == std::wstring::npos) return false;
    size_t start = OcrBlockJsonSkipWhitespace(json, colon + 1);
    if (start >= json.size()) return false;

    size_t end = std::wstring::npos;
    if (json[start] == L'[') {
        end = OcrBlockJsonFindMatching(json, start, L'[', L']');
        if (end != std::wstring::npos) ++end;
    } else if (json[start] == L'{') {
        end = OcrBlockJsonFindMatching(json, start, L'{', L'}');
        if (end != std::wstring::npos) ++end;
    } else if (json[start] == L'"') {
        end = start + 1;
        while (end < json.size()) {
            if (json[end] == L'\\' && end + 1 < json.size()) {
                end += 2;
            } else if (json[end] == L'"') {
                ++end;
                break;
            } else {
                ++end;
            }
        }
    } else {
        end = start;
        while (end < json.size() && json[end] != L',' && json[end] != L'}' &&
               json[end] != L'\r' && json[end] != L'\n') {
            ++end;
        }
    }
    if (end == std::wstring::npos || end < start || end > json.size()) return false;
    json.replace(start, end - start, valueJson);
    return true;
}

static std::wstring DashboardBuildBlocksJsonArtifact(
    const std::vector<OcrLayoutBlock>& blocks)
{
    std::wstringstream ss;
    ss << L"{\n";
    ss << L"  \"version\": 1,\n";
    ss << L"  \"blockCount\": " << blocks.size() << L",\n";
    ss << L"  \"blocks\": " << OcrLayoutBlocksToJson(blocks, 2) << L"\n";
    ss << L"}\n";
    return ss.str();
}

static bool DashboardWriteBlocksJsonArtifact(
    const std::wstring& path,
    const std::vector<OcrLayoutBlock>& blocks)
{
    return !path.empty() &&
        DashboardWriteUtf8TextFileAtomic(path, DashboardBuildBlocksJsonArtifact(blocks));
}

static std::wstring DashboardPreviewEditJournalPath(const std::wstring& contentJsonPath) {
    return contentJsonPath + L".preview-edit.journal";
}

static bool DashboardRecoverPreviewEditJournal(
    const std::wstring& markdownPath,
    const std::wstring& contentJsonPath,
    bool& recovered,
    std::wstring& recoveredMarkdown,
    std::vector<OcrLayoutBlock>& recoveredBlocks)
{
    recovered = false;
    recoveredMarkdown.clear();
    recoveredBlocks.clear();
    const std::wstring journalPath = DashboardPreviewEditJournalPath(contentJsonPath);
    if (!PathFileExistsW(journalPath.c_str())) return true;

    std::wstring journal;
    if (!DashboardReadUtf8TextFile(journalPath, journal)) return false;
    std::wstring updatedDocument = UnescapeJsonString(ExtractJsonField(journal, L"markdownDocument"));
    std::wstring updatedJson = UnescapeJsonString(ExtractJsonField(journal, L"contentJson"));
    std::wstring updatedBlocksJson = UnescapeJsonString(ExtractJsonField(journal, L"blocksJson"));
    if (journal.find(L"\"markdownDocument\"") == std::wstring::npos ||
        journal.find(L"\"contentJson\"") == std::wstring::npos ||
        journal.find(L"\"blocksJson\"") == std::wstring::npos ||
        updatedJson.empty() || updatedBlocksJson.empty() || markdownPath.empty() ||
        updatedJson.find(L"\"markdown\"") == std::wstring::npos ||
        updatedJson.find(L"\"blocks\"") == std::wstring::npos) return false;

    if (!DashboardWriteUtf8TextFileAtomic(contentJsonPath, updatedJson) ||
        !DashboardWriteUtf8TextFileAtomic(markdownPath, updatedDocument) ||
        !DashboardWriteUtf8TextFileAtomic(
            DashboardPathWithSuffix(contentJsonPath, L".blocks.json"), updatedBlocksJson)) {
        return false;
    }
    if (!DeleteFileW(journalPath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) return false;

    std::wstring escapedMarkdown = ExtractJsonField(updatedJson, L"markdown");
    recoveredMarkdown = UnescapeJsonString(escapedMarkdown);
    recoveredBlocks = ParseOcrLayoutBlocks(updatedJson, 0);
    recovered = true;
    return true;
}

static bool DashboardPersistContentJsonBlocks(
    const std::wstring& contentJsonPath,
    const std::vector<OcrLayoutBlock>& blocks)
{
    if (contentJsonPath.empty() || !PathFileExistsW(contentJsonPath.c_str())) return false;
    std::wstring originalJson;
    if (!DashboardReadUtf8TextFile(contentJsonPath, originalJson)) return false;
    std::wstring json = originalJson;
    if (!DashboardReplaceJsonValue(json, L"blocks", OcrLayoutBlocksToJson(blocks, 2))) return false;
    if (!DashboardWriteUtf8TextFileAtomic(contentJsonPath, json)) return false;
    if (!DashboardWriteBlocksJsonArtifact(
            DashboardPathWithSuffix(contentJsonPath, L".blocks.json"), blocks)) {
        DashboardWriteUtf8TextFileAtomic(contentJsonPath, originalJson);
        return false;
    }
    return true;
}

static bool DashboardReadContentJsonMarkdown(
    const std::wstring& contentJsonPath,
    std::wstring& json,
    std::wstring& markdown)
{
    if (contentJsonPath.empty() || !PathFileExistsW(contentJsonPath.c_str())) return false;
    if (!DashboardReadUtf8TextFile(contentJsonPath, json)) return false;
    std::wstring escaped = ExtractJsonField(json, L"markdown");
    if (escaped.empty() && json.find(L"\"markdown\"") == std::wstring::npos) return false;
    markdown = UnescapeJsonString(escaped);
    return true;
}

static bool DashboardBuildContentJsonMarkdown(
    const std::wstring& originalJson,
    const std::wstring& markdown,
    std::wstring& updatedJson)
{
    updatedJson = originalJson;
    return DashboardReplaceJsonValue(
        updatedJson,
        L"markdown",
        L"\"" + EscapeJsonString(markdown) + L"\"");
}

static std::wstring DashboardNormalizeLf(std::wstring text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            out.push_back(L'\n');
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

static std::wstring DashboardLfToCrLf(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + 16);
    for (wchar_t ch : text) {
        if (ch == L'\n') out.push_back(L'\r');
        out.push_back(ch);
    }
    return out;
}

static bool DashboardReplaceNthExact(
    std::wstring& text,
    const std::wstring& needle,
    const std::wstring& replacement,
    size_t occurrence)
{
    if (needle.empty()) return false;
    size_t pos = 0;
    for (size_t index = 0; index <= occurrence; ++index) {
        pos = text.find(needle, pos);
        if (pos == std::wstring::npos) return false;
        if (index < occurrence) pos += needle.size();
    }
    text.replace(pos, needle.size(), replacement);
    return true;
}

static bool DashboardReplaceFirstMarkdownBlockContent(
    std::wstring& markdown,
    const std::wstring& oldContent,
    const std::wstring& newContent,
    size_t occurrence)
{
    if (oldContent.empty()) return false;
    if (DashboardReplaceNthExact(markdown, oldContent, newContent, occurrence)) return true;

    std::wstring oldLf = DashboardNormalizeLf(oldContent);
    std::wstring newLf = DashboardNormalizeLf(newContent);
    if (oldLf.empty()) return false;

    if (markdown.find(L"\r\n") != std::wstring::npos) {
        if (DashboardReplaceNthExact(
                markdown,
                DashboardLfToCrLf(oldLf),
                DashboardLfToCrLf(newLf),
                occurrence)) {
            return true;
        }
    }
    if (DashboardReplaceNthExact(markdown, oldLf, newLf, occurrence)) return true;

    while (!oldLf.empty() && (oldLf.back() == L'\n' || oldLf.back() == L' ' || oldLf.back() == L'\t')) {
        oldLf.pop_back();
    }
    while (!newLf.empty() && (newLf.back() == L'\n' || newLf.back() == L' ' || newLf.back() == L'\t')) {
        newLf.pop_back();
    }
    if (oldLf.empty()) return false;
    if (markdown.find(L"\r\n") != std::wstring::npos) {
        return DashboardReplaceNthExact(
            markdown,
            DashboardLfToCrLf(oldLf),
            DashboardLfToCrLf(newLf),
            occurrence);
    }
    return DashboardReplaceNthExact(markdown, oldLf, newLf, occurrence);
}

std::vector<DashboardOcrBlock> OcrDashboardWindow::BuildBlocksForHistoryItem(const OcrDashboardHistoryItem& item) const {
    std::vector<DashboardOcrBlock> blocks;
    if (!item.blocks.empty()) {
        blocks.reserve(item.blocks.size());
        std::map<int, int> nextOrderByPage;
        for (const auto& src : item.blocks) {
            if (!DashboardRectIsValid(src.bbox)) continue;
            DashboardOcrBlock block;
            block.pageIndex = src.pageIndex;
            block.order = ++nextOrderByPage[block.pageIndex];
            // OWN-124: pure page/block id (WideStringUtils).
            block.id = src.id.empty()
                ? WideFormatPageBlockId(block.pageIndex + 1, block.order)
                : src.id;
            block.label = DashboardNormalizeBlockLabel(src.label);
            block.displayLabel = DashboardBlockDisplayLabel(block.label);
            block.content = src.content;
            block.bbox = src.bbox;
            block.polygon = src.polygon;
            block.confidence = src.confidence;
            block.source = src.source;
            block.groupId = src.groupId;
            block.edited = src.edited;
            block.editBaseline = src.editBaseline;
            if (block.polygon.empty()) {
                RECT r = block.bbox;
                block.polygon = {
                    {(float)r.left, (float)r.top},
                    {(float)r.right, (float)r.top},
                    {(float)r.right, (float)r.bottom},
                    {(float)r.left, (float)r.bottom}
                };
            }
            blocks.push_back(std::move(block));
        }
        return blocks;
    }

    blocks.reserve(item.bboxes.size());
    for (size_t i = 0; i < item.bboxes.size(); ++i) {
        RECT r = item.bboxes[i];
        if (!DashboardRectIsValid(r)) continue;
        std::wstring label = i < item.bboxClasses.size() ? item.bboxClasses[i] : L"text";
        label = DashboardNormalizeBlockLabel(label);

        DashboardOcrBlock block;
        // OWN-124: pure page/bbox id (WideStringUtils).
        block.id = WideFormatPageBboxId((int)blocks.size() + 1);
        block.pageIndex = 0;
        block.order = (int)blocks.size() + 1;
        block.label = label;
        block.displayLabel = DashboardBlockDisplayLabel(label);
        block.content = L"";
        block.bbox = r;
        block.polygon = {
            {(float)r.left, (float)r.top},
            {(float)r.right, (float)r.top},
            {(float)r.right, (float)r.bottom},
            {(float)r.left, (float)r.bottom}
        };
        block.source = L"layout_overlay";
        blocks.push_back(std::move(block));
    }
    return blocks;
}

int OcrDashboardWindow::CountBlockIssues(const DashboardOcrBlock& block) const {
    size_t index = m_canvas.blockRuntimeIndex.FindById(block.id);
    return index == DashboardBlockRuntimeIndex::npos ? 0 : m_canvas.blockRuntimeIndex.IssueCount(index);
}

bool OcrDashboardWindow::BlockHasIssue(const DashboardOcrBlock& block) const {
    return CountBlockIssues(block) > 0;
}

void OcrDashboardWindow::RefreshCurrentBlocks() {
    std::vector<DashboardOcrBlock> next;
    if (const auto* history = m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState))) {
        DashboardStateSetPreviewPersistenceBlocked(m_dashboardState, false);
        next = BuildBlocksForHistoryItem(*history);
    } else if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        auto taskIt = std::find_if(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
            [&](const DashboardBatchTaskItem& task) {
                return IsImageTaskSelectionForTask(task);
            });
        if (taskIt != m_batch.batchTasks.end()) {
            bool recovered = false;
            std::wstring recoveredMarkdown;
            std::vector<OcrLayoutBlock> recoveredBlocks;
            if (!DashboardRecoverPreviewEditJournal(
                    taskIt->job.markdownPath,
                    taskIt->job.contentJsonPath,
                    recovered,
                    recoveredMarkdown,
                    recoveredBlocks)) {
                DashboardStateSetPreviewPersistenceBlocked(m_dashboardState, true);
            } else {
                DashboardStateSetPreviewPersistenceBlocked(m_dashboardState, false);
                if (recovered) taskIt->job.blocks = std::move(recoveredBlocks);
            }
            OcrDashboardHistoryItem item;
            item.blocks = taskIt->job.blocks;
            item.bboxes.clear();
            item.bboxClasses.clear();
            next = BuildBlocksForHistoryItem(item);
        }
    } else if (DashboardStateHasPdfSelection(m_dashboardState)) {
        const int overlayPageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0
            ? DashboardStatePdfSelectionPageIndex(m_dashboardState)
            : 1;
        auto jobIt = std::find_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
            [&](const BatchOcrPdfJob& job) {
                DashboardPdfSelectionKey key;
                key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
                key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
                key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
                key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
                return DashboardSamePdfSelectionKey(job, key);
            });
        if (jobIt != m_batch.activePdfJobs.end()) {
            auto pageIt = std::find_if(jobIt->pages.begin(), jobIt->pages.end(),
                [&](const BatchOcrPdfPageJob& page) {
                    return page.pageIndex == overlayPageIndex;
                });
            // Pure dual-write is read authority for canvas image path.
            const std::wstring& canvasPath = DashboardStateCanvasImagePath(m_dashboardState);
            const bool rootCanvasMatchesPage =
                DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0 ||
                (pageIt != jobIt->pages.end() &&
                    !pageIt->sourceImagePath.empty() &&
                    !canvasPath.empty() &&
                    WideEqualsNoCase(pageIt->sourceImagePath, canvasPath));
            if (pageIt != jobIt->pages.end() && rootCanvasMatchesPage) {
                bool recovered = false;
                std::wstring recoveredMarkdown;
                std::vector<OcrLayoutBlock> recoveredBlocks;
                if (!DashboardRecoverPreviewEditJournal(
                        pageIt->markdownPath,
                        pageIt->contentJsonPath,
                        recovered,
                        recoveredMarkdown,
                        recoveredBlocks)) {
                    DashboardStateSetPreviewPersistenceBlocked(m_dashboardState, true);
                } else {
                    DashboardStateSetPreviewPersistenceBlocked(m_dashboardState, false);
                    if (recovered) {
                        pageIt->markdown = std::move(recoveredMarkdown);
                        pageIt->blocks = std::move(recoveredBlocks);
                    }
                }
                OcrDashboardHistoryItem item;
                item.blocks = pageIt->blocks;
                next = BuildBlocksForHistoryItem(item);
            } else if (DashboardStatePdfSelectionPageIndex(m_dashboardState) <= 0) {
                // A cover without a rendered/OCR'd Page 1 remains a bitmap-only
                // document preview. Never reuse blocks from the first selected
                // OCR range page (for example Page 5 in a 5-10 range).
                DashboardStateSetPreviewPersistenceBlocked(m_dashboardState, false);
            }
        }
    }
    m_canvas.currentBlocks = std::move(next);
    RebuildBlockRuntimeIndex();
    // D-G-3: block selection/hover sole authority is DashboardState.
    if (!DashboardStateHoveredBlockId(m_dashboardState).empty() && !FindCurrentBlockById(DashboardStateHoveredBlockId(m_dashboardState))) {
        DashboardStateClearHoveredBlockId(m_dashboardState);
    }
    if (!DashboardStateSelectedBlockId(m_dashboardState).empty() && !FindCurrentBlockById(DashboardStateSelectedBlockId(m_dashboardState))) {
        DashboardStateClearSelectedBlockId(m_dashboardState);
    }
    if (m_imageArea && IsWindow(m_imageArea)) InvalidateRect(m_imageArea, nullptr, FALSE);
}

void OcrDashboardWindow::RebuildBlockRuntimeIndex() {
    std::set<std::wstring> seenIds;
    m_canvas.currentBlocks.erase(
        std::remove_if(m_canvas.currentBlocks.begin(), m_canvas.currentBlocks.end(),
            [&](const DashboardOcrBlock& block) {
                return block.id.empty() || !seenIds.insert(block.id).second;
            }),
        m_canvas.currentBlocks.end());
    LONG imageArea = 0;
    if (m_gdiplusImageFull || m_gdiplusImage) {
        auto* image = static_cast<Gdiplus::Image*>(
            m_gdiplusImageFull ? m_gdiplusImageFull : m_gdiplusImage);
        if (image) {
            ULONGLONG area = static_cast<ULONGLONG>(image->GetWidth()) * image->GetHeight();
            imageArea = area > static_cast<ULONGLONG>(LONG_MAX) ? LONG_MAX : static_cast<LONG>(area);
        }
    }
    // IDs are now non-empty and unique, so every runtime surface resolves the
    // same vector element. Keep the helper's duplicate guard as a final invariant.
    m_canvas.blockRuntimeIndex.Rebuild(m_canvas.currentBlocks, imageArea);
}

const DashboardOcrBlock* OcrDashboardWindow::FindCurrentBlockById(const std::wstring& id) const {
    // D-G-1: CanvasModel owns blocks + runtime index.
    return m_canvas.findById(id);
}

const DashboardOcrBlock* OcrDashboardWindow::ResolveBlockContentOwner(
    const DashboardOcrBlock& selected) const
{
    const size_t selectedIndex = m_canvas.blockRuntimeIndex.FindById(selected.id);
    const size_t ownerIndex = m_canvas.blockRuntimeIndex.ContentOwnerIndex(
        selectedIndex, m_canvas.currentBlocks);
    return ownerIndex < m_canvas.currentBlocks.size()
        ? &m_canvas.currentBlocks[ownerIndex] : &selected;
}

int OcrDashboardWindow::HitTestImageBlock(int x, int y) const {
    // D-G-1: pure free hit-tester; Host only gates overlay visibility.
    if (!DashboardStateShowLayoutOverlay(m_dashboardState) ||
        m_canvas.currentBlocks.empty() ||
        !DashboardStateHasCanvasZoom(m_dashboardState)) {
        return -1;
    }
    return DashboardCanvasHitTestBlockClient(
        m_canvas.currentBlocks,
        DashboardStateCanvasViewOf(m_dashboardState),
        x,
        y);
}

RECT OcrDashboardWindow::GetImageBlockCopyButtonRect(
    const DashboardOcrBlock& block,
    int imageAreaW,
    int imageAreaH) const
{
    RECT r = block.bbox;
    int bx = (int)(DashboardStateCanvasPanX(m_dashboardState) + r.left * DashboardStateCanvasZoom(m_dashboardState));
    int by = (int)(DashboardStateCanvasPanY(m_dashboardState) + r.top * DashboardStateCanvasZoom(m_dashboardState));
    int bw = (int)((r.right - r.left) * DashboardStateCanvasZoom(m_dashboardState));
    int bh = (int)((r.bottom - r.top) * DashboardStateCanvasZoom(m_dashboardState));
    RECT blockRc = {bx, by, bx + max(1, bw), by + max(1, bh)};

    int buttonW = Scale(56);
    int buttonH = Scale(24);
    int gap = Scale(8);
    int margin = Scale(3);
    POINT center = DashboardRectCenter(blockRc);
    struct Candidate {
        int x;
        int y;
    };
    Candidate candidates[] = {
        {blockRc.right + gap, center.y - buttonH / 2},
        {center.x - buttonW / 2, blockRc.bottom + gap},
        {center.x - buttonW / 2, blockRc.top - buttonH - gap},
        {blockRc.left - buttonW - gap, center.y - buttonH / 2},
        {blockRc.right + gap, blockRc.bottom + gap},
        {blockRc.right + gap, blockRc.top - buttonH - gap},
        {blockRc.left - buttonW - gap, blockRc.bottom + gap},
        {blockRc.left - buttonW - gap, blockRc.top - buttonH - gap},
        {blockRc.right - buttonW, blockRc.top - buttonH - Scale(4)},
    };

    RECT best = {margin, margin, margin + buttonW, margin + buttonH};
    LONGLONG bestScore = 0x7fffffffffffffffLL;
    RECT expandedBlock = DashboardInflateRectValue(blockRc, Scale(2), Scale(2));
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        RECT candidate = {candidates[i].x, candidates[i].y,
            candidates[i].x + buttonW, candidates[i].y + buttonH};
        candidate = DashboardClampRectToBounds(candidate, imageAreaW, imageAreaH, margin);
        LONG overlap = DashboardRectIntersectionArea(candidate, expandedBlock);
        LONG distance = DashboardDistanceSquared(DashboardRectCenter(candidate), DashboardRectCenter(blockRc));
        LONGLONG score = (LONGLONG)overlap * 1000000LL + (LONGLONG)distance + i;
        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

int OcrDashboardWindow::HitTestImageBlockCopyButton(int x, int y) const {
    if (!DashboardStateShowLayoutOverlay(m_dashboardState) || m_canvas.currentBlocks.empty()) return -1;
    RECT rc = {};
    if (m_imageArea) GetClientRect(m_imageArea, &rc);
    int w = max(1, rc.right - rc.left);
    int h = max(1, rc.bottom - rc.top);

    for (int i = (int)m_canvas.currentBlocks.size() - 1; i >= 0; --i) {
        const auto& block = m_canvas.currentBlocks[(size_t)i];
        if (block.id != DashboardStateHoveredBlockId(m_dashboardState) && block.id != DashboardStateSelectedBlockId(m_dashboardState)) continue;
        RECT copyRc = GetImageBlockCopyButtonRect(block, w, h);
        if (DashboardPointInRectInclusive(copyRc, x, y)) {
            return i;
        }
    }
    return -1;
}

bool OcrDashboardWindow::ShouldPreserveImageBlockCopyHover(int x, int y) const {
    if (!DashboardStateShowLayoutOverlay(m_dashboardState) || m_canvas.currentBlocks.empty()) return false;
    const DashboardOcrBlock* block = nullptr;
    if (!DashboardStateHoveredBlockId(m_dashboardState).empty()) block = FindCurrentBlockById(DashboardStateHoveredBlockId(m_dashboardState));
    if (!block && !DashboardStateSelectedBlockId(m_dashboardState).empty()) block = FindCurrentBlockById(DashboardStateSelectedBlockId(m_dashboardState));
    if (!block) return false;

    RECT rc = {};
    if (m_imageArea) GetClientRect(m_imageArea, &rc);
    int w = max(1, rc.right - rc.left);
    int h = max(1, rc.bottom - rc.top);
    RECT copyRc = GetImageBlockCopyButtonRect(*block, w, h);
    if (DashboardPointInRectInclusive(DashboardInflateRectValue(copyRc, Scale(6), Scale(6)), x, y)) {
        return true;
    }

    RECT r = block->bbox;
    RECT blockRc = {
        (int)(DashboardStateCanvasPanX(m_dashboardState) + r.left * DashboardStateCanvasZoom(m_dashboardState)),
        (int)(DashboardStateCanvasPanY(m_dashboardState) + r.top * DashboardStateCanvasZoom(m_dashboardState)),
        (int)(DashboardStateCanvasPanX(m_dashboardState) + r.right * DashboardStateCanvasZoom(m_dashboardState)),
        (int)(DashboardStateCanvasPanY(m_dashboardState) + r.bottom * DashboardStateCanvasZoom(m_dashboardState))
    };
    RECT bridge = DashboardInflateRectValue(DashboardUnionRectValue(blockRc, copyRc), Scale(8), Scale(8));
    return DashboardPointInRectInclusive(bridge, x, y);
}

void OcrDashboardWindow::SetHoveredBlock(const std::wstring& id) {
    // D-G-3: hovered block sole authority is DashboardState.
    if (DashboardStateHoveredBlockId(m_dashboardState) == id) return;
    DashboardStateSetHoveredBlockId(m_dashboardState, id);
    if (m_dashboardState.hotImageBlockCopyButtonId != id) {
        m_dashboardState.hotImageBlockCopyButtonId.clear();
    }
    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, FALSE);
    if (DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Preview && m_previewHost) {
        m_previewHost->SetHoveredBlock(id);
    }
    if (m_translationPreviewHost) {
        m_translationPreviewHost->SetHoveredBlock(id);
    }
}

void OcrDashboardWindow::SetSelectedBlock(const std::wstring& id, bool ensureVisible) {
    // D-G-3: selected block sole authority is DashboardState.
    if (DashboardStateSelectedBlockId(m_dashboardState) == id) {
        if (ensureVisible) {
            if (DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Preview && m_previewHost) {
                m_previewHost->SetSelectedBlock(id, true);
            }
            if (m_translationPreviewHost) {
                m_translationPreviewHost->SetSelectedBlock(id, true);
            }
        }
        return;
    }
    DashboardStateSetSelectedBlockId(m_dashboardState, id);
    if (!DashboardStateSelectedBlockId(m_dashboardState).empty()) {
        const DashboardOcrBlock* block = FindCurrentBlockById(DashboardStateSelectedBlockId(m_dashboardState));
        if (block) {
            const DashboardOcrBlock* owner = ResolveBlockContentOwner(*block);
            const std::wstring& content = owner ? owner->content : block->content;
            std::wstring preview = content.empty()
                // OWN-124: pure bbox LTRB label (WideStringUtils).
                ? (L"bbox " + WideFormatBboxLtrb(block->bbox.left, block->bbox.top,
                    block->bbox.right, block->bbox.bottom))
                : DashboardBlockPreviewText(content, 80);
            std::wstring ownerHint;
            if (owner && owner != block) {
                // OWN-124: pure int label (WideStringUtils).
                ownerHint = (S::IsChinese() ? L" · 内容归属 #" : L" · content owner #") +
                    WideFormatIntLabel(owner->order);
            }
            // OWN-120: pure ann id label (WideStringUtils).
            UpdateStatus(WideFormatAnnId(block->order) + L" " +
                block->displayLabel + ownerHint + L" · " + preview);
        }
    }
    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, FALSE);
    if (DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Preview && m_previewHost) {
        m_previewHost->SetSelectedBlock(id, ensureVisible);
    }
    if (m_translationPreviewHost) {
        m_translationPreviewHost->SetSelectedBlock(id, ensureVisible);
    }
}

void OcrDashboardWindow::CenterSelectedBlockInImage(bool onlyIfOffscreen) {
    if (DashboardStateSelectedBlockId(m_dashboardState).empty() || !m_imageArea || !m_gdiplusImage) return;
    const DashboardOcrBlock* block = FindCurrentBlockById(DashboardStateSelectedBlockId(m_dashboardState));
    if (!block) return;

    RECT rc = {};
    GetClientRect(m_imageArea, &rc);
    int viewW = rc.right - rc.left;
    int viewH = rc.bottom - rc.top;
    if (viewW <= 0 || viewH <= 0) return;

    // 块在屏幕坐标下的位置（用当前 zoom/pan 投影）。
    int screenLeft = (int)(DashboardStateCanvasPanX(m_dashboardState) + (float)block->bbox.left * DashboardStateCanvasZoom(m_dashboardState));
    int screenTop = (int)(DashboardStateCanvasPanY(m_dashboardState) + (float)block->bbox.top * DashboardStateCanvasZoom(m_dashboardState));
    int screenRight = (int)(DashboardStateCanvasPanX(m_dashboardState) + (float)block->bbox.right * DashboardStateCanvasZoom(m_dashboardState));
    int screenBottom = (int)(DashboardStateCanvasPanY(m_dashboardState) + (float)block->bbox.bottom * DashboardStateCanvasZoom(m_dashboardState));
    int margin = Scale(16);
    bool offscreen = (screenRight < margin || screenBottom < margin ||
                      screenLeft > viewW - margin || screenTop > viewH - margin);
    if (onlyIfOffscreen && !offscreen) return;

    // 块中心在图片坐标系。
    float blockCX = ((float)block->bbox.left + (float)block->bbox.right) / 2.0f;
    float blockCY = ((float)block->bbox.top + (float)block->bbox.bottom) / 2.0f;

    // 若块在当前 zoom 下太小（< 视口 35%），适度放大让用户能看清; 只放大、不缩小，避免破坏用户已缩放状态。
    int blockW = block->bbox.right - block->bbox.left;
    int blockH = block->bbox.bottom - block->bbox.top;
    if (blockW > 0 && blockH > 0) {
        float targetZoomX = (float)viewW * 0.45f / (float)blockW;
        float targetZoomY = (float)viewH * 0.45f / (float)blockH;
        float targetZoom = (std::min)(targetZoomX, targetZoomY);
        targetZoom = (std::min)(targetZoom, 8.0f); // 上限 8x，避免对极小块过度放大
        // Write path: intermediate math uses legacy authority, then dual-write pure.
        if (targetZoom > m_dashboardState.canvasView.zoom) {
            m_dashboardState.canvasView.zoom = targetZoom;
        }
    }

    m_dashboardState.canvasView.panX = (float)viewW / 2.0f - blockCX * m_dashboardState.canvasView.zoom;
    m_dashboardState.canvasView.panY = (float)viewH / 2.0f - blockCY * m_dashboardState.canvasView.zoom;
    m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
    InvalidateRect(m_imageArea, nullptr, FALSE);
    ShowZoomHud();
}

void OcrDashboardWindow::CopySelectedBlockToClipboard() {
    const DashboardOcrBlock* block = FindCurrentBlockById(DashboardStateSelectedBlockId(m_dashboardState));
    if (!block && !DashboardStateHoveredBlockId(m_dashboardState).empty()) block = FindCurrentBlockById(DashboardStateHoveredBlockId(m_dashboardState));
    if (!block) return;

    const DashboardOcrBlock* owner = ResolveBlockContentOwner(*block);
    std::wstring text = owner ? owner->content : block->content;
    if (text.empty()) {
        // OWN-120: pure ann id label (WideStringUtils).
        text = WideFormatAnnId(block->order) + L" " + block->displayLabel;
    }
    if (CopyTextToClipboard(m_hwnd, text)) {
        UpdateStatus(S::IsChinese() ? L"已复制当前块" : L"Copied current block");
        if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1600, nullptr);
    }
}

bool OcrDashboardWindow::ApplyPreviewBlockEdit(
    const std::wstring& id,
    const std::wstring& content,
    const DashboardSourceEditRequest& sourceEdit)
{
    if (DashboardStateIsPreviewPersistenceBlocked(m_dashboardState)) {
        DashboardStateSetPreviewEditRollbackFailed(m_dashboardState, true);
        UpdateStatus(S::IsChinese()
            ? L"上次保存事务尚未恢复，已阻止继续编辑"
            : L"A previous save transaction still requires recovery; further edits are blocked");
        return false;
    }
    DashboardStateSetPreviewEditRollbackFailed(m_dashboardState, false);
    if (id.empty()) return false;

    const DashboardOcrBlock* selected = FindCurrentBlockById(id);
    if (!selected) return false;
    const DashboardOcrBlock* resolvedOwner = ResolveBlockContentOwner(*selected);
    const std::wstring targetId = resolvedOwner ? resolvedOwner->id : id;

    std::vector<DashboardOcrBlock> originalBlocks = m_canvas.currentBlocks;
    bool found = false;
    bool changed = false;
    for (auto& block : m_canvas.currentBlocks) {
        if (block.id != targetId) continue;
        found = true;
        if (block.content != content || !block.edited) {
            // Capture the immutable OCR baseline exactly once.  Legacy edited
            // records without a baseline deliberately remain non-restorable;
            // treating their current content as OCR output would be dishonest.
            if (!block.edited && !block.editBaseline.has_value()) {
                OcrBlockEditBaseline baseline;
                baseline.content = block.content;
                baseline.sourceSegment = sourceEdit.expectedSource;
                baseline.canonicalSource = sourceEdit.canonicalSource;
                block.editBaseline = std::move(baseline);
            }
            block.content = content;
            block.edited = true;
            changed = true;
        }
        break;
    }
    if (!found) return false;

    // D-G-3: selected block sole authority is DashboardState.
    DashboardStateSetSelectedBlockId(m_dashboardState, id);
    bool persisted = !changed ||
        PersistPreviewMarkdownEdit(content, sourceEdit);

    if (!persisted) {
        m_canvas.currentBlocks = std::move(originalBlocks);
    }
    RebuildBlockRuntimeIndex();

    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, FALSE);

    UpdateStatus(persisted
        ? (S::IsChinese() ? L"预览块已更新" : L"Preview block updated")
        : (DashboardStateIsPreviewEditRollbackFailed(m_dashboardState)
            ? (S::IsChinese() ? L"保存失败且自动回滚未完整完成，请立即检查输出文件" :
                L"Save failed and automatic rollback was incomplete; inspect the output files")
            : (S::IsChinese() ? L"预览块保存失败，原内容已保留" :
                L"Preview block save failed; the original content was preserved")));
    if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1600, nullptr);
    return persisted;
}

bool OcrDashboardWindow::RestorePreviewBlockOriginal(
    const std::wstring& id,
    const DashboardSourceEditRequest& sourceEdit)
{
    if (DashboardStateIsPreviewPersistenceBlocked(m_dashboardState)) {
        DashboardStateSetPreviewEditRollbackFailed(m_dashboardState, true);
        UpdateStatus(S::IsChinese()
            ? L"上次保存事务尚未恢复，已阻止还原"
            : L"A previous save transaction still requires recovery; restore is blocked");
        return false;
    }
    DashboardStateSetPreviewEditRollbackFailed(m_dashboardState, false);
    if (id.empty()) return false;

    const DashboardOcrBlock* selected = FindCurrentBlockById(id);
    if (!selected) return false;
    const DashboardOcrBlock* resolvedOwner = ResolveBlockContentOwner(*selected);
    const std::wstring targetId = resolvedOwner ? resolvedOwner->id : id;

    std::vector<DashboardOcrBlock> originalBlocks = m_canvas.currentBlocks;
    OcrBlockEditBaseline baseline;
    bool found = false;
    for (auto& block : m_canvas.currentBlocks) {
        if (block.id != targetId) continue;
        if (!block.edited || !block.editBaseline.has_value()) return false;
        baseline = *block.editBaseline;
        if (baseline.sourceSegment.empty() ||
            baseline.canonicalSource != sourceEdit.canonicalSource) return false;
        block.content = baseline.content;
        block.edited = false;
        block.editBaseline.reset();
        found = true;
        break;
    }
    if (!found) return false;

    // D-G-3: selected block sole authority is DashboardState.
    DashboardStateSetSelectedBlockId(m_dashboardState, id);
    bool persisted = PersistPreviewMarkdownEdit(baseline.sourceSegment, sourceEdit);
    if (!persisted) m_canvas.currentBlocks = std::move(originalBlocks);
    RebuildBlockRuntimeIndex();
    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, FALSE);

    UpdateStatus(persisted
        ? (S::IsChinese() ? L"已还原为最初 OCR 结果" : L"Restored original OCR result")
        : (DashboardStateIsPreviewEditRollbackFailed(m_dashboardState)
            ? (S::IsChinese() ? L"还原失败且自动回滚未完整完成，请立即检查输出文件" :
                L"Restore failed and automatic rollback was incomplete; inspect the output files")
            : (S::IsChinese() ? L"无法还原，当前编辑内容已保留" :
                L"Restore failed; the current edited content was preserved")));
    if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
    return persisted;
}

bool OcrDashboardWindow::PersistPreviewMarkdownEdit(
    const std::wstring& newContent,
    const DashboardSourceEditRequest& sourceEdit)
{
    std::vector<OcrLayoutBlock> layoutBlocks =
        DashboardLayoutBlocksFromDashboardBlocks(m_canvas.currentBlocks);

    auto updateMarkdownText = [&](std::wstring& markdown) -> bool {
        std::wstring updated;
        if (!DashboardSourceMap::ApplyStrict(markdown, sourceEdit, newContent, updated)) return false;
        markdown = std::move(updated);
        return true;
    };

    auto updateMarkdownArtifacts = [&](const std::wstring& markdownPath,
                                       const std::wstring& contentJsonPath,
                                       std::wstring* memoryMarkdown) -> bool {
        if (markdownPath.empty() || contentJsonPath.empty()) return false;

        std::wstring originalJson;
        std::wstring jsonMarkdown;
        if (!DashboardReadContentJsonMarkdown(contentJsonPath, originalJson, jsonMarkdown)) return false;

        std::wstring originalBody = memoryMarkdown && !memoryMarkdown->empty()
            ? *memoryMarkdown
            : jsonMarkdown;
        std::wstring updatedBody = originalBody;
        if (!updateMarkdownText(updatedBody)) return false;

        std::wstring originalDocument;
        if (!DashboardReadUtf8TextFile(markdownPath, originalDocument)) return false;
        std::wstring updatedDocument = originalDocument;
        if (!DashboardReplaceFirstMarkdownBlockContent(
                updatedDocument,
                originalBody,
                updatedBody,
                0)) {
            return false;
        }

        std::wstring updatedJson;
        if (!DashboardBuildContentJsonMarkdown(originalJson, updatedBody, updatedJson) ||
            !DashboardReplaceJsonValue(updatedJson, L"blocks", OcrLayoutBlocksToJson(layoutBlocks, 2))) {
            return false;
        }

        const std::wstring updatedBlocksJson = DashboardBuildBlocksJsonArtifact(layoutBlocks);
        const std::wstring journalPath = DashboardPreviewEditJournalPath(contentJsonPath);
        std::wstring journal = L"{\n  \"version\": 1,\n";
        journal += L"  \"markdownDocument\": \"" + EscapeJsonString(updatedDocument) + L"\",\n";
        journal += L"  \"contentJson\": \"" + EscapeJsonString(updatedJson) + L"\",\n";
        journal += L"  \"blocksJson\": \"" + EscapeJsonString(updatedBlocksJson) + L"\"\n}\n";
        if (!DashboardWriteUtf8TextFileAtomic(journalPath, journal)) return false;

        auto finishRollback = [&](bool markdownRolledBack, bool jsonRolledBack) {
            if (!markdownRolledBack || !jsonRolledBack) {
                DashboardStateSetPreviewEditRollbackFailed(m_dashboardState, true);
                DashboardStateSetPreviewPersistenceBlocked(m_dashboardState, true);
                return;
            }
            DeleteFileW(journalPath.c_str());
        };

        if (!DashboardWriteUtf8TextFileAtomic(contentJsonPath, updatedJson)) {
            DeleteFileW(journalPath.c_str());
            return false;
        }
        if (!DashboardWriteUtf8TextFileAtomic(markdownPath, updatedDocument)) {
            finishRollback(true, DashboardWriteUtf8TextFileAtomic(contentJsonPath, originalJson));
            return false;
        }
        if (!DashboardWriteUtf8TextFileAtomic(
                DashboardPathWithSuffix(contentJsonPath, L".blocks.json"), updatedBlocksJson)) {
            bool markdownRolledBack = DashboardWriteUtf8TextFileAtomic(markdownPath, originalDocument);
            bool jsonRolledBack = DashboardWriteUtf8TextFileAtomic(contentJsonPath, originalJson);
            finishRollback(markdownRolledBack, jsonRolledBack);
            return false;
        }

        DeleteFileW(journalPath.c_str());

        if (memoryMarkdown) *memoryMarkdown = updatedBody;
        return true;
    };

    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        auto it = std::find_if(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
            [&](const DashboardBatchTaskItem& task) {
                return IsImageTaskSelectionForTask(task);
            });
        if (it == m_batch.batchTasks.end()) return false;
        int linkedHistoryIndex = FindLinkedHistoryIndexForImageTask(it->job);
        const bool taskArtifactsWritable =
            it->status == BatchOcrTaskStatus::Completed &&
            !it->job.markdownPath.empty() &&
            !it->job.contentJsonPath.empty() &&
            PathFileExistsW(it->job.markdownPath.c_str()) &&
            PathFileExistsW(it->job.contentJsonPath.c_str());
        if (!taskArtifactsWritable &&
            m_history.model.itemAt(linkedHistoryIndex) != nullptr) {
            OcrDashboardHistoryItem originalHistory =
                m_history.model.items[static_cast<size_t>(linkedHistoryIndex)];
            std::vector<OcrLayoutBlock> originalTaskBlocks = it->job.blocks;
            OcrDashboardHistoryItem updatedHistory = originalHistory;
            if (!updateMarkdownText(updatedHistory.text)) return false;
            updatedHistory.blocks = layoutBlocks;
            m_history.model.items[static_cast<size_t>(linkedHistoryIndex)] = std::move(updatedHistory);
            it->job.blocks = layoutBlocks;
            if (DashboardHistorySessionSaveItems(m_history, m_dashboardState)) {
                DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
                return true;
            }
            m_history.model.items[static_cast<size_t>(linkedHistoryIndex)] = std::move(originalHistory);
            it->job.blocks = std::move(originalTaskBlocks);
            DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
            return false;
        }
        if (!updateMarkdownArtifacts(it->job.markdownPath, it->job.contentJsonPath, nullptr)) return false;
        it->job.blocks = layoutBlocks;
        return true;
    }

    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        auto jobIt = std::find_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
            [&](const BatchOcrPdfJob& job) {
                return DashboardSamePdfSelectionKey(job, key);
            });
        if (jobIt == m_batch.activePdfJobs.end()) return false;
        const int editPageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0
            ? DashboardStatePdfSelectionPageIndex(m_dashboardState)
            : 1;
        if (editPageIndex <= 0) return false;
        auto pageIt = std::find_if(jobIt->pages.begin(), jobIt->pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == editPageIndex;
            });
        if (pageIt == jobIt->pages.end()) return false;
        if (!updateMarkdownArtifacts(pageIt->markdownPath, pageIt->contentJsonPath, &pageIt->markdown)) return false;
        pageIt->blocks = layoutBlocks;
        return true;
    }

    if (m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState)) != nullptr) {
        // Content authority remains Window vector; dual-write via SaveHistory Sync.
        auto& item = m_history.model.items[(size_t)DashboardStateSelectedHistoryIndex(m_dashboardState)];
        OcrDashboardHistoryItem originalItem = item;
        OcrDashboardHistoryItem updatedItem = item;
        if (!updateMarkdownText(updatedItem.text)) return false;
        updatedItem.blocks = layoutBlocks;
        item = std::move(updatedItem);
        if (DashboardHistorySessionSaveItems(m_history, m_dashboardState)) {
            DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
            return true;
        }
        item = std::move(originalItem);
        DashboardHistorySessionSyncSelection(m_history, m_dashboardState);
        return false;
    }

    return false;
}
