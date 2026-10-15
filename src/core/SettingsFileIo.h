#pragma once

#include <string>

std::wstring ReadFileToString(const std::wstring& path);
// Writes UTF-8 through a flushed temporary file, then atomically replaces the destination.
bool WriteStringToFile(
    const std::wstring& path,
    const std::wstring& content,
    std::wstring* error);
