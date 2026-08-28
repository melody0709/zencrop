#include "BatchOcrImageLinks.h"

#include "OcrUtils.h"
#include "image/BitmapCodec.h"
#include "dashboard/DashboardFileTypes.h"
#include "core/WideStringUtils.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>
#include <set>
#include <utility>

namespace {

// OWN-73/75: thin wrappers over pure DashboardFileTypes / WideStringUtils helpers.
bool StartsWithNoCase(const std::wstring& value, const std::wstring& prefix) {
    return WideStartsWithNoCase(value, prefix);
}

std::wstring ToLower(std::wstring value) {
    return WideToLower(std::move(value));
}

bool EnsureDirectory(const std::wstring& dir) {
    if (dir.empty()) return false;
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

bool CanonicalizePath(std::wstring path, std::wstring& out) {
    for (wchar_t& ch : path) {
        if (ch == L'/') ch = L'\\';
    }

    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return false;

    std::wstring full(needed, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) return false;

    full.resize(written);
    out = full;
    return true;
}

// OWN-75: canonicalize (Win32) then pure path-under via WideStringUtils.
bool IsPathUnderDirectory(const std::wstring& path, const std::wstring& dir) {
    std::wstring fullPath;
    std::wstring fullDir;
    if (!CanonicalizePath(path, fullPath) || !CanonicalizePath(dir, fullDir)) return false;
    fullPath = ToLower(std::move(fullPath));
    fullDir = ToLower(std::move(fullDir));
    return WideIsPathStrictlyUnderDirectory(fullPath, fullDir);
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// OWN-75: thin wrapper over pure WideJoinPath.
std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    return WideJoinPath(left, right);
}

std::wstring LastErrorMessage(const wchar_t* prefix) {
    DWORD err = GetLastError();
    wchar_t* buffer = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring result = prefix ? prefix : L"Operation failed";
    if (err != 0) {
        // OWN-125: pure paren int (WideStringUtils).
        result += WideFormatParenInt(static_cast<int>(err));
    }
    if (buffer) {
        result += L": ";
        result += buffer;
        LocalFree(buffer);
    }
    return result;
}

std::wstring UrlDecodeUtf8(const std::wstring& input) {
    std::string bytes;
    bytes.reserve(input.size());
    auto hexValue = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };

    for (size_t i = 0; i < input.size(); i++) {
        wchar_t ch = input[i];
        if (ch == L'%' && i + 2 < input.size()) {
            int hi = hexValue(input[i + 1]);
            int lo = hexValue(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                bytes.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (ch == L'+') {
            bytes.push_back(' ');
        } else if (ch <= 0x7F) {
            bytes.push_back(static_cast<char>(ch));
        } else {
            char mb[8] = {};
            int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, mb, sizeof(mb), nullptr, nullptr);
            if (len > 0) bytes.append(mb, mb + len);
        }
    }

    if (bytes.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    }
    if (wlen <= 0) return L"";

    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), result.data(), wlen);
    return result;
}

// OWN-75: thin wrapper over pure WideIsAllowedImageExtension.
bool IsAllowedImageExtension(const std::wstring& path) {
    return WideIsAllowedImageExtension(path);
}

bool TryExtractLocalOcrImagePath(const std::wstring& url, std::wstring& canonicalPath) {
    if (!StartsWithNoCase(url, L"http://127.0.0.1:") &&
        !StartsWithNoCase(url, L"http://localhost:")) {
        return false;
    }

    size_t pathParam = url.find(L"?path=");
    if (pathParam == std::wstring::npos) pathParam = url.find(L"&path=");
    if (pathParam == std::wstring::npos) return false;
    pathParam += 6;

    size_t pathEnd = url.find(L'&', pathParam);
    std::wstring encodedPath = pathEnd == std::wstring::npos
        ? url.substr(pathParam)
        : url.substr(pathParam, pathEnd - pathParam);
    std::wstring decodedPath = UrlDecodeUtf8(encodedPath);
    if (decodedPath.empty()) return false;

    std::wstring fullPath;
    if (!CanonicalizePath(decodedPath, fullPath)) return false;
    if (!IsPathUnderDirectory(fullPath, GetOcrImageDir())) return false;
    if (!IsAllowedImageExtension(fullPath)) return false;
    if (!FileExists(fullPath)) return false;

    canonicalPath = fullPath;
    return true;
}

