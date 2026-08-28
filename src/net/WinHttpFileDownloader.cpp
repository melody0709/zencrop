#include "WinHttpFileDownloader.h"

#include "core/WideFormatUtils.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <cwchar>
#include <limits>

namespace {

class InternetHandle {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value) : value_(value) {}
    ~InternetHandle() { if (value_) WinHttpCloseHandle(value_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    HINTERNET get() const { return value_; }

private:
    HINTERNET value_ = nullptr;
};

class FileHandle {
public:
    explicit FileHandle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~FileHandle() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE get() const { return value_; }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

std::uint64_t ExistingFileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return 0;
    }
    ULARGE_INTEGER size = {};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

std::wstring QueryHeader(HINTERNET request, DWORD query)
{
    DWORD bytes = 0;
    WinHttpQueryHeaders(
        request,
        query,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        &bytes,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) return L"";
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(
            request,
            query,
            WINHTTP_HEADER_NAME_BY_INDEX,
            value.data(),
            &bytes,
            WINHTTP_NO_HEADER_INDEX)) {
        return L"";
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

std::wstring QueryFinalUrl(HINTERNET request)
{
    DWORD bytes = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) return L"";
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, value.data(), &bytes)) return L"";
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

bool ParseHttpsUrl(
    const std::wstring& url,
    std::wstring& host,
    std::wstring& path,
    INTERNET_PORT& port)
{
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = 1;
    components.dwHostNameLength = 1;
    components.dwUrlPathLength = 1;
    components.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }
    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo && components.dwExtraInfoLength) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    port = components.nPort;
    return !host.empty() && !path.empty();
}

bool ParseContentRangeStart(const std::wstring& value, std::uint64_t& start)
{
    start = 0;
    static constexpr wchar_t kPrefix[] = L"bytes ";
    if (value.rfind(kPrefix, 0) != 0) return false;
    const wchar_t* first = value.c_str() + (sizeof(kPrefix) / sizeof(kPrefix[0]) - 1);
    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(first, &end, 10);
    if (end == first || !end || *end != L'-') return false;
    start = static_cast<std::uint64_t>(parsed);
    return true;
}

FileDownloadResult Failure(
    FileDownloadFailure failure,
    const std::wstring& error,
    unsigned long win32Error = 0,
    bool retryable = false)
{
    FileDownloadResult result;
    result.failure = failure;
    result.error = error;
    result.win32Error = win32Error;
    result.retryable = retryable;
    return result;
}

} // namespace

