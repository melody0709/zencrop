#include "HttpTransport.h"
#include "core/NarrowStringUtils.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"
#include <winhttp.h>
#include <algorithm>
#include <limits>
#include <vector>
#include <stdio.h>

#pragma comment(lib, "winhttp.lib")

namespace {

bool ConfigureRedirectPolicy(
    HINTERNET request,
    bool allowRedirects,
    std::wstring& error)
{
    if (allowRedirects) return true;
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (WinHttpSetOption(
            request,
            WINHTTP_OPTION_REDIRECT_POLICY,
            &policy,
            sizeof(policy))) {
        return true;
    }
    // OWN-120: pure Win32 error format (WideStringUtils).
    error = WideFormatWin32ErrorSuffix(L"Failed to disable HTTP redirects", GetLastError());
    return false;
}

std::wstring QueryHeaderString(HINTERNET request, DWORD query) {
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

std::wstring QueryFinalUrl(HINTERNET request) {
    DWORD bytes = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) return L"";
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, value.data(), &bytes)) return L"";
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

bool ReadResponseBodyBounded(
    HINTERNET request,
    size_t maxBytes,
    std::string& body,
    std::wstring& error)
{
    body.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = WideFormatWin32Failed(L"WinHttpQueryDataAvailable", GetLastError());
            body.clear();
            return false;
        }
        if (available == 0) return true;
        if (body.size() > maxBytes || static_cast<size_t>(available) > maxBytes - body.size()) {
            error = L"HTTP response exceeded the configured byte limit.";
            body.clear();
            return false;
        }
        const size_t oldSize = body.size();
        body.resize(oldSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + oldSize, available, &read)) {
            error = WideFormatWin32Failed(L"WinHttpReadData", GetLastError());
            body.clear();
            return false;
        }
        if (read == 0) {
            error = L"HTTP response ended before advertised bytes were read.";
            body.clear();
            return false;
        }
        body.resize(oldSize + read);
    }
}

void PopulateResponseMetadata(HINTERNET request, HttpResponse& response) {
    response.contentType = QueryHeaderString(request, WINHTTP_QUERY_CONTENT_TYPE);
    response.finalUrl = QueryFinalUrl(request);
}

} // namespace

HttpResponse HttpPost(const std::wstring& url,
                      const std::string& body,
                      const std::vector<std::wstring>& headers,
                      int timeoutMs) {
    HttpRequestOptions options;
    options.timeoutMs = timeoutMs;
    return HttpPost(url, body, headers, options);
}

