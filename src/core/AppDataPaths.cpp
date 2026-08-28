#define WIN32_LEAN_AND_MEAN
#include "AppDataPaths.h"

#include "WideStringUtils.h"

#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace {

std::wstring ModuleDirectory() {
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return L"";
    return WideParentDirFromPath(std::wstring(path.data(), length));
}

std::wstring EnvironmentVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1) return L"";
    std::vector<wchar_t> value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        name, value.data(), static_cast<DWORD>(value.size()));
    if (written == 0 || written >= value.size()) return L"";
    return std::wstring(value.data(), written);
}

std::wstring AbsolutePath(const std::wstring& path) {
    if (path.empty()) return L"";
    std::vector<wchar_t> absolute(32768, L'\0');
    const DWORD length = GetFullPathNameW(
        path.c_str(), static_cast<DWORD>(absolute.size()), absolute.data(), nullptr);
    if (length == 0 || length >= absolute.size()) return L"";
    return std::wstring(absolute.data(), length);
}

bool PathIsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool PathIsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    if (PathIsDirectory(path)) return true;
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_EXISTS &&
        result != ERROR_ALREADY_EXISTS) {
        return false;
    }
    return PathIsDirectory(path);
}

void ReportDirectoryFailure(const std::wstring& path) {
    const std::wstring message =
        L"ZenCrop: application data directory is unavailable: " + path + L"\n";
    OutputDebugStringW(message.c_str());
}

std::wstring DefaultDataDirectory() {
    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(
            nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData)) &&
        localAppData[0] != L'\0') {
        return WideJoinPath(localAppData, L"ZenCrop");
    }

    const std::wstring environmentLocalAppData =
        AbsolutePath(EnvironmentVariable(L"LOCALAPPDATA"));
    if (!environmentLocalAppData.empty()) {
        return WideJoinPath(environmentLocalAppData, L"ZenCrop");
    }

    std::vector<wchar_t> temporary(32768, L'\0');
    const DWORD length = GetTempPathW(
        static_cast<DWORD>(temporary.size()), temporary.data());
    if (length == 0 || length >= temporary.size()) return L"";
    return WideJoinPath(std::wstring(temporary.data(), length), L"ZenCrop");
}

std::wstring ResolveDataDirectory() {
    const std::wstring executableDirectory = ModuleDirectory();

    const std::wstring configuredValue = EnvironmentVariable(L"ZENCROP_DATA_DIR");
    if (!configuredValue.empty()) {
        const std::wstring configuredDirectory = AbsolutePath(configuredValue);
        if (configuredDirectory.empty()) {
            ReportDirectoryFailure(configuredValue);
            return L"";
        }
        if (!EnsureDirectory(configuredDirectory)) {
            ReportDirectoryFailure(configuredDirectory);
        }
        return configuredDirectory;
    }

    if (!executableDirectory.empty() &&
        PathIsFile(WideJoinPath(executableDirectory, L"portable.flag"))) {
        return executableDirectory;
    }

    const std::wstring dataDirectory = DefaultDataDirectory();
    if (!EnsureDirectory(dataDirectory)) {
        ReportDirectoryFailure(dataDirectory);
    }
    return dataDirectory;
}

bool PortableFlagPresent() {
    const std::wstring executableDirectory = ModuleDirectory();
    return !executableDirectory.empty() &&
        PathIsFile(WideJoinPath(executableDirectory, L"portable.flag"));
}

} // namespace

const std::wstring& ZenCropAppDataDirectory() {
    static const std::wstring directory = ResolveDataDirectory();
    return directory;
}

std::wstring ZenCropAppDataFilePath(const wchar_t* fileName) {
    const std::wstring& directory = ZenCropAppDataDirectory();
    if (directory.empty()) return L"";
    return WideJoinPath(directory, fileName ? fileName : L"");
}

bool ZenCropIsPortableMode() {
    return PortableFlagPresent();
}
