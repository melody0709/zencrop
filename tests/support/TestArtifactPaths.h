#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

inline std::filesystem::path ZenCropTestArtifactDirectory(const wchar_t* suiteName) {
    std::vector<wchar_t> configured(32768, L'\0');
    const DWORD configuredLength = GetEnvironmentVariableW(
        L"ZENCROP_TEST_OUTPUT_ROOT",
        configured.data(),
        static_cast<DWORD>(configured.size()));

    std::filesystem::path root;
    if (configuredLength > 0 && configuredLength < configured.size()) {
        root = std::wstring(configured.data(), configuredLength);
    } else {
        std::vector<wchar_t> temp(32768, L'\0');
        const DWORD tempLength = GetTempPathW(
            static_cast<DWORD>(temp.size()), temp.data());
        if (tempLength > 0 && tempLength < temp.size()) {
            root = std::wstring(temp.data(), tempLength);
        }
        root /= L"ZenCropTests";
    }

    if (suiteName && *suiteName) root /= suiteName;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return root;
}

