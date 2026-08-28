#pragma once

#include <cstddef>
#include <string>

bool ComputeSha256Hex(
    const void* data,
    size_t size,
    std::wstring& sha256,
    std::wstring& error);

bool ComputeUtf8Sha256Hex(
    const std::wstring& text,
    std::wstring& sha256,
    std::wstring& error);

bool ComputeFileSha256Hex(
    const std::wstring& path,
    std::wstring& sha256,
    std::wstring& error);

bool IsSha256Hex(const std::wstring& value);