// OWN-75: thin wrappers over pure WideStringUtils helpers.
bool IsUrlTerminator(wchar_t ch) {
    return WideIsUrlTerminator(ch);
}

std::wstring NormalizeAssetExtension(const std::wstring& sourcePath) {
    return WideNormalizeAssetExtension(sourcePath);
}

std::wstring BuildAssetFileName(
    int pageIndex,
    int assetIndex,
    const std::wstring& sourcePath,
    PdfRenderImageFormat forcedFormat)
{
    if (pageIndex <= 0) pageIndex = 1;

    // OWN-73: pure page asset stem + product extension.
    const std::wstring extension = forcedFormat == PdfRenderImageFormat::Auto
        ? NormalizeAssetExtension(sourcePath)
        : PdfRenderImageFormatExtension(forcedFormat);
    return DashboardFormatPageAssetStem(pageIndex, assetIndex) + extension;
}

ImageCodec::ImageFileFormat EmbeddedAssetCodecFormat(PdfRenderImageFormat format) {
    switch (format) {
    case PdfRenderImageFormat::Jpeg:
        return ImageCodec::ImageFileFormat::Jpeg;
    case PdfRenderImageFormat::WebP:
        return ImageCodec::ImageFileFormat::WebP;
    case PdfRenderImageFormat::Png:
    case PdfRenderImageFormat::Auto:
    default:
        return ImageCodec::ImageFileFormat::Png;
    }
}

bool WriteEmbeddedAsset(
    const std::wstring& sourcePath,
    const std::wstring& targetPath,
    PdfRenderImageFormat forcedFormat,
    int quality,
    std::wstring& error)
{
    error.clear();
    if (forcedFormat == PdfRenderImageFormat::Auto) {
        if (!CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
            error = LastErrorMessage(L"Failed to copy OCR image asset");
            return false;
        }
        return true;
    }

    std::wstring decodeError;
    HBITMAP bitmap = ImageCodec::LoadHBitmapFromFile(sourcePath, &decodeError);
    if (!bitmap) {
        error = decodeError.empty() ? L"Failed to decode OCR image asset." : decodeError;
        return false;
    }

    // OWN-125: pure tmp pid.tick (WideStringUtils).
    const std::wstring tempPath = targetPath +
        WideFormatTmpPidTick(GetCurrentProcessId(), GetTickCount64()) +
        PdfRenderImageFormatExtension(forcedFormat);
    DeleteFileW(tempPath.c_str());

    ImageCodec::EncodeOptions encodeOptions;
    encodeOptions.quality = quality;
    std::wstring encodeError;
    const bool saved = ImageCodec::SaveHBitmapToFile(
        bitmap,
        tempPath,
        EmbeddedAssetCodecFormat(forcedFormat),
        encodeOptions,
        &encodeError);
    DeleteObject(bitmap);
    if (!saved) {
        DeleteFileW(tempPath.c_str());
        error = encodeError.empty() ? L"Failed to encode OCR image asset." : encodeError;
        return false;
    }

    if (!MoveFileExW(
            tempPath.c_str(),
            targetPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = LastErrorMessage(L"Failed to finalize OCR image asset");
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

std::wstring ToRelativeAssetPath(const std::wstring& fileName) {
    return L"assets/" + fileName;
}

PdfRenderImageFormat ProviderFormat(const OcrEmbeddedAssetSpec& spec) {
    switch (spec.providerFormat) {
    case OcrEmbeddedAssetEncodedFormat::Png: return PdfRenderImageFormat::Png;
    case OcrEmbeddedAssetEncodedFormat::Jpeg: return PdfRenderImageFormat::Jpeg;
    case OcrEmbeddedAssetEncodedFormat::WebP: return PdfRenderImageFormat::WebP;
    default: return PdfRenderImageFormat::Auto;
    }
}

OcrEmbeddedAssetEncodedFormat DetectProviderFormat(
    const std::vector<unsigned char>& bytes)
{
    if (bytes.size() >= 12 &&
        memcmp(bytes.data(), "RIFF", 4) == 0 &&
        memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
        return OcrEmbeddedAssetEncodedFormat::WebP;
    }
    static const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() >= sizeof(png) && memcmp(bytes.data(), png, sizeof(png)) == 0) {
        return OcrEmbeddedAssetEncodedFormat::Png;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        return OcrEmbeddedAssetEncodedFormat::Jpeg;
    }
    return OcrEmbeddedAssetEncodedFormat::Unknown;
}

PdfRenderImageFormat EffectiveAssetFormat(
    const OcrEmbeddedAssetSpec& spec,
    PdfRenderImageFormat requested)
{
    if (requested != PdfRenderImageFormat::Auto) return requested;
    // OWN-94: pure case-insensitive semantic class compare.
    return WideEqualsNoCase(spec.semanticClass, L"seal")
        ? PdfRenderImageFormat::Png
        : PdfRenderImageFormat::Jpeg;
}

std::wstring SanitizeAssetStem(std::wstring stem) {
    if (stem.empty()) stem = L"ocr";
    for (wchar_t& ch : stem) {
        if (!iswalnum(ch) && ch != L'-' && ch != L'_') ch = L'_';
    }
    if (stem.size() > 80) stem.resize(80);
    return stem;
}

bool WriteBytesFile(
    const std::wstring& path,
    const std::vector<unsigned char>& bytes,
    std::wstring& error)
{
    error.clear();
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = LastErrorMessage(L"Failed to create OCR asset candidate");
        return false;
    }
    DWORD written = 0;
    const bool ok = bytes.size() <= MAXDWORD &&
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
        error = LastErrorMessage(L"Failed to write OCR asset candidate");
        DeleteFileW(path.c_str());
    }
    return ok;
}

