#include "OcrModelInstaller.h"

#include "core/AppDataPaths.h"
#include "core/Sha256.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"
#include "miniz.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kPpOcrV6DictBytes = 74947ULL;
constexpr wchar_t kPpOcrV6DictSha256[] =
    L"b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d";
constexpr wchar_t kLlamaReceiptName[] = L".zencrop-llama-b9128.receipt";

const std::array<const wchar_t*, 22>& LlamaRuntimeFiles()
{
    static const std::array<const wchar_t*, 22> kFiles = {
        L"ggml-base.dll",
        L"ggml-cpu-alderlake.dll",
        L"ggml-cpu-cannonlake.dll",
        L"ggml-cpu-cascadelake.dll",
        L"ggml-cpu-cooperlake.dll",
        L"ggml-cpu-haswell.dll",
        L"ggml-cpu-icelake.dll",
        L"ggml-cpu-ivybridge.dll",
        L"ggml-cpu-piledriver.dll",
        L"ggml-cpu-sandybridge.dll",
        L"ggml-cpu-sapphirerapids.dll",
        L"ggml-cpu-skylakex.dll",
        L"ggml-cpu-sse42.dll",
        L"ggml-cpu-x64.dll",
        L"ggml-cpu-zen4.dll",
        L"ggml-rpc.dll",
        L"ggml.dll",
        L"libomp140.x86_64.dll",
        L"llama-common.dll",
        L"llama.dll",
        L"mtmd.dll",
        L"llama-server.exe",
    };
    return kFiles;
}

bool IsDirectory(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsRegularFile(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool EnsureDirectory(const std::wstring& path, std::wstring& detail)
{
    if (IsDirectory(path)) return true;
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS &&
        result != ERROR_FILE_EXISTS) {
        detail = L"Cannot create directory: " + path + L" (" +
            std::to_wstring(result) + L")";
        return false;
    }
    if (!IsDirectory(path)) {
        detail = L"Directory was not created: " + path;
        return false;
    }
    return true;
}

std::wstring ParentDirectory(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

bool FileSize(const std::wstring& path, std::uint64_t& bytes)
{
    bytes = 0;
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    ULARGE_INTEGER size = {};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    bytes = size.QuadPart;
    return true;
}

bool ReadBinaryFile(
    const std::wstring& path,
    std::vector<unsigned char>& bytes,
    std::wstring& detail)
{
    bytes.clear();
    std::uint64_t fileBytes = 0;
    if (!FileSize(path, fileBytes) || fileBytes > 256ULL * 1024ULL * 1024ULL) {
        detail = L"Archive size is invalid.";
        return false;
    }
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        detail = WideFormatWin32Failed(L"CreateFile(archive)", GetLastError());
        return false;
    }
    try {
        bytes.resize(static_cast<size_t>(fileBytes));
    } catch (...) {
        CloseHandle(file);
        detail = L"Out of memory reading archive.";
        return false;
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>((std::min)(
            bytes.size() - offset, static_cast<size_t>(1024 * 1024)));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &read, nullptr) || read == 0) {
            detail = WideFormatWin32Failed(L"ReadFile(archive)", GetLastError());
            CloseHandle(file);
            bytes.clear();
            return false;
        }
        offset += read;
    }
    CloseHandle(file);
    return true;
}

bool WriteBinaryFile(
    const std::wstring& path,
    const void* data,
    size_t bytes,
    std::wstring& detail)
{
    if (!EnsureDirectory(ParentDirectory(path), detail)) return false;
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        detail = WideFormatWin32Failed(L"CreateFile(output)", GetLastError());
        return false;
    }
    const auto* source = static_cast<const unsigned char*>(data);
    size_t offset = 0;
    while (offset < bytes) {
        const DWORD request = static_cast<DWORD>((std::min)(
            bytes - offset,
            static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!WriteFile(file, source + offset, request, &written, nullptr) || written != request) {
            detail = WideFormatWin32Failed(L"WriteFile(output)", GetLastError());
            CloseHandle(file);
            return false;
        }
        offset += written;
    }
    const bool flushed = FlushFileBuffers(file) != FALSE;
    const DWORD flushError = flushed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!flushed) {
        detail = WideFormatWin32Failed(L"FlushFileBuffers(output)", flushError);
        return false;
    }
    return true;
}

