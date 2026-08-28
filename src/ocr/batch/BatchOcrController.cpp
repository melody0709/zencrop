#include "BatchOcrController.h"
#include "BatchOcrWriter.h"
#include "dashboard/DashboardFileTypes.h"
#include "core/WideStringUtils.h"

#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <set>

// Batch default empty stem is "image" (preview uses "document"); compose pure helpers.
static std::wstring SanitizePathSegment(const std::wstring& input) {
    std::wstring result = DashboardSanitizePathSegment(input);
    result = DashboardTrimPreviewStem(result);
    if (result.empty()) result = L"image";
    if (result.size() > 80) result.resize(80);
    return result;
}

// OWN-73: pure leaf + strip final extension, then batch sanitize (empty → "image").
static std::wstring FileStem(const std::wstring& path) {
    std::wstring name = DashboardFileNameFromPath(path);
    if (name.empty()) name = path;
    name = DashboardStripFinalExtension(std::move(name));
    return SanitizePathSegment(name);
}

// OWN-72: thin wrappers over pure DashboardFileTypes naming helpers.
static std::wstring FormatIndexName(int index) {
    return DashboardFormatImageIndexName(index);
}

static std::wstring FormatPageName(int pageIndex) {
    return DashboardFormatPageIndexName(pageIndex);
}

static std::wstring AppendDuplicateSuffix(const std::wstring& base, int suffix) {
    return DashboardAppendPreviewDuplicateSuffix(base, suffix);
}