HBITMAP CropBitmapForAsset(HBITMAP source, const RECT& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (!source || width <= 0 || height <= 0) return nullptr;
    HDC screen = GetDC(nullptr);
    HDC sourceDc = CreateCompatibleDC(screen);
    HDC targetDc = CreateCompatibleDC(screen);
    HBITMAP target = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ oldSource = SelectObject(sourceDc, source);
    HGDIOBJ oldTarget = SelectObject(targetDc, target);
    const bool copied = BitBlt(
        targetDc, 0, 0, width, height,
        sourceDc, rect.left, rect.top, SRCCOPY) != FALSE;
    SelectObject(targetDc, oldTarget);
    SelectObject(sourceDc, oldSource);
    DeleteDC(targetDc);
    DeleteDC(sourceDc);
    ReleaseDC(nullptr, screen);
    if (!copied) {
        DeleteObject(target);
        return nullptr;
    }
    return target;
}

bool ReplaceAllExact(
    std::wstring& text,
    const std::wstring& needle,
    const std::wstring& replacement)
{
    if (needle.empty()) return false;
    bool replaced = false;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::wstring::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
        replaced = true;
    }
    return replaced;
}

} // namespace

BatchOcrAssetTransaction::~BatchOcrAssetTransaction() {
    RollbackOcrAssetTransaction(*this);
}

BatchOcrAssetTransaction::BatchOcrAssetTransaction(
    BatchOcrAssetTransaction&& other) noexcept
    : entries(std::move(other.entries)), active(other.active)
{
    other.active = false;
}

BatchOcrAssetTransaction& BatchOcrAssetTransaction::operator=(
    BatchOcrAssetTransaction&& other) noexcept
{
    if (this == &other) return *this;
    RollbackOcrAssetTransaction(*this);
    entries = std::move(other.entries);
    active = other.active;
    other.active = false;
    return *this;
}