bool WriteTextAtomic(
    const std::wstring& path,
    const std::string& text,
    std::wstring& detail)
{
    const std::wstring temporary = path + L".tmp";
    if (!WriteBinaryFile(temporary, text.data(), text.size(), detail)) return false;
    if (!MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        detail = WideFormatWin32Failed(L"MoveFileExW", GetLastError());
        return false;
    }
    return true;
}

bool ReadSmallAsciiFile(const std::wstring& path, std::string& content)
{
    content.clear();
    std::uint64_t size = 0;
    if (!FileSize(path, size) || size > 4096) return false;
    std::vector<unsigned char> bytes;
    std::wstring ignored;
    if (!ReadBinaryFile(path, bytes, ignored)) return false;
    content.assign(bytes.begin(), bytes.end());
    return true;
}

std::wstring ArtifactReceiptPath(
    const std::wstring& modelRoot,
    const OcrModelArtifactSpec& artifact)
{
    return WideJoinPath(
        WideJoinPath(WideJoinPath(modelRoot, L".zencrop"), L"receipts"),
        artifact.id + L".receipt");
}

std::string WideToAscii(const std::wstring& value);

std::string ArtifactReceipt(const OcrModelArtifactSpec& artifact)
{
    return WideToAscii(artifact.sha256) + " " +
        std::to_string(artifact.expectedBytes) + "\n";
}

bool PublishFile(
    const std::wstring& stagingPath,
    const std::wstring& finalPath,
    OcrModelDownloadError& error)
{
    std::wstring detail;
    if (!EnsureDirectory(ParentDirectory(finalPath), detail)) {
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.userMessage = L"The model directory is unavailable.";
        error.technicalDetail = detail;
        return false;
    }
    if (!MoveFileExW(
            stagingPath.c_str(), finalPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD moveError = GetLastError();
        error.category = moveError == ERROR_SHARING_VIOLATION || moveError == ERROR_ACCESS_DENIED
            ? OcrModelDownloadErrorCategory::ModelInUse
            : OcrModelDownloadErrorCategory::Publish;
        error.userMessage = error.category == OcrModelDownloadErrorCategory::ModelInUse
            ? L"The installed model is currently in use. Stop local OCR and retry."
            : L"The verified model file could not be installed.";
        error.technicalDetail = WideFormatWin32Failed(L"MoveFileExW", moveError);
        error.win32Error = moveError;
        error.retryable = true;
        return false;
    }
    return true;
}

bool HashMatches(
    const std::wstring& path,
    std::uint64_t expectedBytes,
    const std::wstring& expectedSha256)
{
    std::uint64_t bytes = 0;
    if (!FileSize(path, bytes) || bytes != expectedBytes) return false;
    std::wstring actual;
    std::wstring ignored;
    return ComputeFileSha256Hex(path, actual, ignored) && actual == expectedSha256;
}

bool SafeFlatArchiveName(const std::string& name)
{
    return !name.empty() && name.find('/') == std::string::npos &&
        name.find('\\') == std::string::npos && name.find(':') == std::string::npos &&
        name != "." && name != "..";
}

std::wstring AsciiToWide(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}

std::string WideToAscii(const std::wstring& value)
{
    std::string ascii;
    ascii.reserve(value.size());
    for (wchar_t ch : value) {
        ascii.push_back(ch <= 0x7f ? static_cast<char>(ch) : '?');
    }
    return ascii;
}

std::set<std::string> RequiredLlamaRuntimeNames()
{
    std::set<std::string> names;
    for (const wchar_t* name : LlamaRuntimeFiles()) names.insert(WideToAscii(name));
    return names;
}

std::wstring ModuleDirectory()
{
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return L"";
    return ParentDirectory(std::wstring(path.data(), length));
}

} // namespace

OcrModelInstaller::OcrModelInstaller(std::wstring runtimeTemplateDir)
    : runtimeTemplateDir_(std::move(runtimeTemplateDir))
{
}