HttpResponse HttpPost(const std::wstring& url,
                      const std::string& body,
                      const std::vector<std::wstring>& headers,
                      const HttpRequestOptions& options) {
    HttpResponse result;
    const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 15000;

    OutputDebugStringA("[HTTP] Starting request...\n");

    // Parse URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = 1;
    urlComp.dwHostNameLength = 1;
    urlComp.dwUrlPathLength = 1;
    urlComp.dwExtraInfoLength = 1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatHttpCrackUrlFailed(GetLastError()).c_str());
        result.error = L"Invalid URL: " + url;
        return result;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatHttpHostPath(
        host.c_str(), path.c_str(), isHttps ? 1 : 0, urlComp.nPort).c_str());

    // Open session
    HINTERNET hSession = WinHttpOpen(L"ZenCrop/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatHttpOpenFailed(GetLastError()).c_str());
        result.error = L"WinHttpOpen failed";
        return result;
    }

    // Set timeouts
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    // Connect
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        urlComp.nPort, 0);
    if (!hConnect) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatHttpConnectFailed(GetLastError()).c_str());
        result.error = WideFormatWin32Failed(L"WinHttpConnect", GetLastError());
        WinHttpCloseHandle(hSession);
        return result;
    }
    OutputDebugStringA("[HTTP] Connected\n");

    // Open request
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        isHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatHttpOpenRequestFailed(GetLastError()).c_str());
        result.error = WideFormatWin32Failed(L"WinHttpOpenRequest", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    if (!ConfigureRedirectPolicy(hRequest, options.allowRedirects, result.error)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    OutputDebugStringA("[HTTP] Request opened\n");

    // Add headers
    std::wstring allHeaders;
    for (const auto& h : headers) {
        allHeaders += h + L"\r\n";
    }

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatHttpHeaderCount(headers.size()).c_str());
    OutputDebugStringA(NarrowFormatHttpBodySize(body.length()).c_str());

    // Send request
    OutputDebugStringA("[HTTP] Sending request...\n");
    BOOL sent = WinHttpSendRequest(hRequest,
        allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(),
        (DWORD)allHeaders.length(),
        (LPVOID)body.c_str(),
        (DWORD)body.length(),
        (DWORD)body.length(),
        0);

    if (!sent) {
        DWORD err = GetLastError();
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatHttpSendFailed(err).c_str());
        result.error = WideFormatWin32Failed(L"WinHttpSendRequest", err);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    OutputDebugStringA("[HTTP] Request sent\n");

    // Receive response
    OutputDebugStringA("[HTTP] Waiting for response...\n");
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD err = GetLastError();
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatHttpReceiveFailed(err).c_str());

        // Provide more specific error messages
        if (err == ERROR_WINHTTP_TIMEOUT) {
            result.error = L"Request timed out";
        } else if (err == ERROR_WINHTTP_CANNOT_CONNECT) {
            result.error = L"Cannot connect to server";
        } else if (err == ERROR_WINHTTP_CONNECTION_ERROR) {
            result.error = L"Connection error";
        } else if (err == ERROR_WINHTTP_SECURE_FAILURE) {
            result.error = L"SSL/TLS security error";
        } else {
            result.error = WideFormatWin32Failed(L"WinHttpReceiveResponse", err);
        }
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    OutputDebugStringA("[HTTP] Response received\n");
    
    // Get status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    result.statusCode = (int)statusCode;
    PopulateResponseMetadata(hRequest, result);
    
    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatHttpStatusCode(result.statusCode).c_str());

    // Read response body
    OutputDebugStringA("[HTTP] Reading response body...\n");
    ReadResponseBodyBounded(
        hRequest,
        options.maxResponseBytes,
        result.body,
        result.error);

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatHttpResponseBodySize(result.body.length()).c_str());
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    OutputDebugStringA("[HTTP] Request complete\n");
    return result;
}

HttpResponse HttpGet(const std::wstring& url, int timeoutMs) {
    std::vector<std::wstring> headers;
    return HttpGet(url, headers, timeoutMs);
}

HttpResponse HttpGet(const std::wstring& url,
                     const std::vector<std::wstring>& headers,
                     int timeoutMs) {
    HttpRequestOptions options;
    options.timeoutMs = timeoutMs;
    return HttpGet(url, headers, options);
}

HttpResponse HttpGet(const std::wstring& url,
                     const std::vector<std::wstring>& headers,
                     const HttpRequestOptions& options) {
    HttpResponse result;
    const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 15000;
    
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = 1;
    urlComp.dwHostNameLength = 1;
    urlComp.dwUrlPathLength = 1;
    urlComp.dwExtraInfoLength = 1;
    
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        result.error = L"Invalid URL: " + url;
        return result;
    }
    
    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    
    HINTERNET hSession = WinHttpOpen(L"ZenCrop/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        result.error = L"WinHttpOpen failed";
        return result;
    }
    
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        result.error = WideFormatWin32Failed(L"WinHttpConnect", GetLastError());
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        isHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        result.error = WideFormatWin32Failed(L"WinHttpOpenRequest", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    if (!ConfigureRedirectPolicy(hRequest, options.allowRedirects, result.error)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::wstring allHeaders;
    for (const auto& h : headers) {
        allHeaders += h + L"\r\n";
    }

    if (!WinHttpSendRequest(hRequest,
        allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(),
        (DWORD)allHeaders.length(),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        result.error = WideFormatWin32Failed(L"WinHttpSendRequest", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD err = GetLastError();
        if (err == ERROR_WINHTTP_TIMEOUT) {
            result.error = L"Request timed out";
        } else if (err == ERROR_WINHTTP_CANNOT_CONNECT) {
            result.error = L"Cannot connect to server";
        } else if (err == ERROR_WINHTTP_CONNECTION_ERROR) {
            result.error = L"Connection error";
        } else if (err == ERROR_WINHTTP_SECURE_FAILURE) {
            result.error = L"SSL/TLS security error";
        } else {
            result.error = WideFormatWin32Failed(L"WinHttpReceiveResponse", err);
        }
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    result.statusCode = (int)statusCode;
    PopulateResponseMetadata(hRequest, result);

    ReadResponseBodyBounded(
        hRequest,
        options.maxResponseBytes,
        result.body,
        result.error);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return result;
}
