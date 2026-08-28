#include "Sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {

class BCryptAlgorithmScope {
public:
    ~BCryptAlgorithmScope() {
        if (handle) BCryptCloseAlgorithmProvider(handle, 0);
    }
    BCRYPT_ALG_HANDLE handle = nullptr;
};

class BCryptHashScope {
public:
    ~BCryptHashScope() {
        if (handle) BCryptDestroyHash(handle);
    }
    BCRYPT_HASH_HANDLE handle = nullptr;
};

bool InitializeSha256(
    BCryptAlgorithmScope& algorithm,
    BCryptHashScope& hash,
    std::vector<unsigned char>& hashObject,
    DWORD& hashLength,
    std::wstring& error)
{
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        error = L"BCryptOpenAlgorithmProvider(SHA-256) failed.";
        return false;
    }

    DWORD objectLength = 0;
    DWORD copied = 0;
    status = BCryptGetProperty(
        algorithm.handle,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength),
        &copied,
        0);
    if (status < 0 || objectLength == 0) {
        error = L"BCrypt SHA-256 object length is unavailable.";
        return false;
    }

    status = BCryptGetProperty(
        algorithm.handle,
        BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashLength),
        sizeof(hashLength),
        &copied,
        0);
    if (status < 0 || hashLength != 32) {
        error = L"BCrypt SHA-256 hash length is invalid.";
        return false;
    }

    hashObject.resize(objectLength);
    status = BCryptCreateHash(
        algorithm.handle,
        &hash.handle,
        hashObject.data(),
        static_cast<ULONG>(hashObject.size()),
        nullptr,
        0,
        0);
    if (status < 0) {
        error = L"BCryptCreateHash(SHA-256) failed.";
        return false;
    }
    return true;
}

bool FinishSha256(
    BCryptHashScope& hash,
    DWORD hashLength,
    std::wstring& sha256,
    std::wstring& error)
{
    std::vector<unsigned char> digest(hashLength);
    NTSTATUS status = BCryptFinishHash(
        hash.handle,
        digest.data(),
        static_cast<ULONG>(digest.size()),
        0);
    if (status < 0) {
        error = L"BCryptFinishHash(SHA-256) failed.";
        return false;
    }

    static constexpr wchar_t kHex[] = L"0123456789abcdef";
    sha256.clear();
    sha256.reserve(digest.size() * 2);
    for (unsigned char byte : digest) {
        sha256.push_back(kHex[(byte >> 4) & 0x0f]);
        sha256.push_back(kHex[byte & 0x0f]);
    }
    return true;
}

std::string WideToUtf8(const std::wstring& text, std::wstring& error) {
    error.clear();
    if (text.empty()) return {};
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        error = L"UTF-16 input is too large to encode for SHA-256.";
        return {};
    }
    int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        error = L"Failed to encode UTF-16 text as UTF-8 for SHA-256.";
        return {};
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            utf8.data(),
            size,
            nullptr,
            nullptr) != size) {
        error = L"Failed to encode complete UTF-16 text as UTF-8 for SHA-256.";
        return {};
    }
    return utf8;
}

} // namespace

bool ComputeSha256Hex(
    const void* data,
    size_t size,
    std::wstring& sha256,
    std::wstring& error)
{
    sha256.clear();
    error.clear();
    if (!data && size != 0) {
        error = L"SHA-256 input pointer is null.";
        return false;
    }

    BCryptAlgorithmScope algorithm;
    BCryptHashScope hash;
    std::vector<unsigned char> hashObject;
    DWORD hashLength = 0;
    if (!InitializeSha256(algorithm, hash, hashObject, hashLength, error)) return false;

    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    size_t offset = 0;
    while (offset < size) {
        ULONG chunk = static_cast<ULONG>((std::min)(
            size - offset,
            static_cast<size_t>((std::numeric_limits<ULONG>::max)())));
        NTSTATUS status = BCryptHashData(
            hash.handle,
            const_cast<PUCHAR>(bytes + offset),
            chunk,
            0);
        if (status < 0) {
            error = L"BCryptHashData(SHA-256) failed.";
            return false;
        }
        offset += chunk;
    }
    return FinishSha256(hash, hashLength, sha256, error);
}

bool ComputeUtf8Sha256Hex(
    const std::wstring& text,
    std::wstring& sha256,
    std::wstring& error)
{
    std::string utf8 = WideToUtf8(text, error);
    if (!error.empty()) return false;
    return ComputeSha256Hex(utf8.data(), utf8.size(), sha256, error);
}

bool ComputeFileSha256Hex(
    const std::wstring& path,
    std::wstring& sha256,
    std::wstring& error)
{
    sha256.clear();
    error.clear();

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Failed to open file for SHA-256: " + path;
        return false;
    }

    BCryptAlgorithmScope algorithm;
    BCryptHashScope hash;
    std::vector<unsigned char> hashObject;
    DWORD hashLength = 0;
    if (!InitializeSha256(algorithm, hash, hashObject, hashLength, error)) {
        CloseHandle(file);
        return false;
    }

    std::vector<unsigned char> buffer(1024 * 1024);
    bool ok = true;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            error = L"Failed to read file for SHA-256: " + path;
            ok = false;
            break;
        }
        if (read == 0) break;
        if (BCryptHashData(hash.handle, buffer.data(), read, 0) < 0) {
            error = L"BCryptHashData(file SHA-256) failed.";
            ok = false;
            break;
        }
    }
    CloseHandle(file);
    return ok && FinishSha256(hash, hashLength, sha256, error);
}

bool IsSha256Hex(const std::wstring& value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'f') ||
            (ch >= L'A' && ch <= L'F');
    });
}
