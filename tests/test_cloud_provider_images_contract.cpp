#include "OcrUtils.h"

#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << "\n";
    return 1;
}

// Minimal 1x1 PNG (base64).
const char* kTinyPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

// Minimal 1x1 JPEG (base64).
const char* kTinyJpegBase64 =
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0a"
    "HBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIy"
    "MjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAABAAEDASIA"
    "AhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAn/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/8QAFQEB"
    "AQAAAAAAAAAAAAAAAAAAAAX/xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oADAMBAAIQAxAAAAGcP//E"
    "ABQQAQAAAAAAAAAAAAAAAAAAAAD/2gAIAQEAAQUCf//EABQRAQAAAAAAAAAAAAAAAAAAAAD/2gAI"
    "AQMBAT8Bf//EABQRAQAAAAAAAAAAAAAAAAAAAAD/2gAIAQIBAT8Bf//EABQQAQAAAAAAAAAAAAAAAA"
    "AAAAD/2gAIAQEABj8Cf//EABQQAQAAAAAAAAAAAAAAAAAAAAD/2gAIAQEAAT8hf//Z";

// Raw PNG bytes matching kTinyPngBase64 (for injectable HTTP downloader).
const unsigned char kTinyPngBytes[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

struct FakeHttpResponse {
    int statusCode = 200;
    std::wstring contentType = L"application/octet-stream";
    std::vector<unsigned char> body;
    std::wstring error;
};

std::map<std::wstring, std::vector<FakeHttpResponse>> g_fakeHttpQueue;
int g_fakeHttpHits = 0;

bool FakeProviderHttpGet(
    const std::wstring& url,
    std::vector<unsigned char>& bytes,
    std::wstring& contentType,
    int& statusCode,
    std::wstring& error)
{
    g_fakeHttpHits++;
    bytes.clear();
    contentType.clear();
    statusCode = 0;
    error.clear();
    auto it = g_fakeHttpQueue.find(url);
    if (it == g_fakeHttpQueue.end() || it->second.empty()) {
        error = L"no fake response queued";
        return false;
    }
    FakeHttpResponse resp = std::move(it->second.front());
    it->second.erase(it->second.begin());
    statusCode = resp.statusCode;
    contentType = resp.contentType;
    error = resp.error;
    if (statusCode != 200 || !error.empty() || resp.body.empty()) {
        return false;
    }
    bytes = std::move(resp.body);
    return true;
}

void QueueFake(const std::wstring& url, FakeHttpResponse resp) {
    g_fakeHttpQueue[url].push_back(std::move(resp));
}

FakeHttpResponse MakeOctetStreamPng() {
    FakeHttpResponse r;
    r.statusCode = 200;
    r.contentType = L"application/octet-stream";
    r.body.assign(kTinyPngBytes, kTinyPngBytes + sizeof(kTinyPngBytes));
    return r;
}

} // namespace