bool OcrModelInstaller::PrepareTarget(
    const std::wstring& modelRoot,
    std::uint64_t requiredBytes,
    OcrModelDownloadError& error) const
{
    error = {};
    std::wstring detail;
    if (modelRoot.empty() || !EnsureDirectory(modelRoot, detail)) {
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.userMessage = L"The selected model directory is unavailable.";
        error.technicalDetail = detail;
        return false;
    }
    ULARGE_INTEGER available = {};
    if (!GetDiskFreeSpaceExW(modelRoot.c_str(), &available, nullptr, nullptr)) {
        const DWORD diskError = GetLastError();
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.userMessage = L"Free space could not be checked for the model directory.";
        error.technicalDetail = WideFormatWin32Failed(L"GetDiskFreeSpaceExW", diskError);
        error.win32Error = diskError;
        return false;
    }
    const std::uint64_t margin = 64ULL * 1024ULL * 1024ULL;
    if (available.QuadPart < requiredBytes + margin) {
        error.category = OcrModelDownloadErrorCategory::DiskSpace;
        error.userMessage = L"There is not enough free space for this model bundle.";
        error.technicalDetail = L"Required: " + std::to_wstring(requiredBytes + margin) +
            L" bytes; available: " + std::to_wstring(available.QuadPart) + L" bytes.";
        return false;
    }
    return true;
}

std::wstring OcrModelInstaller::StagingPath(
    const std::wstring& modelRoot,
    const OcrModelArtifactSpec& artifact) const
{
    const std::wstring staging = WideJoinPath(
        WideJoinPath(modelRoot, L".zencrop"), L"staging");
    return WideJoinPath(
        staging,
        artifact.id + L"-" + artifact.sha256.substr(0, 16) + L".part");
}

bool OcrModelInstaller::PrepareStaging(
    const std::wstring& modelRoot,
    const OcrModelArtifactSpec& artifact,
    std::wstring& stagingPath,
    OcrModelDownloadError& error) const
{
    error = {};
    stagingPath = StagingPath(modelRoot, artifact);
    std::wstring detail;
    if (!EnsureDirectory(ParentDirectory(stagingPath), detail)) {
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.artifactId = artifact.id;
        error.stage = L"preparing";
        error.userMessage = L"The download staging directory is unavailable.";
        error.technicalDetail = detail;
        return false;
    }
    const std::string sidecar =
        "{\n"
        "  \"version\": 1,\n"
        "  \"artifactId\": \"" + WideToAscii(artifact.id) + "\",\n"
        "  \"expectedBytes\": " + std::to_string(artifact.expectedBytes) + ",\n"
        "  \"sha256\": \"" + WideToAscii(artifact.sha256) + "\"\n"
        "}\n";
    const std::wstring sidecarPath = stagingPath + L".json";
    std::string existing;
    if (IsRegularFile(stagingPath) &&
        (!ReadSmallAsciiFile(sidecarPath, existing) || existing != sidecar)) {
        DeleteFileW(stagingPath.c_str());
    }
    if (!WriteTextAtomic(sidecarPath, sidecar, detail)) {
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.artifactId = artifact.id;
        error.stage = L"preparing";
        error.userMessage = L"Download recovery metadata could not be written.";
        error.technicalDetail = detail;
        return false;
    }
    return true;
}

bool OcrModelInstaller::ArtifactInstalled(
    const std::wstring& modelRoot,
    const OcrModelArtifactSpec& artifact,
    bool verifyContent) const
{
    if (artifact.kind == OcrModelArtifactKind::LlamaRuntimeZip) {
        return LlamaRuntimeInstalled(modelRoot, artifact);
    }
    const std::wstring finalPath = WideJoinPath(modelRoot, artifact.relativeInstallPath);
    if (verifyContent) {
        return HashMatches(finalPath, artifact.expectedBytes, artifact.sha256);
    }
    std::uint64_t bytes = 0;
    std::string receipt;
    return FileSize(finalPath, bytes) &&
        bytes == artifact.expectedBytes &&
        ReadSmallAsciiFile(ArtifactReceiptPath(modelRoot, artifact), receipt) &&
        receipt == ArtifactReceipt(artifact);
}

bool OcrModelInstaller::VerifyStaging(
    const OcrModelArtifactSpec& artifact,
    const std::wstring& stagingPath,
    OcrModelDownloadError& error) const
{
    error = {};
    std::uint64_t bytes = 0;
    if (!FileSize(stagingPath, bytes) || bytes != artifact.expectedBytes) {
        error.category = OcrModelDownloadErrorCategory::Integrity;
        error.artifactId = artifact.id;
        error.stage = L"verifying";
        error.userMessage = L"The downloaded file size does not match the verified catalog.";
        error.technicalDetail = L"Expected " + std::to_wstring(artifact.expectedBytes) +
            L" bytes; received " + std::to_wstring(bytes) + L" bytes.";
        return false;
    }
    std::wstring actual;
    std::wstring detail;
    if (!ComputeFileSha256Hex(stagingPath, actual, detail) || actual != artifact.sha256) {
        error.category = OcrModelDownloadErrorCategory::Integrity;
        error.artifactId = artifact.id;
        error.stage = L"verifying";
        error.userMessage = L"SHA-256 verification failed for the downloaded file.";
        error.technicalDetail = detail.empty()
            ? L"Expected " + artifact.sha256 + L"; received " + actual + L"."
            : detail;
        return false;
    }
    return true;
}