static std::wstring NowLocalTimestamp() {
    // OWN-110: pure date/time format (WideStringUtils).
    SYSTEMTIME st;
    GetLocalTime(&st);
    return WideFormatDateTimeParts(
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static std::wstring DirectoryCreateError(DWORD err) {
    // OWN-127: pure job-dir error (WideStringUtils).
    return WideFormatJobDirError(err);
}

enum class DirectoryClaimResult {
    Created,
    AlreadyExists,
    Failed
};

static DirectoryClaimResult ClaimNewDirectory(const std::wstring& dir, std::wstring& error) {
    error.clear();
    if (CreateDirectoryW(dir.c_str(), nullptr)) {
        return DirectoryClaimResult::Created;
    }
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS) {
        return DirectoryClaimResult::AlreadyExists;
    }
    error = DirectoryCreateError(err);
    return DirectoryClaimResult::Failed;
}

// 递归删除目录及其内容（仅在批创建失败回滚路径上使用）。
// 失败时静默忽略——回滚本身不能因失败而中断。
static void RemoveDirectoryRecursive(const std::wstring& path) {
    if (path.empty()) return;
    // OWN-119: pure path join (WideStringUtils).
    std::wstring pattern = WideJoinPath(path, L"*");
    WIN32_FIND_DATAW fd = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(path.c_str());
        return;
    }
    do {
        if (WideEquals(fd.cFileName, L".") || WideEquals(fd.cFileName, L"..")) continue;
        std::wstring child = WideJoinPath(path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RemoveDirectoryRecursive(child);
        } else {
            DeleteFileW(child.c_str());
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    RemoveDirectoryW(path.c_str());
}

// RAII guard：在批创建失败时回滚已创建的 job 目录。
// committed = true 时析构不做任何事（成功路径）。
struct JobDirRollback {
    std::vector<std::wstring> createdDirs;
    bool committed = false;
    ~JobDirRollback() {
        if (committed) return;
        for (auto it = createdDirs.rbegin(); it != createdDirs.rend(); ++it) {
            RemoveDirectoryRecursive(*it);
        }
    }
};

bool BatchOcrController::CreateImageJobs(
    const std::vector<std::wstring>& imageFiles,
    const std::wstring& outputRoot,
    std::vector<BatchOcrImageJob>& jobs,
    std::wstring& error,
    const std::wstring& engineMode,
    const OcrOutputArtifactOptions* outputArtifacts) const
{
    jobs.clear();
    error.clear();

    if (imageFiles.empty()) {
        error = L"No image files selected.";
        return false;
    }
    if (outputRoot.empty()) {
        error = L"No output directory selected.";
        return false;
    }
    if (!BatchOcrWriter::EnsureDirectory(outputRoot)) {
        error = L"Failed to create output directory.";
        return false;
    }

    std::set<std::wstring> reservedLower;
    jobs.reserve(imageFiles.size());

    // RAII 回滚：任意一个 job 创建失败时，递归删除之前已成功创建的 job 目录，
    // 避免 ScanJobs 扫出孤儿 pending manifest 污染状态机。
    JobDirRollback rollback;

    for (size_t i = 0; i < imageFiles.size(); i++) {
        std::wstring base = FileStem(imageFiles[i]);
        if (base.empty()) base = FormatIndexName((int)i + 1);

        std::wstring uniqueName;
        std::wstring jobDir;
        for (int suffix = 1; suffix < 1000; suffix++) {
            uniqueName = AppendDuplicateSuffix(base, suffix);
            jobDir = WideJoinPath(outputRoot, uniqueName);

            // OWN-79: pure lower (WideStringUtils).
            const std::wstring lower = WideToLower(jobDir);
            if (!reservedLower.insert(lower).second) {
                uniqueName.clear();
                jobDir.clear();
                continue;
            }

            std::wstring createError;
            DirectoryClaimResult claim = ClaimNewDirectory(jobDir, createError);
            if (claim == DirectoryClaimResult::Created) {
                break;
            }
            if (claim == DirectoryClaimResult::Failed) {
                error = createError;
                return false;
            }
            uniqueName.clear();
            jobDir.clear();
        }

        if (uniqueName.empty() || jobDir.empty()) {
            error = L"Failed to create a unique output directory name.";
            return false;
        }
        rollback.createdDirs.push_back(jobDir);

        BatchOcrImageJob job;
        job.index = (int)i + 1;
        job.sourceInstanceId = CreateBatchOcrSourceInstanceId();
        if (job.sourceInstanceId.empty()) {
            error = L"Failed to create a stable image source identity.";
            return false;
        }
        job.sourcePath = imageFiles[i];
        job.outputRoot = outputRoot;
        job.outputDir = jobDir;
        job.baseName = uniqueName;
        job.sourceImagePath = WideJoinPath(jobDir, L"source.png");
        job.markdownPath = WideJoinPath(jobDir, uniqueName + L".md");
        job.textPath = WideJoinPath(jobDir, uniqueName + L".txt");
        job.contentJsonPath = WideJoinPath(jobDir, uniqueName + L".content.json");
        job.manifestPath = WideJoinPath(jobDir, L"manifest.json");
        job.createdAt = NowLocalTimestamp();
        job.engineMode = engineMode;
        if (outputArtifacts) {
            job.outputArtifacts = NormalizeOcrOutputArtifactOptions(*outputArtifacts);
        }
        job.status = BatchOcrTaskStatus::Pending;

        BatchOcrWriteResult pending = BatchOcrWriter::WriteImagePending(job);
        if (!pending.success) {
            error = pending.error.empty() ? L"Failed to write pending manifest." : pending.error;
            return false;
        }

        jobs.push_back(std::move(job));
    }

    rollback.committed = true;
    return true;
}

bool BatchOcrController::CreatePdfJob(
    const std::wstring& pdfFile,
    const std::wstring& outputRoot,
    BatchOcrPdfJob& job,
    std::wstring& error,
    const std::wstring& engineMode,
    const OcrOutputArtifactOptions* outputArtifacts) const
{
    job = BatchOcrPdfJob{};
    error.clear();

    if (pdfFile.empty()) {
        error = L"No PDF file selected.";
        return false;
    }
    if (outputRoot.empty()) {
        error = L"No output directory selected.";
        return false;
    }
    if (!BatchOcrWriter::EnsureDirectory(outputRoot)) {
        error = L"Failed to create output directory.";
        return false;
    }

    std::wstring base = FileStem(pdfFile);
    if (base.empty()) base = L"document";

    std::wstring uniqueName;
    std::wstring jobDir;
    for (int suffix = 1; suffix < 1000; suffix++) {
        uniqueName = AppendDuplicateSuffix(base, suffix);
        jobDir = WideJoinPath(outputRoot, uniqueName);
        std::wstring createError;
        DirectoryClaimResult claim = ClaimNewDirectory(jobDir, createError);
        if (claim == DirectoryClaimResult::Created) break;
        if (claim == DirectoryClaimResult::Failed) {
            error = createError;
            return false;
        }
        uniqueName.clear();
        jobDir.clear();
    }

    if (uniqueName.empty() || jobDir.empty()) {
        error = L"Failed to create a unique PDF output directory name.";
        return false;
    }

    // RAII 回滚：PDF job 创建涉及多个子目录与 manifest 写入，失败时整体清理。
    JobDirRollback rollback;
    rollback.createdDirs.push_back(jobDir);

    job.index = 1;
    job.sourcePath = pdfFile;
    job.outputRoot = outputRoot;
    job.outputDir = jobDir;
    job.baseName = uniqueName;
    job.pagesDir = WideJoinPath(jobDir, L"pages");
    job.pageImagesDir = WideJoinPath(jobDir, L"page_images");
    job.assetsDir = WideJoinPath(jobDir, L"assets");
    job.markdownPath = WideJoinPath(jobDir, uniqueName + L".md");
    job.textPath = WideJoinPath(jobDir, uniqueName + L".txt");
    job.contentJsonPath = WideJoinPath(jobDir, uniqueName + L".content.json");
    job.manifestPath = WideJoinPath(jobDir, L"manifest.json");
    job.createdAt = NowLocalTimestamp();
    job.engineMode = engineMode;
    if (outputArtifacts) {
        job.outputArtifacts = NormalizeOcrOutputArtifactOptions(*outputArtifacts);
    }
    job.status = BatchOcrTaskStatus::Pending;

    if (!BatchOcrWriter::EnsureDirectory(job.pagesDir) ||
        !BatchOcrWriter::EnsureDirectory(job.pageImagesDir) ||
        !BatchOcrWriter::EnsureDirectory(job.assetsDir)) {
        error = L"Failed to create PDF job subdirectories.";
        return false;
    }

    BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(job);
    if (!pending.success) {
        error = pending.error.empty() ? L"Failed to write PDF pending manifest." : pending.error;
        return false;
    }

    rollback.committed = true;
    return true;
}

bool BatchOcrController::InitializePdfPages(
    BatchOcrPdfJob& job,
    int pageCount,
    std::wstring& error) const
{
    std::vector<int> pageIndices;
    if (pageCount > 0) {
        pageIndices.reserve((size_t)pageCount);
        for (int i = 1; i <= pageCount; i++) pageIndices.push_back(i);
    }
    job.sourcePageCount = pageCount;
    return InitializePdfPages(job, pageIndices, error);
}

bool BatchOcrController::InitializePdfPages(
    BatchOcrPdfJob& job,
    const std::vector<int>& pageIndices,
    std::wstring& error) const
{
    error.clear();
    if (job.outputDir.empty() || job.pagesDir.empty() || job.pageImagesDir.empty()) {
        error = L"PDF job is not initialized.";
        return false;
    }
    if (pageIndices.empty()) {
        error = L"PDF has no renderable pages.";
        return false;
    }

    if (!BatchOcrWriter::EnsureDirectory(job.pagesDir) ||
        !BatchOcrWriter::EnsureDirectory(job.pageImagesDir) ||
        !BatchOcrWriter::EnsureDirectory(job.assetsDir)) {
        error = L"Failed to create PDF job subdirectories.";
        return false;
    }

    std::vector<int> normalized = pageIndices;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    for (int pageIndex : normalized) {
        if (pageIndex <= 0) {
            error = L"PDF page index must be positive.";
            return false;
        }
    }

    job.pages.clear();
    job.pages.reserve(normalized.size());
    for (int pageIndex : normalized) {
        std::wstring pageName = FormatPageName(pageIndex);
        BatchOcrPdfPageJob page;
        page.pageIndex = pageIndex;
        PdfRenderImageFormat plannedFormat = job.pdfImageFormat == PdfRenderImageFormat::Auto
            ? PdfRenderImageFormat::Png
            : job.pdfImageFormat;
        page.sourceImagePath = WideJoinPath(job.pageImagesDir, pageName + PdfRenderImageFormatExtension(plannedFormat));
        page.imageFormat = plannedFormat;
        page.markdownPath = WideJoinPath(job.pagesDir, pageName + L".md");
        page.textPath = WideJoinPath(job.pagesDir, pageName + L".txt");
        page.contentJsonPath = WideJoinPath(job.pagesDir, pageName + L".json");
        page.status = BatchOcrTaskStatus::Pending;
        job.pages.push_back(std::move(page));
    }
    job.status = BatchOcrTaskStatus::Pending;
    job.elapsedMs = 0;
    job.error.clear();
    if (job.sourcePageCount <= 0) {
        job.sourcePageCount = normalized.back();
    }

    BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(job);
    if (!pending.success) {
        error = pending.error.empty() ? L"Failed to write PDF pending manifest." : pending.error;
        return false;
    }

    return true;
}