BatchOcrImageLinkRewriteResult MaterializeOcrEmbeddedAssets(
    const std::wstring& markdown,
    const std::wstring& canonicalSourceImagePath,
    const std::vector<OcrEmbeddedAssetSpec>& specs,
    const std::wstring& assetsDir,
    int pageIndex,
    const OcrOutputArtifactOptions& sourceOptions,
    OcrEmbeddedAssetReferenceKind referenceKind,
    const std::wstring& transientAssetStem)
{
    BatchOcrImageLinkRewriteResult result;
    result.markdown = markdown;
    if (specs.empty()) return result;
    if (assetsDir.empty() || !EnsureDirectory(assetsDir)) {
        result.error = L"Failed to create OCR embedded assets directory.";
        return result;
    }

    const OcrOutputArtifactOptions options =
        NormalizeOcrOutputArtifactOptions(sourceOptions);
    HBITMAP canonicalBitmap = nullptr;
    BITMAP canonicalInfo = {};
    const bool needsCanonical = std::any_of(
        specs.begin(), specs.end(), [](const OcrEmbeddedAssetSpec& spec) {
            return spec.sourceKind == OcrEmbeddedAssetSourceKind::CanonicalCrop;
        });
    if (needsCanonical) {
        std::wstring decodeError;
        canonicalBitmap = ImageCodec::LoadHBitmapFromFile(
            canonicalSourceImagePath, &decodeError);
        if (!canonicalBitmap) {
            result.error = decodeError.empty()
                ? L"Failed to load canonical OCR image for embedded assets."
                : decodeError;
            return result;
        }
        GetObject(canonicalBitmap, sizeof(canonicalInfo), &canonicalInfo);
    }

    struct StagedAsset {
        std::wstring placeholder;
        std::wstring targetPath;
        std::wstring candidatePath;
        std::wstring backupPath;
        std::wstring markdownReference;
        bool published = false;
    };
    std::vector<StagedAsset> staged;
    staged.reserve(specs.size());
    std::set<std::wstring> targetKeys;
    std::set<std::wstring> assetIds;
    std::set<std::wstring> placeholders;
    const ULONGLONG transactionTick = GetTickCount64();
    const DWORD processId = GetCurrentProcessId();
    const std::wstring safeTransientStem = SanitizeAssetStem(transientAssetStem);

    auto fail = [&](const std::wstring& message) {
        if (result.error.empty()) result.error = message;
        for (auto it = staged.rbegin(); it != staged.rend(); ++it) {
            DeleteFileW(it->candidatePath.c_str());
            if (it->published) DeleteFileW(it->targetPath.c_str());
            if (!it->backupPath.empty() && FileExists(it->backupPath)) {
                MoveFileExW(
                    it->backupPath.c_str(), it->targetPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
        }
        if (canonicalBitmap) {
            DeleteObject(canonicalBitmap);
            canonicalBitmap = nullptr;
        }
    };

    for (size_t index = 0; index < specs.size(); ++index) {
        OcrEmbeddedAssetSpec spec = specs[index];
        if (spec.id.empty() || !assetIds.insert(ToLower(spec.id)).second) {
            fail(L"OCR embedded asset IDs must be non-empty and unique.");
            return result;
        }
        if (spec.placeholderUri.empty() ||
            result.markdown.find(spec.placeholderUri) == std::wstring::npos) {
            fail(L"OCR embedded asset placeholder/spec mismatch.");
            return result;
        }
        if (!placeholders.insert(spec.placeholderUri).second) {
            fail(L"OCR embedded asset placeholders must be unique per asset.");
            return result;
        }
        if (spec.sourceKind == OcrEmbeddedAssetSourceKind::ProviderEncodedBytes) {
            const auto detected = DetectProviderFormat(spec.providerBytes);
            if (detected == OcrEmbeddedAssetEncodedFormat::Unknown) {
                fail(L"Provider OCR embedded asset has an unsupported signature.");
                return result;
            }
            if (spec.providerFormat == OcrEmbeddedAssetEncodedFormat::Unknown) {
                spec.providerFormat = detected;
            } else if (spec.providerFormat != detected) {
                fail(L"Provider OCR embedded asset format metadata does not match its bytes.");
                return result;
            }
        }

        const PdfRenderImageFormat format =
            EffectiveAssetFormat(spec, options.embeddedAssetFormat);
        if (format == PdfRenderImageFormat::Auto) {
            fail(L"Could not determine OCR embedded asset output format.");
            return result;
        }
        const int order = spec.localOrder > 0
            ? spec.localOrder : static_cast<int>(index) + 1;
        // OWN-113: pure page asset stem / pad3 for embedded asset names.
        std::wstring fileName;
        if (referenceKind == OcrEmbeddedAssetReferenceKind::OutputRelative) {
            fileName = WideFormatPageAssetStem((std::max)(1, pageIndex), order)
                + PdfRenderImageFormatExtension(format);
        } else {
            fileName = safeTransientStem + L"_asset_" + WideFormatPad3(order)
                + PdfRenderImageFormatExtension(format);
        }
        const std::wstring targetPath = JoinPath(assetsDir, fileName);
        std::wstring targetKey = ToLower(targetPath);
        if (!targetKeys.insert(targetKey).second) {
            fail(L"Duplicate OCR embedded asset target name.");
            return result;
        }

        StagedAsset item;
        item.placeholder = spec.placeholderUri;
        item.targetPath = targetPath;
        // OWN-125: pure candidate pid.tick (WideStringUtils).
        item.candidatePath = targetPath +
            WideFormatCandidatePidTick(processId, transactionTick) +
            PdfRenderImageFormatExtension(format);
        DeleteFileW(item.candidatePath.c_str());

        bool encoded = false;
        std::wstring encodeError;
        if (spec.sourceKind == OcrEmbeddedAssetSourceKind::CanonicalCrop) {
            const RECT rect = spec.cropRect;
            if (rect.left < 0 || rect.top < 0 || rect.right > canonicalInfo.bmWidth ||
                rect.bottom > canonicalInfo.bmHeight ||
                rect.right <= rect.left || rect.bottom <= rect.top) {
                fail(L"OCR embedded asset crop is outside the canonical image.");
                return result;
            }
            HBITMAP crop = CropBitmapForAsset(canonicalBitmap, rect);
            if (!crop) {
                fail(L"Failed to crop OCR embedded asset from canonical pixels.");
                return result;
            }
            ImageCodec::EncodeOptions encodeOptions;
            encodeOptions.quality = options.embeddedAssetQuality;
            encoded = ImageCodec::SaveHBitmapToFile(
                crop,
                item.candidatePath,
                EmbeddedAssetCodecFormat(format),
                encodeOptions,
                &encodeError);
            DeleteObject(crop);
        } else if (options.embeddedAssetFormat == PdfRenderImageFormat::Auto &&
                   format == ProviderFormat(spec)) {
            encoded = WriteBytesFile(
                item.candidatePath, spec.providerBytes, encodeError);
            if (encoded) {
                HBITMAP validationBitmap = ImageCodec::LoadHBitmapFromFile(
                    item.candidatePath, &encodeError);
                encoded = validationBitmap != nullptr;
                if (validationBitmap) DeleteObject(validationBitmap);
            }
        } else {
            const std::wstring providerInput = item.candidatePath + L".provider" +
                PdfRenderImageFormatExtension(ProviderFormat(spec));
            if (WriteBytesFile(providerInput, spec.providerBytes, encodeError)) {
                HBITMAP providerBitmap = ImageCodec::LoadHBitmapFromFile(
                    providerInput, &encodeError);
                if (providerBitmap) {
                    ImageCodec::EncodeOptions encodeOptions;
                    encodeOptions.quality = options.embeddedAssetQuality;
                    encoded = ImageCodec::SaveHBitmapToFile(
                        providerBitmap,
                        item.candidatePath,
                        EmbeddedAssetCodecFormat(format),
                        encodeOptions,
                        &encodeError);
                    DeleteObject(providerBitmap);
                }
            }
            DeleteFileW(providerInput.c_str());
        }
        if (!encoded) {
            DeleteFileW(item.candidatePath.c_str());
            fail(encodeError.empty()
                ? L"Failed to encode OCR embedded asset candidate."
                : encodeError);
            return result;
        }

        item.markdownReference =
            referenceKind == OcrEmbeddedAssetReferenceKind::OutputRelative
                ? ToRelativeAssetPath(fileName)
                : targetPath;
        staged.push_back(std::move(item));
    }

    // Publish only after every candidate has been encoded successfully. Each
    // overwritten target is backed up so a later publish failure can restore
    // the complete previous set rather than leaving a partial retry state.
    for (size_t index = 0; index < staged.size(); ++index) {
        auto& item = staged[index];
        if (FileExists(item.targetPath)) {
            // OWN-125: pure backup pid.tick (WideStringUtils).
            item.backupPath = item.targetPath +
                WideFormatBackupPidTick(processId, transactionTick);
            DeleteFileW(item.backupPath.c_str());
            if (!MoveFileExW(
                    item.targetPath.c_str(), item.backupPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                fail(LastErrorMessage(L"Failed to back up existing OCR embedded asset"));
                return result;
            }
        }
        if (!MoveFileExW(
                item.candidatePath.c_str(), item.targetPath.c_str(),
                MOVEFILE_WRITE_THROUGH)) {
            fail(LastErrorMessage(L"Failed to publish OCR embedded asset set"));
            return result;
        }
        item.published = true;
    }

    if (canonicalBitmap) DeleteObject(canonicalBitmap);
    for (auto& item : staged) {
        ReplaceAllExact(result.markdown, item.placeholder, item.markdownReference);
        result.assets.push_back(item.markdownReference);
        BatchOcrAssetTransactionEntry transactionEntry;
        transactionEntry.targetPath = item.targetPath;
        transactionEntry.backupPath = item.backupPath;
        transactionEntry.published = item.published;
        result.transaction.entries.push_back(std::move(transactionEntry));
        if (referenceKind == OcrEmbeddedAssetReferenceKind::LocalhostCache) {
            result.ownedFiles.push_back(item.targetPath);
        }
    }
    result.transaction.active = !result.transaction.entries.empty();
    return result;
}

void MergeOcrAssetTransactions(
    BatchOcrAssetTransaction& target,
    BatchOcrAssetTransaction&& source)
{
    if (source.entries.empty()) return;
    target.entries.insert(
        target.entries.end(),
        std::make_move_iterator(source.entries.begin()),
        std::make_move_iterator(source.entries.end()));
    target.active = true;
    source.entries.clear();
    source.active = false;
}

void CommitOcrAssetTransaction(BatchOcrAssetTransaction& transaction) {
    if (!transaction.active) return;
    for (const auto& entry : transaction.entries) {
        if (!entry.backupPath.empty()) DeleteFileW(entry.backupPath.c_str());
    }
    transaction.entries.clear();
    transaction.active = false;
}

void RollbackOcrAssetTransaction(BatchOcrAssetTransaction& transaction) {
    if (!transaction.active) return;
    for (auto it = transaction.entries.rbegin(); it != transaction.entries.rend(); ++it) {
        if (it->published) DeleteFileW(it->targetPath.c_str());
        if (!it->backupPath.empty() && FileExists(it->backupPath)) {
            MoveFileExW(
                it->backupPath.c_str(), it->targetPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }
    transaction.entries.clear();
    transaction.active = false;
}

BatchOcrImageLinkRewriteResult RewriteOcrImageLinksForExport(
    const std::wstring& markdown,
    const std::wstring& outputAssetsDir,
    int pageIndex,
    const OcrOutputArtifactOptions& sourceOptions,
    int firstAssetIndex)
{
    BatchOcrImageLinkRewriteResult result;
    result.markdown = markdown;
    if (markdown.empty() || outputAssetsDir.empty()) {
        return result;
    }

    const OcrOutputArtifactOptions options =
        NormalizeOcrOutputArtifactOptions(sourceOptions);
    const PdfRenderImageFormat forcedFormat = options.embeddedAssetFormat;

    std::wstring out;
    out.reserve(markdown.size());
    std::map<std::wstring, std::wstring> copiedByCanonicalPath;
    int nextAssetIndex = (std::max)(1, firstAssetIndex);
    const ULONGLONG transactionTick = GetTickCount64();
    const DWORD processId = GetCurrentProcessId();
    size_t pos = 0;

    auto fail = [&](const std::wstring& error) {
        result.error = error;
        RollbackOcrAssetTransaction(result.transaction);
    };

    while (pos < markdown.size()) {
        size_t next127 = markdown.find(L"http://127.0.0.1:", pos);
        size_t nextLocalhost = markdown.find(L"http://localhost:", pos);
        size_t next = std::wstring::npos;
        if (next127 != std::wstring::npos) next = next127;
        if (nextLocalhost != std::wstring::npos && (next == std::wstring::npos || nextLocalhost < next)) {
            next = nextLocalhost;
        }

        if (next == std::wstring::npos) {
            out.append(markdown, pos, std::wstring::npos);
            break;
        }

        out.append(markdown, pos, next - pos);

        size_t end = next;
        while (end < markdown.size() && !IsUrlTerminator(markdown[end])) end++;
        std::wstring url = markdown.substr(next, end - next);

        std::wstring imagePath;
        if (!TryExtractLocalOcrImagePath(url, imagePath)) {
            fail(L"Completed OCR Markdown contains an unresolved localhost image reference.");
            return std::move(result);
        }

        std::wstring key = ToLower(imagePath);
        auto found = copiedByCanonicalPath.find(key);
        if (found != copiedByCanonicalPath.end()) {
            out += found->second;
            pos = end;
            continue;
        }

        if (!EnsureDirectory(outputAssetsDir)) {
            fail(L"Failed to create export assets directory.");
            return std::move(result);
        }

        std::wstring assetName = BuildAssetFileName(
            pageIndex,
            nextAssetIndex,
            imagePath,
            forcedFormat);
        std::wstring targetPath = JoinPath(outputAssetsDir, assetName);
        BatchOcrAssetTransactionEntry transactionEntry;
        transactionEntry.targetPath = targetPath;
        if (FileExists(targetPath)) {
            // OWN-125: pure backup pid.tick (WideStringUtils).
            transactionEntry.backupPath = targetPath +
                WideFormatBackupPidTick(processId, transactionTick);
            DeleteFileW(transactionEntry.backupPath.c_str());
            if (!MoveFileExW(
                    targetPath.c_str(), transactionEntry.backupPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                fail(LastErrorMessage(L"Failed to back up legacy OCR image asset"));
                return std::move(result);
            }
        }
        if (!WriteEmbeddedAsset(
                imagePath,
                targetPath,
                forcedFormat,
                options.embeddedAssetQuality,
                result.error)) {
            if (!transactionEntry.backupPath.empty() && FileExists(transactionEntry.backupPath)) {
                MoveFileExW(
                    transactionEntry.backupPath.c_str(), targetPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
            fail(result.error);
            return std::move(result);
        }
        transactionEntry.published = true;
        result.transaction.entries.push_back(std::move(transactionEntry));
        result.transaction.active = true;

        std::wstring relativeAssetPath = ToRelativeAssetPath(assetName);
        copiedByCanonicalPath.emplace(key, relativeAssetPath);
        result.assets.push_back(relativeAssetPath);
        nextAssetIndex++;

        out += relativeAssetPath;
        pos = end;
    }

    result.markdown = std::move(out);
    return result;
}

bool RemoveStaleOcrEmbeddedAssetFiles(
    const std::wstring& assetsDir,
    int pageIndex,
    const std::vector<std::wstring>& committedAssets,
    std::wstring& warning)
{
    warning.clear();
    if (assetsDir.empty()) return true;
    DWORD attrs = GetFileAttributesW(assetsDir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        warning = L"OCR assets path is not a directory; stale assets were not cleaned.";
        return false;
    }

    std::set<std::wstring> keepNames;
    for (const auto& reference : committedAssets) {
        if (reference.empty()) continue;
        std::wstring normalized = reference;
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
        // OWN-96: pure file-name extract (WideStringUtils).
        const std::wstring name = WideFileNameFromPath(normalized);
        if (!name.empty()) keepNames.insert(ToLower(name));
    }

    // OWN-113: pure page asset prefix.
    const std::wstring prefix = WideFormatPageAssetPrefix((std::max)(1, pageIndex));
    const std::wstring pattern = JoinPath(assetsDir, prefix + L"*");
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return true;
        warning = L"Failed to enumerate stale OCR embedded assets.";
        return false;
    }

    bool ok = true;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const std::wstring name = data.cFileName;
        // OWN-94: pure image-ext gate (WideStringUtils).
        if (!WideIsAllowedImageExtension(name)) {
            continue;
        }
        if (keepNames.find(ToLower(name)) != keepNames.end()) continue;
        if (!DeleteFileW(JoinPath(assetsDir, name).c_str())) ok = false;
    } while (FindNextFileW(find, &data));
    FindClose(find);

    if (!ok) {
        warning = L"Some stale OCR embedded assets could not be removed.";
    }
    return ok;
}