bool OcrModelInstaller::InstallArtifact(
    const std::wstring& modelRoot,
    const OcrModelArtifactSpec& artifact,
    const std::wstring& stagingPath,
    OcrModelDownloadError& error) const
{
    error = {};
    if (artifact.kind == OcrModelArtifactKind::LlamaRuntimeZip) {
        return InstallLlamaRuntime(modelRoot, artifact, stagingPath, error);
    }
    error.artifactId = artifact.id;
    error.stage = L"installing";
    if (!PublishFile(
            stagingPath,
            WideJoinPath(modelRoot, artifact.relativeInstallPath),
            error)) {
        return false;
    }
    std::wstring detail;
    if (!WriteTextAtomic(
            ArtifactReceiptPath(modelRoot, artifact),
            ArtifactReceipt(artifact),
            detail)) {
        error.category = OcrModelDownloadErrorCategory::Publish;
        error.userMessage = L"The model installation receipt could not be written.";
        error.technicalDetail = detail;
        return false;
    }
    DeleteFileW((stagingPath + L".json").c_str());
    return true;
}

bool OcrModelInstaller::InstallLlamaRuntime(
    const std::wstring& modelRoot,
    const OcrModelArtifactSpec& artifact,
    const std::wstring& archivePath,
    OcrModelDownloadError& error) const
{
    error = {};
    error.artifactId = artifact.id;
    error.stage = L"installing";
    std::wstring detail;
    std::vector<unsigned char> archiveBytes;
    if (!ReadBinaryFile(archivePath, archiveBytes, detail)) {
        error.category = OcrModelDownloadErrorCategory::Archive;
        error.userMessage = L"The llama.cpp runtime archive could not be read.";
        error.technicalDetail = detail;
        return false;
    }

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_mem(&zip, archiveBytes.data(), archiveBytes.size(), 0)) {
        error.category = OcrModelDownloadErrorCategory::Archive;
        error.userMessage = L"The llama.cpp runtime archive is invalid.";
        error.technicalDetail = AsciiToWide(mz_zip_get_error_string(zip.m_last_error));
        return false;
    }

    const std::set<std::string> required = RequiredLlamaRuntimeNames();
    std::set<std::string> present;
    std::set<std::string> seen;
    std::uint64_t totalExpanded = 0;
    const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
    bool valid = fileCount > 0 && fileCount <= 64;
    for (mz_uint i = 0; valid && i < fileCount; ++i) {
        mz_zip_archive_file_stat stat = {};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            valid = false;
            detail = L"ZIP central-directory entry could not be read.";
            break;
        }
        const std::string name = stat.m_filename;
        const mz_uint32 unixMode = stat.m_external_attr >> 16;
        const bool unixSymlink = (unixMode & 0170000U) == 0120000U;
        if (!SafeFlatArchiveName(name) || stat.m_is_directory ||
            stat.m_is_encrypted || unixSymlink || !seen.insert(name).second) {
            valid = false;
            detail = L"ZIP contains an unsafe, encrypted, symlink, or duplicate entry.";
            break;
        }
        if (stat.m_uncomp_size > 128ULL * 1024ULL * 1024ULL ||
            totalExpanded > 512ULL * 1024ULL * 1024ULL - stat.m_uncomp_size) {
            valid = false;
            detail = L"ZIP expanded size exceeds the safety limit.";
            break;
        }
        totalExpanded += stat.m_uncomp_size;
        if (required.find(name) != required.end()) present.insert(name);
    }
    if (valid && present != required) {
        valid = false;
        detail = L"ZIP does not contain the pinned llama.cpp runtime file set.";
    }
    if (!valid) {
        mz_zip_reader_end(&zip);
        error.category = OcrModelDownloadErrorCategory::Archive;
        error.userMessage = L"The llama.cpp runtime archive failed safety validation.";
        error.technicalDetail = detail;
        return false;
    }

    const std::wstring extractRoot = archivePath + L".extract";
    if (!EnsureDirectory(extractRoot, detail)) {
        mz_zip_reader_end(&zip);
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.userMessage = L"The llama.cpp extraction staging directory is unavailable.";
        error.technicalDetail = detail;
        return false;
    }

    for (const auto& name : required) {
        const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
        mz_zip_archive_file_stat stat = {};
        if (index < 0 || !mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &stat)) {
            detail = L"Required ZIP entry is unavailable: " + AsciiToWide(name);
            valid = false;
            break;
        }
        std::vector<unsigned char> content(static_cast<size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(
                &zip, static_cast<mz_uint>(index), content.data(), content.size(), 0)) {
            detail = L"ZIP entry failed CRC/extraction validation: " + AsciiToWide(name);
            valid = false;
            break;
        }
        if (!WriteBinaryFile(
                WideJoinPath(extractRoot, AsciiToWide(name)),
                content.data(), content.size(), detail)) {
            valid = false;
            break;
        }
    }
    mz_zip_reader_end(&zip);
    if (!valid) {
        error.category = OcrModelDownloadErrorCategory::Archive;
        error.userMessage = L"The llama.cpp runtime could not be extracted.";
        error.technicalDetail = detail;
        return false;
    }

    const std::wstring finalRoot = WideJoinPath(modelRoot, artifact.relativeInstallPath);
    if (!EnsureDirectory(finalRoot, detail)) {
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.userMessage = L"The llama.cpp runtime directory is unavailable.";
        error.technicalDetail = detail;
        return false;
    }
    for (const auto& name : required) {
        if (!PublishFile(
                WideJoinPath(extractRoot, AsciiToWide(name)),
                WideJoinPath(finalRoot, AsciiToWide(name)),
                error)) {
            return false;
        }
    }
    RemoveDirectoryW(extractRoot.c_str());
    if (!WriteTextAtomic(
            WideJoinPath(finalRoot, kLlamaReceiptName),
            WideToAscii(artifact.sha256) + "\n",
            detail)) {
        error.category = OcrModelDownloadErrorCategory::Publish;
        error.userMessage = L"The llama.cpp runtime receipt could not be written.";
        error.technicalDetail = detail;
        return false;
    }
    DeleteFileW((archivePath + L".json").c_str());
    DeleteFileW(archivePath.c_str());
    return true;
}

