#pragma once

#include "core/HttpTransport.h"
#include <string>
#include <vector>

// Neutral cloud resource port; document workflow types stay in ocr/document.
class IPaddleCloudDocumentHttpClient {
public:
    virtual ~IPaddleCloudDocumentHttpClient() = default;
    virtual HttpResponse Post(
        const std::wstring& url,
        const std::string& body,
        const std::vector<std::wstring>& headers,
        int timeoutMs) = 0;
    virtual HttpResponse Get(
        const std::wstring& url,
        const std::vector<std::wstring>& headers,
        int timeoutMs) = 0;
};

class WinHttpPaddleCloudDocumentClient final : public IPaddleCloudDocumentHttpClient {
public:
    HttpResponse Post(
        const std::wstring& url,
        const std::string& body,
        const std::vector<std::wstring>& headers,
        int timeoutMs) override;
    HttpResponse Get(
        const std::wstring& url,
        const std::vector<std::wstring>& headers,
        int timeoutMs) override;
};

// Resource consumers validate public URLs and signed redirect targets.
bool IsSafePaddleCloudResourceUrl(const std::wstring& url, std::wstring& error);
bool IsSamePaddleCloudUrlTarget(const std::wstring& expectedUrl, const std::wstring& actualUrl);
