#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

struct DashboardSourceEditRequest {
    std::wstring canonicalSource;
    std::wstring offsetUnit;
    size_t sourceStart = 0;
    size_t sourceEnd = 0;
    std::wstring revisionSha256;
    std::wstring expectedSource;
};

class DashboardSourceMap {
public:
    static std::wstring NormalizeLf(const std::wstring& text) {
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

    static std::wstring RevisionSha256(const std::wstring& canonicalLf) {
        if (canonicalLf.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return L"";
        int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            canonicalLf.data(), static_cast<int>(canonicalLf.size()), nullptr, 0, nullptr, nullptr);
        if (byteCount < 0 || (!canonicalLf.empty() && byteCount == 0)) return L"";
        std::vector<unsigned char> utf8(static_cast<size_t>(byteCount));
        if (byteCount > 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                canonicalLf.data(), static_cast<int>(canonicalLf.size()),
                reinterpret_cast<char*>(utf8.data()), byteCount, nullptr, nullptr) != byteCount) {
            return L"";
        }
        unsigned char digest[32] = {};
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status >= 0) status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        if (status >= 0 && !utf8.empty()) {
            status = BCryptHashData(hash, utf8.data(), static_cast<ULONG>(utf8.size()), 0);
        }
        if (status >= 0) status = BCryptFinishHash(hash, digest, static_cast<ULONG>(sizeof(digest)), 0);
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if (status < 0) return L"";
        static constexpr wchar_t kHex[] = L"0123456789abcdef";
        std::wstring hex;
        hex.reserve(64);
        for (unsigned char value : digest) {
            hex.push_back(kHex[value >> 4]);
            hex.push_back(kHex[value & 0x0f]);
        }
        return hex;
    }

    static bool ApplyStrict(const std::wstring& currentSource,
                            const DashboardSourceEditRequest& request,
                            const std::wstring& replacement,
                            std::wstring& updated,
                            std::wstring* errorCode = nullptr) {
        auto fail = [&](const wchar_t* code) {
            if (errorCode) *errorCode = code;
            updated.clear();
            return false;
        };
        if (request.canonicalSource != L"markdown-body-lf" ||
            request.offsetUnit != L"utf16-code-unit") return fail(L"invalid_contract");
        std::wstring canonical = NormalizeLf(currentSource);
        if (request.revisionSha256.size() != 64 ||
            RevisionSha256(canonical) != request.revisionSha256) return fail(L"stale_revision");
        if (request.sourceStart > request.sourceEnd || request.sourceEnd > canonical.size())
            return fail(L"invalid_range");
        if (canonical.compare(request.sourceStart,
                request.sourceEnd - request.sourceStart, request.expectedSource) != 0)
            return fail(L"source_mismatch");
        updated = canonical;
        updated.replace(request.sourceStart, request.sourceEnd - request.sourceStart,
            NormalizeLf(replacement));
        if (errorCode) errorCode->clear();
        return true;
    }
};