bool OcrModelInstaller::InstallPpOcrV6Metadata(
    const std::wstring& modelRoot,
    const std::wstring& variant,
    OcrModelDownloadError& error) const
{
    const std::wstring source = WideJoinPath(
        runtimeTemplateDir_, L"ppocrv6_rec_dict.txt");
    if (!HashMatches(source, kPpOcrV6DictBytes, kPpOcrV6DictSha256)) {
        error.category = OcrModelDownloadErrorCategory::Integrity;
        error.stage = L"installing";
        error.userMessage = L"The bundled PP-OCRv6 dictionary template is missing or corrupt.";
        error.technicalDetail = source;
        return false;
    }
    const std::wstring recDir = WideJoinPath(
        WideJoinPath(WideJoinPath(modelRoot, L"pp-ocrv6"), variant), L"rec");
    std::wstring detail;
    if (!EnsureDirectory(recDir, detail)) {
        error.category = OcrModelDownloadErrorCategory::TargetUnavailable;
        error.stage = L"installing";
        error.userMessage = L"The PP-OCRv6 recognition directory is unavailable.";
        error.technicalDetail = detail;
        return false;
    }
    const std::wstring dictPath = WideJoinPath(recDir, L"ppocrv6_rec_dict.txt");
    const std::wstring dictTemp = dictPath + L".tmp";
    if (!CopyFileW(source.c_str(), dictTemp.c_str(), FALSE)) {
        const DWORD copyError = GetLastError();
        error.category = OcrModelDownloadErrorCategory::Integrity;
        error.stage = L"installing";
        error.userMessage = L"The PP-OCRv6 dictionary could not be staged safely.";
        error.technicalDetail = WideFormatWin32Failed(L"CopyFileW", copyError);
        error.win32Error = copyError;
        return false;
    }
    if (!HashMatches(dictTemp, kPpOcrV6DictBytes, kPpOcrV6DictSha256)) {
        error.category = OcrModelDownloadErrorCategory::Integrity;
        error.stage = L"installing";
        error.userMessage = L"The PP-OCRv6 dictionary could not be staged safely.";
        error.technicalDetail = L"The staged dictionary failed its size or SHA-256 check.";
        return false;
    }
    if (!PublishFile(dictTemp, dictPath, error)) return false;

    const std::string modelName = variant == L"medium"
        ? "PP-OCRv6_medium_rec"
        : "PP-OCRv6_small_rec";
    const std::string manifest =
        "{\n"
        "  \"manifest_version\": 2,\n"
        "  \"model_name\": \"" + modelName + "\",\n"
        "  \"rec_image_shape\": [3, 48, 320],\n"
        "  \"decode_type\": \"CTCLabelDecode\",\n"
        "  \"base_dict_size\": 18708,\n"
        "  \"append_space\": true,\n"
        "  \"effective_dict_size\": 18709,\n"
        "  \"blank_index\": 0,\n"
        "  \"expected_output_classes\": 18710,\n"
        "  \"dict_size\": 18708\n"
        "}\n";
    if (!WriteTextAtomic(WideJoinPath(recDir, L"manifest.json"), manifest, detail)) {
        error.category = OcrModelDownloadErrorCategory::Publish;
        error.stage = L"installing";
        error.userMessage = L"The PP-OCRv6 manifest could not be installed.";
        error.technicalDetail = detail;
        return false;
    }
    return true;
}

