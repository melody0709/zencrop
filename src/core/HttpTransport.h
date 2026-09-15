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
    // Provider troubleshooting identifier echoed by the response
    // (SiliconFlow: x-siliconcloud-trace-id). Empty when the provider does not
    // send one. It is surfaced in failure messages so a user report carries an
    // identifier the provider can look up.
    std::wstring traceId;
};

struct HttpRequestOptions {
    int timeoutMs = 15000;
    // 0 = reuse timeoutMs. Only AsyncHttpTransport consumes this field; the
    // synchronous HttpPost/HttpGet implementations in net/Network.cpp keep
    // using a single value for all four WinHTTP timeouts. It exists so the LLM
    // engines can allow a long response generation wait without also dragging
    // DNS resolution / TCP connect out to the same value.
    int receiveTimeoutMs = 0;
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