FileDownloadResult WinHttpDownloadFile(
    const FileDownloadRequest& request,
    const std::atomic_bool& cancelRequested,
    const FileDownloadProgressFn& onProgress)
{
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    if (!ParseHttpsUrl(request.url, host, path, port)) {
        return Failure(FileDownloadFailure::InvalidUrl, L"Download URL must be valid HTTPS.");
    }
    if (cancelRequested.load()) {
        return Failure(FileDownloadFailure::Cancelled, L"Download cancelled.");
    }

    const int timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : 15000;
    InternetHandle session(WinHttpOpen(
        L"ZenCrop-ModelDownloader/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.get()) {
        const DWORD error = GetLastError();
        return Failure(
            FileDownloadFailure::Network,
            WideFormatWin32Failed(L"WinHttpOpen", error),
            error,
            true);
    }
    WinHttpSetTimeouts(session.get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!connection.get()) {
        const DWORD error = GetLastError();
        return Failure(
            FileDownloadFailure::Network,
            WideFormatWin32Failed(L"WinHttpConnect", error),
            error,
            true);
    }

    InternetHandle httpRequest(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!httpRequest.get()) {
        const DWORD error = GetLastError();
        return Failure(
            FileDownloadFailure::Network,
            WideFormatWin32Failed(L"WinHttpOpenRequest", error),
            error,
            true);
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(
            httpRequest.get(), WINHTTP_OPTION_REDIRECT_POLICY,
            &redirectPolicy, sizeof(redirectPolicy))) {
        const DWORD error = GetLastError();
        return Failure(
            FileDownloadFailure::RedirectRejected,
            WideFormatWin32Failed(L"WinHttpSetOption(redirect policy)", error),
            error);
    }
    DWORD maximumRedirects = 8;
    WinHttpSetOption(
        httpRequest.get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
        &maximumRedirects, sizeof(maximumRedirects));

    std::uint64_t resumeBytes = request.allowResume
        ? ExistingFileSize(request.destinationPath)
        : 0;
    if (resumeBytes > request.expectedBytes) resumeBytes = 0;
    std::wstring headers;
    if (resumeBytes > 0 && resumeBytes < request.expectedBytes) {
        headers = L"Range: bytes=" + std::to_wstring(resumeBytes) + L"-\r\n";
    } else if (resumeBytes == request.expectedBytes && request.expectedBytes > 0) {
        FileDownloadResult result;
        result.success = true;
        result.finalBytes = resumeBytes;
        if (onProgress) onProgress(resumeBytes, request.expectedBytes);
        return result;
    }

    if (!WinHttpSendRequest(
            httpRequest.get(),
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            static_cast<DWORD>(headers.size()),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(httpRequest.get(), nullptr)) {
        const DWORD error = GetLastError();
        return Failure(
            FileDownloadFailure::Network,
            WideFormatWin32Failed(L"WinHTTP request", error),
            error,
            error == ERROR_WINHTTP_TIMEOUT ||
                error == ERROR_WINHTTP_CONNECTION_ERROR ||
                error == ERROR_WINHTTP_CANNOT_CONNECT);
    }

    DWORD status = 0;
    DWORD statusBytes = sizeof(status);
    if (!WinHttpQueryHeaders(
            httpRequest.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusBytes,
            WINHTTP_NO_HEADER_INDEX)) {
        const DWORD error = GetLastError();
        return Failure(
            FileDownloadFailure::Network,
            WideFormatWin32Failed(L"WinHttpQueryHeaders(status)", error),
            error,
            true);
    }

    FileDownloadResult result;
    result.httpStatus = static_cast<int>(status);
    result.finalUrl = QueryFinalUrl(httpRequest.get());
    std::wstring finalHost;
    std::wstring finalPath;
    INTERNET_PORT finalPort = 0;
    if (result.finalUrl.empty() ||
        !ParseHttpsUrl(result.finalUrl, finalHost, finalPath, finalPort)) {
        result.failure = FileDownloadFailure::RedirectRejected;
        result.error = L"Download redirect left HTTPS or produced an invalid URL.";
        return result;
    }

    bool append = false;
    if (status == 206 && resumeBytes > 0) {
        std::uint64_t contentRangeStart = 0;
        if (!ParseContentRangeStart(
                QueryHeader(httpRequest.get(), WINHTTP_QUERY_CONTENT_RANGE),
                contentRangeStart) ||
            contentRangeStart != resumeBytes) {
            result.failure = FileDownloadFailure::RangeRejected;
            result.error = L"Server returned an invalid Content-Range for resume.";
            return result;
        }
        append = true;
    } else if (status == 200) {
        resumeBytes = 0;
    } else if (status == 416 && resumeBytes == request.expectedBytes) {
        result.success = true;
        result.finalBytes = resumeBytes;
        return result;
    } else {
        result.failure = FileDownloadFailure::HttpStatus;
        result.error = L"Download server returned HTTP " + std::to_wstring(status) + L".";
        result.retryable = status == 408 || status == 429 ||
            status == 500 || status == 502 || status == 503 || status == 504;
        return result;
    }

    FileHandle output(CreateFileW(
        request.destinationPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        append ? OPEN_ALWAYS : CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));
    if (output.get() == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        result.failure = FileDownloadFailure::FileSystem;
        result.error = WideFormatWin32Failed(L"CreateFile(download target)", error);
        result.win32Error = error;
        return result;
    }
    if (append) {
        LARGE_INTEGER offset = {};
        offset.QuadPart = static_cast<LONGLONG>(resumeBytes);
        if (!SetFilePointerEx(output.get(), offset, nullptr, FILE_BEGIN)) {
            const DWORD error = GetLastError();
            result.failure = FileDownloadFailure::FileSystem;
            result.error = WideFormatWin32Failed(L"SetFilePointerEx", error);
            result.win32Error = error;
            return result;
        }
    }

    std::array<unsigned char, 128 * 1024> buffer = {};
    std::uint64_t received = resumeBytes;
    if (onProgress) onProgress(received, request.expectedBytes);
    for (;;) {
        if (cancelRequested.load()) {
            result.failure = FileDownloadFailure::Cancelled;
            result.error = L"Download cancelled.";
            result.finalBytes = received;
            return result;
        }
        DWORD read = 0;
        if (!WinHttpReadData(
                httpRequest.get(), buffer.data(),
                static_cast<DWORD>(buffer.size()), &read)) {
            const DWORD error = GetLastError();
            result.failure = FileDownloadFailure::Network;
            result.error = WideFormatWin32Failed(L"WinHttpReadData", error);
            result.win32Error = error;
            result.retryable = true;
            result.finalBytes = received;
            return result;
        }
        if (read == 0) break;
        if (request.expectedBytes > 0 &&
            (received > request.expectedBytes || read > request.expectedBytes - received)) {
            result.failure = FileDownloadFailure::TruncatedResponse;
            result.error = L"Download exceeded the catalog size.";
            result.finalBytes = received;
            return result;
        }
        DWORD written = 0;
        if (!WriteFile(output.get(), buffer.data(), read, &written, nullptr) || written != read) {
            const DWORD error = GetLastError();
            result.failure = FileDownloadFailure::FileSystem;
            result.error = WideFormatWin32Failed(L"WriteFile(download target)", error);
            result.win32Error = error;
            result.finalBytes = received;
            return result;
        }
        received += written;
        if (onProgress) onProgress(received, request.expectedBytes);
    }

    if (request.expectedBytes > 0 && received != request.expectedBytes) {
        result.failure = FileDownloadFailure::TruncatedResponse;
        result.error = L"Download ended before the catalog size was received.";
        result.retryable = true;
        result.finalBytes = received;
        return result;
    }
    if (!FlushFileBuffers(output.get())) {
        const DWORD error = GetLastError();
        result.failure = FileDownloadFailure::FileSystem;
        result.error = WideFormatWin32Failed(L"FlushFileBuffers", error);
        result.win32Error = error;
        result.finalBytes = received;
        return result;
    }

    result.success = true;
    result.finalBytes = received;
    return result;
}
