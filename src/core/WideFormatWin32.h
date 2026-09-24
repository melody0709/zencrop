#pragma once

#include <format>
#include <string>

// Pure wide formatting for Win32 API errors, localhost endpoints, HTTP status codes, and server process messages.

inline std::wstring WideFormatWin32ErrorSuffix(const wchar_t* prefix, unsigned long err)
{
    return std::format(L"{}: {}", prefix ? prefix : L"", err);
}

inline std::wstring WideFormatWin32Failed(const wchar_t* apiName, unsigned long err)
{
    return std::format(L"{} failed: {}", apiName ? apiName : L"", err);
}

inline std::wstring WideFormatJobDirError(unsigned long err)
{
    return std::format(L"Failed to create job directory ({}).", err);
}

inline std::wstring WideFormatHttpStatus(int statusCode)
{
    return std::format(L"HTTP {}", statusCode);
}

inline std::wstring WideFormatHttpStatusRejected(int statusCode)
{
    return std::format(L"Endpoint is reachable, but the token was rejected. (HTTP {})", statusCode);
}

inline std::wstring WideFormatHttpJobsEndpointReachable(int statusCode)
{
    return std::format(
        L"Official async jobs endpoint is reachable. (HTTP {})\n\n"
        L"This test does not submit an OCR job; actual OCR will upload an image and poll by jobId.",
        statusCode);
}

inline std::wstring WideFormatEndpointHttp(int statusCode)
{
    return std::format(L"Endpoint returned HTTP {}", statusCode);
}

inline std::wstring WideFormatServerRunningOnPort(int port)
{
    return std::format(L"Server is already running on port {}.", port);
}

inline std::wstring WideFormatServerStartedOnPort(int port)
{
    return std::format(L"Server started successfully on port {}.\nIt is now ready for OCR.", port);
}

inline std::wstring WideFormatServerStartFailedOnPort(int port)
{
    return std::format(L"Failed to start llama-server on port {}.\n\n", port);
}

inline std::wstring WideFormatLocalhostBase(int port)
{
    return std::format(L"http://127.0.0.1:{}", port);
}

inline std::wstring WideFormatLocalhostChatCompletions(int port)
{
    return std::format(L"http://127.0.0.1:{}/v1/chat/completions", port);
}
