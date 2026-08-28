#pragma once
#include <windows.h>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::wstring contentType;
    std::wstring finalUrl;
    std::wstring error;
};

struct HttpRequestOptions {
    int timeoutMs = 15000;
    int deadlineMs = 0; // 0 = use the transport's normal deadline policy.
    size_t maxResponseBytes = (std::numeric_limits<size_t>::max)();
    bool allowRedirects = true;
};

HttpResponse HttpPost(const std::wstring& url,
                      const std::string& body,
                      const std::vector<std::wstring>& headers,
                      int timeoutMs = 15000);
HttpResponse HttpPost(const std::wstring& url,
                      const std::string& body,
                      const std::vector<std::wstring>& headers,
                      const HttpRequestOptions& options);

HttpResponse HttpGet(const std::wstring& url, int timeoutMs = 15000);
HttpResponse HttpGet(const std::wstring& url,
                     const std::vector<std::wstring>& headers,
                     int timeoutMs = 15000);
HttpResponse HttpGet(const std::wstring& url,
                     const std::vector<std::wstring>& headers,
                     const HttpRequestOptions& options);
