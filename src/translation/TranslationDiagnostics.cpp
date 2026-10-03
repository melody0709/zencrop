#include "TranslationDiagnostics.h"
#include "TranslationTextUtils.h"

#include "core/AppDataPaths.h"
#include "Version.generated.h"

#include <windows.h>

#include <algorithm>
#include <format>

namespace translation {
namespace {

constexpr size_t kMaxDiagnosticFileBytes = 256 * 1024;
constexpr size_t kMaxErrorChars = 160;
constexpr wchar_t kDiagnosticFileName[] = L"translation_diagnostics.log";

// Free-form fields are truncated and stripped of line breaks: one record must
// stay one line.
std::wstring SingleLine(const std::wstring& value) {
    std::wstring result = value.substr(0, kMaxErrorChars);
    TruncateUtf16Safe(result, kMaxErrorChars);
    result.erase(std::remove_if(result.begin(), result.end(),
        [](wchar_t character) { return character == L'\r' || character == L'\n'; }),
        result.end());
    return result;
}

std::wstring BuildLine(const TranslationDiagnosticRecord& record) {
    SYSTEMTIME timestamp{};
    GetSystemTime(&timestamp);
    std::wstring line =
        L"generation=" + std::to_wstring(record.generation) + L" batch=" +
        (record.batchId.empty() ? L"-" : SingleLine(record.batchId)) + L" segments=" +
        std::to_wstring(record.segmentCount) + L" untranslatable=" + std::to_wstring(record.untranslatableCount) +
        L" batches=" + std::to_wstring(record.batchCount) + L" transportRetries=" +
        std::to_wstring(record.transportRetries) + L" contentRetries=" + std::to_wstring(record.contentRetries) +
        L" structured=" + (record.structured ? L"1" : L"0") + L" elapsedMs=" + std::to_wstring(record.elapsedMs) +
        L" outcome=" + record.outcome +
        std::format(L" timestamp={:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", timestamp.wYear, timestamp.wMonth,
                    timestamp.wDay, timestamp.wHour, timestamp.wMinute, timestamp.wSecond, timestamp.wMilliseconds) +
        L" version=" ZENCROP_PRODUCT_VERSION_W + L" provider=" + SingleLine(record.provider) + L" model=" +
        SingleLine(record.model) + L" adapter=" + std::to_wstring(record.adapter) + L" outputMode=" +
        SingleLine(record.outputMode) + L" reasoning=" + std::to_wstring(record.reasoning);
    if (!record.errorCode.empty()) line += L" code=" + record.errorCode;
    if (!record.error.empty()) line += L" error=" + SingleLine(record.error);
    line += L"\r\n";
    return line;
}

void RotateIfNeeded(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return;
    }
    const unsigned long long size =
        (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) |
        attributes.nFileSizeLow;
    if (size < kMaxDiagnosticFileBytes) return;
    MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace

void AppendTranslationDiagnostic(const TranslationDiagnosticRecord& record) {
    const std::wstring path = ZenCropAppDataFilePath(kDiagnosticFileName);
    if (path.empty()) return;
    RotateIfNeeded(path);
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const std::string utf8 = WideToUtf8(BuildLine(record));
    DWORD written = 0;
    if (!utf8.empty()) {
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}

} // namespace translation
