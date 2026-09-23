#define WIN32_LEAN_AND_MEAN
#include "WebAssetGuard.h"

#include "web-assets/WebAssetsManifest.generated.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace ZenCrop::WebAssets {
namespace {

class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE value) : value_(value) {}
    ~ScopedHandle() { reset(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : value_(other.release()) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }

    HANDLE get() const { return value_; }
    bool valid() const { return value_ != INVALID_HANDLE_VALUE && value_ != nullptr; }
    HANDLE release() {
        HANDLE value = value_;
        value_ = INVALID_HANDLE_VALUE;
        return value;
    }
    void reset(HANDLE value = INVALID_HANDLE_VALUE) {
        if (valid()) CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

struct ObservedAsset {
    std::wstring path;
};

std::wstring JoinPath(const std::wstring& base, const std::wstring& child) {
    if (base.empty()) return child;
    if (base.back() == L'\\' || base.back() == L'/') return base + child;
    return base + L"\\" + child;
}

bool EqualsNoCaseOrdinal(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(
        left.c_str(), static_cast<int>(left.size()),
        right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool IsFinalPathUnderRoot(const std::wstring& path, const std::wstring& root, bool allowRoot) {
    if (allowRoot && EqualsNoCaseOrdinal(path, root)) return true;
    if (path.size() <= root.size()) return false;
    if (!EqualsNoCaseOrdinal(path.substr(0, root.size()), root)) return false;
    wchar_t separator = path[root.size()];
    return separator == L'\\' || separator == L'/';
}

bool GetFinalPath(HANDLE handle, std::wstring& output) {
    output.clear();
    DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (required == 0) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1, L'\0');
    DWORD length = GetFinalPathNameByHandleW(
        handle, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
    if (length == 0 || length >= buffer.size()) return false;
    output.assign(buffer.data(), length);
    while (output.size() > 3 && (output.back() == L'\\' || output.back() == L'/')) {
        output.pop_back();
    }
    return true;
}

bool GetAttributeTag(HANDLE handle, FILE_ATTRIBUTE_TAG_INFO& output) {
    output = {};
    return GetFileInformationByHandleEx(
        handle, FileAttributeTagInfo, &output, sizeof(output)) != FALSE;
}

bool IsReparsePoint(const FILE_ATTRIBUTE_TAG_INFO& attributes) {
    return (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool IsDirectory(const FILE_ATTRIBUTE_TAG_INFO& attributes) {
    return (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool OpenDirectoryNoReparse(
    const std::wstring& path,
    ScopedHandle& handle,
    FILE_ATTRIBUTE_TAG_INFO& attributes,
    std::wstring& finalPath)
{
    handle.reset(CreateFileW(
        path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr));
    if (!handle.valid() || !GetAttributeTag(handle.get(), attributes) || !GetFinalPath(handle.get(), finalPath)) {
        return false;
    }
    return true;
}

bool OpenFileNoReparse(
    const std::wstring& path,
    ScopedHandle& handle,
    FILE_ATTRIBUTE_TAG_INFO& attributes,
    std::wstring& finalPath)
{
    handle.reset(CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle.valid() || !GetAttributeTag(handle.get(), attributes) || !GetFinalPath(handle.get(), finalPath)) {
        return false;
    }
    return true;
}

bool IsValidRelativePath(const std::wstring& path) {
    if (path.empty() || path.front() == L'/' || path.find(L'\\') != std::wstring::npos ||
        path.find(L"//") != std::wstring::npos || path.find(L":") != std::wstring::npos) {
        return false;
    }
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find(L'/', start);
        if (end == std::wstring::npos) end = path.size();
        const std::wstring component = path.substr(start, end - start);
        if (component.empty() || component == L"." || component == L"..") return false;
        for (wchar_t character : component) {
            if (std::iswcntrl(character)) return false;
        }
        start = end + 1;
    }
    return true;
}

std::wstring HexSha256(const std::vector<unsigned char>& digest) {
    static constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring output;
    output.reserve(digest.size() * 2);
    for (unsigned char value : digest) {
        output.push_back(hex[(value >> 4) & 0x0F]);
        output.push_back(hex[value & 0x0F]);
    }
    return output;
}

bool HashFileSha256(HANDLE file, std::wstring& output) {
    output.clear();
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status != 0) return false;

    DWORD objectLength = 0;
    DWORD resultLength = 0;
    status = BCryptGetProperty(
        algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0);
    if (status != 0 || objectLength == 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<unsigned char> object(objectLength);
    std::vector<unsigned char> digest(32);
    status = BCryptCreateHash(
        algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    LARGE_INTEGER zero = {};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    std::vector<unsigned char> buffer(64 * 1024);
    bool ok = true;
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            ok = false;
            break;
        }
        if (bytesRead == 0) break;
        status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
        if (status != 0) {
            ok = false;
            break;
        }
    }
    if (ok) {
        status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        ok = status == 0;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    output = HexSha256(digest);
    return true;
}

const AssetEntry* FindExpectedExact(const std::wstring& path) {
    for (const AssetEntry& entry : kEntries) {
        if (path == entry.path) return &entry;
    }
    return nullptr;
}

const AssetEntry* FindExpectedNoCase(const std::wstring& path) {
    for (const AssetEntry& entry : kEntries) {
        if (EqualsNoCaseOrdinal(path, entry.path)) return &entry;
    }
    return nullptr;
}

bool HasExpectedDescendantNoCase(const std::wstring& directoryPath) {
    for (const AssetEntry& entry : kEntries) {
        const std::wstring expectedPath(entry.path);
        if (expectedPath.size() <= directoryPath.size() ||
            expectedPath[directoryPath.size()] != L'/' ||
            !EqualsNoCaseOrdinal(expectedPath.substr(0, directoryPath.size()), directoryPath)) {
            continue;
        }
        return true;
    }
    return false;
}

std::wstring RecoveryHint(GuardFailure failure) {
    if (failure == GuardFailure::UnknownFile || failure == GuardFailure::CaseCollision) {
        return L" Confirm that the listed item is an obsolete runtime artifact and remove only that exact item; "
               L"MSI Repair does not delete unknown files.";
    }
    return L" Repair the MSI with its original package (msiexec /fa), or uninstall and reinstall it. "
           L"For Portable, extract a fresh ZIP into a new empty directory.";
}

GuardResult Failure(GuardFailure failure, const std::wstring& path, const std::wstring& detail = L"") {
    GuardResult result;
    result.failure = failure;
    result.relativePath = path;
    result.message = L"Markdown preview asset validation failed: " +
        std::wstring(GuardFailureName(failure));
    if (!path.empty()) result.message += L" (" + path + L")";
    if (!detail.empty()) result.message += L": " + detail;
    result.message += L". Expected asset build " + std::wstring(kAssetBuildId) + L".";
    result.message += RecoveryHint(failure);
    return result;
}

GuardResult EnumerateDirectory(
    const std::wstring& directory,
    const std::wstring& relativePrefix,
    const std::wstring& rootFinalPath,
    std::vector<ObservedAsset>& observed)
{
    ScopedHandle directoryHandle;
    FILE_ATTRIBUTE_TAG_INFO directoryAttributes = {};
    std::wstring directoryFinalPath;
    if (!OpenDirectoryNoReparse(directory, directoryHandle, directoryAttributes, directoryFinalPath)) {
        return Failure(GuardFailure::DirectoryOpen, relativePrefix);
    }
    if (IsReparsePoint(directoryAttributes)) {
        return Failure(relativePrefix.empty() ? GuardFailure::RootReparsePoint : GuardFailure::ReparsePoint,
            relativePrefix);
    }
    if (!IsDirectory(directoryAttributes)) {
        return Failure(relativePrefix.empty() ? GuardFailure::RootNotDirectory : GuardFailure::DirectoryOpen,
            relativePrefix);
    }
    if (!IsFinalPathUnderRoot(directoryFinalPath, rootFinalPath, relativePrefix.empty())) {
        return Failure(GuardFailure::PathEscaped, relativePrefix);
    }

    WIN32_FIND_DATAW data = {};
    std::wstring search = JoinPath(directory, L"*");
    HANDLE find = FindFirstFileW(search.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return Failure(GuardFailure::Enumeration, relativePrefix);
    }

    GuardResult result;
    bool continueEnumeration = true;
    while (continueEnumeration) {
        const std::wstring name = data.cFileName;
        if (name != L"." && name != L"..") {
            const std::wstring relativePath = relativePrefix.empty()
                ? name
                : relativePrefix + L"/" + name;
            if (!IsValidRelativePath(relativePath)) {
                result = Failure(GuardFailure::InvalidRelativePath, relativePath);
                break;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                result = Failure(GuardFailure::ReparsePoint, relativePath);
                break;
            }

            const std::wstring fullPath = JoinPath(directory, name);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                // Do not recurse into an unknown tree: a large unexpected
                // directory must fail fast instead of making the UI hash it.
                // Match case-insensitively here so a case-only rename can
                // reach the canonical collision diagnostic below.
                if (!HasExpectedDescendantNoCase(relativePath)) {
                    result = Failure(GuardFailure::UnknownFile, relativePath);
                    break;
                }
                result = EnumerateDirectory(fullPath, relativePath, rootFinalPath, observed);
                if (!result.ok()) break;
            } else {
                const AssetEntry* expected = FindExpectedExact(relativePath);
                if (!expected) {
                    // A case-only variant is recorded without opening or
                    // hashing it; the final pass reports CaseCollision.
                    if (FindExpectedNoCase(relativePath)) {
                        observed.push_back({relativePath});
                        continueEnumeration = FindNextFileW(find, &data) != FALSE;
                        continue;
                    }
                    result = Failure(GuardFailure::UnknownFile, relativePath);
                    break;
                }
                ScopedHandle fileHandle;
                FILE_ATTRIBUTE_TAG_INFO fileAttributes = {};
                std::wstring fileFinalPath;
                if (!OpenFileNoReparse(fullPath, fileHandle, fileAttributes, fileFinalPath)) {
                    result = Failure(GuardFailure::FileOpen, relativePath);
                    break;
                }
                if (IsReparsePoint(fileAttributes)) {
                    result = Failure(GuardFailure::ReparsePoint, relativePath);
                    break;
                }
                if (IsDirectory(fileAttributes) || !IsFinalPathUnderRoot(fileFinalPath, rootFinalPath, false)) {
                    result = Failure(GuardFailure::PathEscaped, relativePath);
                    break;
                }

                LARGE_INTEGER fileSize = {};
                if (!GetFileSizeEx(fileHandle.get(), &fileSize) || fileSize.QuadPart < 0) {
                    result = Failure(GuardFailure::FileOpen, relativePath, L"could not read file size");
                    break;
                }
                if (static_cast<std::uint64_t>(fileSize.QuadPart) != expected->size) {
                    result = Failure(GuardFailure::SizeMismatch, relativePath);
                    break;
                }
                std::wstring sha256;
                if (!HashFileSha256(fileHandle.get(), sha256)) {
                    result = Failure(GuardFailure::HashFailure, relativePath);
                    break;
                }
                if (sha256 != expected->sha256) {
                    result = Failure(GuardFailure::HashMismatch, relativePath);
                    break;
                }
                observed.push_back({relativePath});
            }
        }
        continueEnumeration = FindNextFileW(find, &data) != FALSE;
    }
    DWORD findError = continueEnumeration ? ERROR_SUCCESS : GetLastError();
    FindClose(find);
    if (!result.ok()) return result;
    if (findError != ERROR_NO_MORE_FILES) return Failure(GuardFailure::Enumeration, relativePrefix);
    return {};
}

const ObservedAsset* FindObservedExact(const std::vector<ObservedAsset>& observed, const wchar_t* path) {
    auto it = std::find_if(observed.begin(), observed.end(), [path](const ObservedAsset& asset) {
        return asset.path == path;
    });
    return it == observed.end() ? nullptr : &*it;
}

const ObservedAsset* FindObservedNoCase(const std::vector<ObservedAsset>& observed, const wchar_t* path) {
    auto it = std::find_if(observed.begin(), observed.end(), [path](const ObservedAsset& asset) {
        return EqualsNoCaseOrdinal(asset.path, path);
    });
    return it == observed.end() ? nullptr : &*it;
}

} // namespace

const wchar_t* GuardFailureName(GuardFailure failure) {
    switch (failure) {
    case GuardFailure::None: return L"none";
    case GuardFailure::RootMissing: return L"missing-root";
    case GuardFailure::RootNotDirectory: return L"root-not-directory";
    case GuardFailure::RootReparsePoint: return L"root-reparse-point";
    case GuardFailure::DirectoryOpen: return L"directory-open-failed";
    case GuardFailure::FileOpen: return L"file-open-failed";
    case GuardFailure::ReparsePoint: return L"reparse-point";
    case GuardFailure::PathEscaped: return L"path-escaped-root";
    case GuardFailure::InvalidRelativePath: return L"invalid-relative-path";
    case GuardFailure::Enumeration: return L"enumeration-failed";
    case GuardFailure::CaseCollision: return L"case-collision";
    case GuardFailure::MissingFile: return L"missing-file";
    case GuardFailure::UnknownFile: return L"unknown-file";
    case GuardFailure::SizeMismatch: return L"size-mismatch";
    case GuardFailure::HashMismatch: return L"sha256-mismatch";
    case GuardFailure::HashFailure: return L"sha256-read-failed";
    }
    return L"unknown";
}

GuardResult VerifyWebAssetDirectory(const std::wstring& assetsRoot) {
    if (assetsRoot.empty()) return Failure(GuardFailure::RootMissing, L"");

    DWORD attributes = GetFileAttributesW(assetsRoot.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return Failure(GuardFailure::RootMissing, L"");
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return Failure(GuardFailure::RootReparsePoint, L"");
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return Failure(GuardFailure::RootNotDirectory, L"");

    ScopedHandle rootHandle;
    FILE_ATTRIBUTE_TAG_INFO rootAttributes = {};
    std::wstring rootFinalPath;
    if (!OpenDirectoryNoReparse(assetsRoot, rootHandle, rootAttributes, rootFinalPath)) {
        return Failure(GuardFailure::DirectoryOpen, L"");
    }
    if (IsReparsePoint(rootAttributes)) return Failure(GuardFailure::RootReparsePoint, L"");
    if (!IsDirectory(rootAttributes)) return Failure(GuardFailure::RootNotDirectory, L"");

    std::vector<ObservedAsset> observed;
    GuardResult enumerated = EnumerateDirectory(assetsRoot, L"", rootFinalPath, observed);
    if (!enumerated.ok()) return enumerated;

    for (size_t i = 0; i < observed.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (EqualsNoCaseOrdinal(observed[i].path, observed[j].path)) {
                return Failure(GuardFailure::CaseCollision, observed[i].path,
                    L"collides with " + observed[j].path);
            }
        }
    }

    for (const AssetEntry& expected : kEntries) {
        const ObservedAsset* actual = FindObservedExact(observed, expected.path);
        if (!actual) {
            const ObservedAsset* caseVariant = FindObservedNoCase(observed, expected.path);
            if (caseVariant) {
                return Failure(GuardFailure::CaseCollision, caseVariant->path,
                    L"expected canonical path " + std::wstring(expected.path));
            }
            return Failure(GuardFailure::MissingFile, expected.path);
        }
    }

    for (const ObservedAsset& actual : observed) {
        if (!FindExpectedExact(actual.path)) return Failure(GuardFailure::UnknownFile, actual.path);
    }
    return {};
}

} // namespace ZenCrop::WebAssets