bool OcrModelInstaller::FinalizeBundle(
    const std::wstring& modelRoot,
    const OcrModelBundleSpec& bundle,
    OcrModelDownloadError& error) const
{
    error = {};
    if (bundle.id == OcrModelBundleId::PpOcrV6Small) {
        if (!InstallPpOcrV6Metadata(modelRoot, L"small", error)) return false;
    } else if (bundle.id == OcrModelBundleId::PpOcrV6Medium) {
        if (!InstallPpOcrV6Metadata(modelRoot, L"medium", error)) return false;
    }
    if (!BundleInstalled(modelRoot, bundle, true)) {
        error.category = OcrModelDownloadErrorCategory::Integrity;
        error.stage = L"installing";
        error.userMessage = L"The installed model bundle did not pass its final readiness check.";
        return false;
    }
    return true;
}

bool OcrModelInstaller::LlamaRuntimeInstalled(
    const std::wstring& modelRoot,
    const OcrModelArtifactSpec& artifact) const
{
    const std::wstring runtimeRoot = WideJoinPath(modelRoot, artifact.relativeInstallPath);
    std::string receipt;
    if (!ReadSmallAsciiFile(WideJoinPath(runtimeRoot, kLlamaReceiptName), receipt) ||
        receipt != WideToAscii(artifact.sha256) + "\n") {
        return false;
    }
    for (const wchar_t* name : LlamaRuntimeFiles()) {
        if (!IsRegularFile(WideJoinPath(runtimeRoot, name))) return false;
    }
    return true;
}

bool OcrModelInstaller::BundleInstalled(
    const std::wstring& modelRoot,
    const OcrModelBundleSpec& bundle,
    bool verifyContent) const
{
    for (const auto& artifact : bundle.artifacts) {
        if (!ArtifactInstalled(modelRoot, artifact, verifyContent)) return false;
    }
    if (bundle.id == OcrModelBundleId::PpOcrV6Small ||
        bundle.id == OcrModelBundleId::PpOcrV6Medium) {
        const wchar_t* variant = bundle.id == OcrModelBundleId::PpOcrV6Medium
            ? L"medium" : L"small";
        const std::wstring recDir = WideJoinPath(
            WideJoinPath(WideJoinPath(modelRoot, L"pp-ocrv6"), variant), L"rec");
        if (!HashMatches(
                WideJoinPath(recDir, L"ppocrv6_rec_dict.txt"),
                kPpOcrV6DictBytes,
                kPpOcrV6DictSha256) ||
            !IsRegularFile(WideJoinPath(recDir, L"manifest.json"))) {
            return false;
        }
    }
    return true;
}

std::wstring OcrModelDefaultDownloadRoot()
{
    return ZenCropAppDataFilePath(L"models");
}

std::wstring OcrModelRuntimeTemplateDirectory()
{
    return WideJoinPath(ModuleDirectory(), L"ocr_templates");
}