int main() {
    {
        std::string json =
            std::string("{\"layoutParsingResults\":[{\"markdown\":{"
            "\"text\":\"<div><img src=\\\"imgs/img_in_image_box_1.jpg\\\" alt=\\\"Image\\\" /></div>\","
            "\"images\":{\"imgs/img_in_image_box_1.jpg\":\"") +
            kTinyPngBase64 +
            "\"}}}]}";
        std::wstring text =
            L"<div><img src=\"imgs/img_in_image_box_1.jpg\" alt=\"Image\" /></div>";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets);
        if (assets.size() != 1) return Fail("base64 provider image must produce one embedded asset");
        if (assets[0].providerFormat != OcrEmbeddedAssetEncodedFormat::Png) {
            return Fail("base64 provider image must keep PNG signature");
        }
        if (text.find(L"imgs/img_in_image_box_1.jpg") != std::wstring::npos) {
            return Fail("provider image path must be replaced by a placeholder");
        }
        if (text.find(assets[0].placeholderUri) == std::wstring::npos) {
            return Fail("markdown must reference the embedded asset placeholder");
        }
    }

    {
        // Cross-page shared relative path must NOT cross-contaminate.
        // Page1 and Page2 both reference imgs/shared.jpg with different payloads.
        std::string json =
            std::string("{\"layoutParsingResults\":["
            "{\"markdown\":{\"text\":\"page1 ![x](imgs/shared.jpg)\","
            "\"images\":{\"imgs/shared.jpg\":\"") +
            kTinyPngBase64 +
            "\"}}},"
            "{\"markdown\":{\"text\":\"page2 ![y](imgs/shared.jpg)\","
            "\"images\":{\"imgs/shared.jpg\":\"" +
            std::string(kTinyJpegBase64) +
            "\"}}}"
            "]}";
        // processedText is ignored when layoutParsingResults rebuild wins.
        std::wstring text = L"stale";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets);
        if (assets.size() != 2) {
            return Fail("shared imgs path across pages must create two assets");
        }
        if (text.find(L"imgs/shared.jpg") != std::wstring::npos) {
            return Fail("shared imgs path must be fully rewritten in scoped rebuild");
        }
        if (text.find(L"page1") == std::wstring::npos ||
            text.find(L"page2") == std::wstring::npos) {
            return Fail("scoped rebuild must keep both page markdown fragments");
        }
        if (text.find(assets[0].placeholderUri) == std::wstring::npos ||
            text.find(assets[1].placeholderUri) == std::wstring::npos) {
            return Fail("each page must reference its own placeholder");
        }
        // Placeholders must differ so page2 does not silently show page1's image.
        if (assets[0].placeholderUri == assets[1].placeholderUri) {
            return Fail("shared relative path must produce distinct placeholders per page");
        }
        bool sawPng = false;
        bool sawJpeg = false;
        for (const auto& a : assets) {
            if (a.providerFormat == OcrEmbeddedAssetEncodedFormat::Png) sawPng = true;
            if (a.providerFormat == OcrEmbeddedAssetEncodedFormat::Jpeg) sawJpeg = true;
        }
        if (!sawPng || !sawJpeg) {
            return Fail("shared-path pages must keep their own PNG/JPEG bytes");
        }
    }

    {
        // Distinct keys across two layoutParsingResults (regression for multi-map scan).
        std::string json =
            std::string("{\"layoutParsingResults\":["
            "{\"markdown\":{\"text\":\"page1 ![a](imgs/a.png)\","
            "\"images\":{\"imgs/a.png\":\"") +
            kTinyPngBase64 +
            "\"}}},"
            "{\"markdown\":{\"text\":\"page2 ![b](imgs/b.jpg)\","
            "\"images\":{\"imgs/b.jpg\":\"" +
            std::string(kTinyJpegBase64) +
            "\"}}}"
            "]}";
        std::wstring text = L"stale";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets);
        if (assets.size() != 2) {
            return Fail("multi layoutParsingResults must materialize every images map");
        }
    }

    {
        // Image processing must be a no-op when layout results contain no
        // images map/reference; in particular it must not reformat page joins.
        std::string json =
            "{\"layoutParsingResults\":["
            "{\"markdown\":{\"text\":\"page one\"}},"
            "{\"markdown\":{\"text\":\"page two\"}}"
            "]}";
        std::wstring text = ParsePaddleVlResponse(json);
        const std::wstring original = text;
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets);
        if (!assets.empty() || text != original) {
            return Fail("layout results without images must preserve parsed markdown exactly");
        }
    }

    {
        // Injectable HTTP: HTTPS-looking URL + application/octet-stream + PNG magic.
        // This is the field BOS failure mode that previously wiped crop images.
        g_fakeHttpQueue.clear();
        g_fakeHttpHits = 0;
        const std::wstring url =
            L"https://pplines-online.bj.bcebos.com/fake/imgs/crop.png?sig=1";
        QueueFake(url, MakeOctetStreamPng());
        std::string json =
            "{\"layoutParsingResults\":[{\"markdown\":{"
            "\"text\":\"![crop](imgs/crop.png)\","
            "\"images\":{\"imgs/crop.png\":"
            "\"https://pplines-online.bj.bcebos.com/fake/imgs/crop.png?sig=1\""
            "}}}]}";
        std::wstring text = L"stale";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets, &FakeProviderHttpGet);
        if (g_fakeHttpHits < 1) return Fail("injectable downloader was not called");
        if (assets.size() != 1) {
            return Fail("octet-stream PNG via injectible downloader must materialize");
        }
        if (assets[0].providerFormat != OcrEmbeddedAssetEncodedFormat::Png) {
            return Fail("octet-stream body must be detected as PNG by magic");
        }
        if (text.find(L"imgs/crop.png") != std::wstring::npos) {
            return Fail("octet-stream success must replace imgs reference");
        }
        if (text.find(assets[0].placeholderUri) == std::wstring::npos) {
            return Fail("octet-stream success must insert placeholder");
        }
    }

    {
        // Injectable HTTP: 429 then 200 octet-stream must succeed after retry.
        g_fakeHttpQueue.clear();
        g_fakeHttpHits = 0;
        const std::wstring url =
            L"https://pplines-online.bj.bcebos.com/fake/imgs/retry.png";
        FakeHttpResponse fail429;
        fail429.statusCode = 429;
        fail429.error = L"rate limited";
        QueueFake(url, fail429);
        QueueFake(url, MakeOctetStreamPng());
        std::string json =
            "{\"layoutParsingResults\":[{\"markdown\":{"
            "\"text\":\"![r](imgs/retry.png)\","
            "\"images\":{\"imgs/retry.png\":"
            "\"https://pplines-online.bj.bcebos.com/fake/imgs/retry.png\""
            "}}}]}";
        std::wstring text = L"stale";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets, &FakeProviderHttpGet);
        if (g_fakeHttpHits != 2) {
            return Fail("429 then 200 must perform exactly one retry");
        }
        if (assets.size() != 1) {
            return Fail("429 then 200 octet-stream must eventually materialize");
        }
        if (text.find(assets[0].placeholderUri) == std::wstring::npos) {
            return Fail("retry success must rewrite markdown to placeholder");
        }
    }

    {
        g_fakeHttpQueue.clear();
        g_fakeHttpHits = 0;
        const std::wstring url =
            L"https://pplines-online.bj.bcebos.com/fake/imgs/deny.png";
        FakeHttpResponse fail403;
        fail403.statusCode = 403;
        fail403.error = L"forbidden";
        QueueFake(url, fail403);
        std::string json =
            "{\"layoutParsingResults\":[{\"markdown\":{"
            "\"text\":\"![d](imgs/deny.png)\","
            "\"images\":{\"imgs/deny.png\":"
            "\"https://pplines-online.bj.bcebos.com/fake/imgs/deny.png\""
            "}}}]}";
        std::wstring text = L"stale";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets, &FakeProviderHttpGet);
        if (g_fakeHttpHits != 1) {
            return Fail("403 provider image must not retry");
        }
        if (!assets.empty()) return Fail("403 provider image must not create assets");
        if (text.find(L"imgs/deny.png") != std::wstring::npos ||
            text.find(L"bcebos.com") != std::wstring::npos) {
            return Fail("403 provider image must blank markdown reference");
        }
    }

    {
        // Invalid base64 / garbage must blank, not leave imgs path.
        std::string json =
            "{\"markdown\":{\"text\":\"![bad](imgs/bad.bin)\","
            "\"images\":{\"imgs/bad.bin\":\"not-an-image\"}}}";
        std::wstring text = L"![bad](imgs/bad.bin)";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets);
        if (!assets.empty()) return Fail("invalid provider payload must not create assets");
        if (text.find(L"imgs/bad.bin") != std::wstring::npos) {
            return Fail("invalid provider payload must clear the markdown reference");
        }
    }

    {
        // The 32-image budget limits attempted referenced entries, not only
        // successful assets. Fast permanent failures must not issue unbounded
        // provider requests within the job deadline.
        g_fakeHttpQueue.clear();
        g_fakeHttpHits = 0;
        std::string markdown;
        std::string images = "{";
        constexpr int kEntryCount = 35;
        for (int i = 0; i < kEntryCount; ++i) {
            const std::string suffix = std::to_string(i);
            const std::string key = "imgs/limit_" + suffix + ".png";
            const std::string url =
                "https://pplines-online.bj.bcebos.com/fake/imgs/limit_" +
                suffix + ".png";
            if (!markdown.empty()) markdown += " ";
            markdown += "![x](" + key + ")";
            if (i != 0) images += ",";
            images += "\"" + key + "\":\"" + url + "\"";

            FakeHttpResponse denied;
            denied.statusCode = 403;
            denied.error = L"forbidden";
            QueueFake(std::wstring(url.begin(), url.end()), std::move(denied));
        }
        images += "}";
        const std::string json =
            "{\"layoutParsingResults\":[{\"markdown\":{\"text\":\"" +
            markdown + "\",\"images\":" + images + "}}}]}";
        std::wstring text = L"stale";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets, &FakeProviderHttpGet);
        if (g_fakeHttpHits != 32) {
            return Fail("provider image attempt budget must stop downloader after 32 entries");
        }
        if (!assets.empty() || text.find(L"imgs/limit_") != std::wstring::npos) {
            return Fail("attempt-budget overflow entries must be blanked without assets");
        }
    }

    {
        // The injected transport replaces I/O only and must not bypass the
        // production URL policy.
        g_fakeHttpQueue.clear();
        g_fakeHttpHits = 0;
        const std::wstring loopbackUrl = L"http://127.0.0.1:1/loop.png";
        QueueFake(loopbackUrl, MakeOctetStreamPng());
        std::string json =
            "{\"markdown\":{\"text\":\"![loop](imgs/loop.png)\","
            "\"images\":{\"imgs/loop.png\":\"http://127.0.0.1:1/loop.png\"}}}";
        std::wstring text = L"![loop](imgs/loop.png)";
        std::vector<OcrEmbeddedAssetSpec> assets;
        ProcessImagesInResponse(json, text, &assets, &FakeProviderHttpGet);
        if (g_fakeHttpHits != 0) {
            return Fail("injectable transport must not bypass provider URL safety");
        }
        if (!assets.empty()) return Fail("loopback provider URL must not create assets");
        if (text.find(L"imgs/loop.png") != std::wstring::npos ||
            text.find(L"127.0.0.1") != std::wstring::npos) {
            return Fail("rejected remote provider URL must clear the markdown reference");
        }
    }

    std::cout << "PASS cloud provider image materialization contract\n";
    return 0;
}
