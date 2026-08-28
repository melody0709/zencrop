#include "ocr/ui/dashboard/DashboardHistoryRepository.h"
#include "ocr/ui/dashboard/DashboardHistoryStore.h"
#include "core/AppDataPaths.h"
#include "core/WideStringUtils.h"

#include <windows.h>
#include <shlwapi.h>

#include <fstream>
#include <string>
#include <vector>

namespace {

// R0-FIX-1: size-based conversion (no embedded NUL sizing).
// Old path used c_str()/-1 so required buffer = payload+NUL, but allocated only
// payload (len-1) and still asked Win32 to write `len` bytes → 1-byte overrun.
std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    if (wstr.size() > static_cast<size_t>(INT_MAX)) return std::string();
    const int srcChars = static_cast<int>(wstr.size());
    int len = WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), srcChars, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string str(static_cast<size_t>(len), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), srcChars, str.data(), len, nullptr, nullptr);
    if (written != len) return std::string();
    return str;
}

std::wstring Utf8ToWString(const std::string& str) {
    if (str.empty()) return L"";
    if (str.size() > static_cast<size_t>(INT_MAX)) return L"";
    const int srcBytes = static_cast<int>(str.size());
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), srcBytes, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(static_cast<size_t>(len), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, 0, str.data(), srcBytes, wstr.data(), len);
    if (written != len) return L"";
    return wstr;
}

bool DecodeUtf8Strict(const std::string& bytes, std::wstring& text) {
    text.clear();
    if (bytes.empty() || bytes.size() > static_cast<size_t>(INT_MAX)) return false;
    int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes.data(),
        static_cast<int>(bytes.size()),
        nullptr,
        0);
    if (length <= 0) return false;
    text.resize(static_cast<size_t>(length));
    return MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes.data(),
        static_cast<int>(bytes.size()),
        text.data(),
        length) == length;
}

bool BackupCorruptFile(const std::wstring& path) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    // OWN-111: pure compact stamp format (WideStringUtils).
    std::wstring stamp = WideFormatCompactStamp(
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    for (int attempt = 0; attempt < 100; attempt++) {
        std::wstring backupPath = path + stamp;
        if (attempt > 0) {
            // OWN-127: pure int label (WideStringUtils).
            backupPath += L".";
            backupPath += WideFormatIntLabel(attempt);
        }
        if (CopyFileW(path.c_str(), backupPath.c_str(), TRUE)) {
            return true;
        }
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    return false;
}

bool AtomicWriteUtf8(const std::wstring& path, const std::string& utf8) {
    std::wstring tmpPath = path + L".tmp";
    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    if (!utf8.empty()) {
        file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }
    file.flush();
    if (!file.good()) {
        file.close();
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    file.close();
    if (!file.good()) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    if (!MoveFileExW(
            tmpPath.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

} // namespace

DashboardHistoryRepository::DashboardHistoryRepository(
    std::wstring historyPath,
    std::wstring dismissedPath)
    : m_historyPath(std::move(historyPath))
    , m_dismissedPath(std::move(dismissedPath))
{
}

std::wstring DashboardHistoryRepository::DefaultHistoryPath() {
    return ZenCropAppDataFilePath(L"ocr_history.json");
}

std::wstring DashboardHistoryRepository::DefaultDismissedPath() {
    return ZenCropAppDataFilePath(L"ocr_dashboard_dismissed.json");
}

DashboardHistoryRepository DashboardHistoryRepository::ForDefaultLocation() {
    return DashboardHistoryRepository(DefaultHistoryPath(), DefaultDismissedPath());
}

bool DashboardHistoryRepository::LoadItems(
    std::vector<OcrDashboardHistoryItem>& items) const
{
    items.clear();
    const std::wstring& path = m_historyPath;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                return true;
            }
        }
        return false;
    }
    std::string utf8((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    std::wstring json = Utf8ToWString(utf8);
    std::vector<OcrDashboardHistoryItem> parsed = DashboardHistoryParseJson(json);
    const bool structurallyComplete = utf8.empty() || DashboardHistoryIsStructurallyCompleteJson(json);
    if (!structurallyComplete ||
        (!utf8.empty() && parsed.empty() && !DashboardHistoryIsEmptyJson(json))) {
        if (!BackupCorruptFile(path)) {
            return false;
        }
        return false;
    }
    items = std::move(parsed);
    return true;
}

bool DashboardHistoryRepository::SaveItems(
    const std::vector<OcrDashboardHistoryItem>& items) const
{
    return AtomicWriteUtf8(
        m_historyPath,
        WStringToUtf8(DashboardHistorySerializeJson(items)));
}

bool DashboardHistoryRepository::LoadDismissedKeys(
    std::set<std::wstring>& keys) const
{
    keys.clear();
    const std::wstring& path = m_dismissedPath;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                return true;
            }
        }
        return false;
    }

    std::string utf8((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    std::wstring json;
    if (!DecodeUtf8Strict(utf8, json) ||
        !DashboardHistoryParseDismissedManifestKeys(json, keys)) {
        BackupCorruptFile(path);
        return false;
    }
    return true;
}

bool DashboardHistoryRepository::SaveDismissedKeys(
    const std::set<std::wstring>& keys) const
{
    return AtomicWriteUtf8(
        m_dismissedPath,
        WStringToUtf8(DashboardHistorySerializeDismissedManifests(keys)));
}
