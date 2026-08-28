#pragma once
#include <windows.h>
#include <string>

// Shared JSON + string helpers. Consolidated from Settings.cpp,
// SettingsDialog.cpp, OcrUtils.cpp, OcrEngine_PaddleOCR_Cloud.cpp and
// OcrDashboardWindow.cpp (each used to carry its own static copy, and the
// UnescapeJsonString in Settings.cpp was missing \/, \b, \f and \uXXXX
// handling, which is a real bug when reading back user paths that contain
// forward slashes).

// PaddleOCR cloud API default endpoint. Consolidated from Settings.cpp,
// SettingsDialog.cpp and OcrEngine_PaddleOCR_Cloud.cpp (each carried its own
// static copy with identical content).
extern const wchar_t* kPaddleOcrJobsUrl;

std::wstring TrimString(const std::wstring& value);
std::wstring EscapeJsonString(const std::wstring& value);
std::wstring UnescapeJsonString(const std::wstring& input);
size_t SkipJsonWhitespace(const std::wstring& s, size_t pos);
std::wstring ExtractJsonField(const std::wstring& objStr, const std::wstring& key);

// Case-insensitive string predicates. Consolidated from Settings.cpp,
// SettingsDialog.cpp and OcrEngine_PaddleOCR_Cloud.cpp.
bool StartsWithNoCase(const std::wstring& value, const std::wstring& prefix);
bool ContainsNoCase(const std::wstring& value, const std::wstring& needle);

// Normalize a user-entered PaddleOCR jobs URL. Empty / bare-host inputs fall
// back to kPaddleOcrJobsUrl. Consolidated from Settings.cpp,
// SettingsDialog.cpp and OcrEngine_PaddleOCR_Cloud.cpp.
std::wstring NormalizePaddleOcrJobsUrl(const std::wstring& input);
