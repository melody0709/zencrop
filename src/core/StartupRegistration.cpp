#include "StartupRegistration.h"

#include <vector>

namespace {

constexpr wchar_t kRunKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"ZenCrop";

std::wstring CurrentExecutablePath(DWORD* error) {
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (length == 0) {
            *error = GetLastError();
            return {};
        }
        if (length < capacity) {
            *error = ERROR_SUCCESS;
            return std::wstring(buffer.data(), length);
        }
    }

    *error = ERROR_FILENAME_EXCED_RANGE;
    return {};
}

DWORD WriteStartupRegistration() {
    DWORD pathError = ERROR_SUCCESS;
    const std::wstring executablePath = CurrentExecutablePath(&pathError);
    if (pathError != ERROR_SUCCESS) return pathError;

    const std::wstring commandLine = BuildStartupRegistrationCommandLine(executablePath);
    if (!IsStartupRegistrationCommandLineValid(commandLine)) {
        return ERROR_FILENAME_EXCED_RANGE;
    }

    HKEY key = nullptr;
    const LSTATUS openResult = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr);
    if (openResult != ERROR_SUCCESS) return static_cast<DWORD>(openResult);

    const DWORD dataSize = static_cast<DWORD>((commandLine.length() + 1) * sizeof(wchar_t));
    const LSTATUS writeResult = RegSetValueExW(
        key, kRunValueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(commandLine.c_str()), dataSize);
    RegCloseKey(key);
    return static_cast<DWORD>(writeResult);
}

DWORD RemoveStartupRegistration() {
    HKEY key = nullptr;
    const LSTATUS openResult = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (openResult != ERROR_SUCCESS) return static_cast<DWORD>(openResult);

    const LSTATUS deleteResult = RegDeleteValueW(key, kRunValueName);
    RegCloseKey(key);
    return deleteResult == ERROR_FILE_NOT_FOUND
        ? ERROR_SUCCESS
        : static_cast<DWORD>(deleteResult);
}

} // namespace

std::wstring BuildStartupRegistrationCommandLine(const std::wstring& executablePath) {
    if (executablePath.empty()) return {};
    return L"\"" + executablePath + L"\"";
}

bool IsStartupRegistrationCommandLineValid(const std::wstring& commandLine) {
    return !commandLine.empty() &&
        commandLine.length() <= kStartupRegistrationCommandLineMaxChars;
}

StartupRegistrationState QueryZenCropStartupRegistration() {
    HKEY key = nullptr;
    const LSTATUS openResult = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND) return {};
    if (openResult != ERROR_SUCCESS) return { false, static_cast<DWORD>(openResult) };

    const LSTATUS queryResult = RegQueryValueExW(
        key, kRunValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);

    if (queryResult == ERROR_FILE_NOT_FOUND) return {};
    if (queryResult != ERROR_SUCCESS) return { false, static_cast<DWORD>(queryResult) };
    return { true, ERROR_SUCCESS };
}

DWORD SetZenCropStartupRegistration(bool enable) {
    const StartupRegistrationState current = QueryZenCropStartupRegistration();
    if (!current.Succeeded()) return current.error;
    if (current.registered == enable) return ERROR_SUCCESS;

    return enable ? WriteStartupRegistration() : RemoveStartupRegistration();
}
