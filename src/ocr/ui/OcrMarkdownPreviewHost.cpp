#define WIN32_LEAN_AND_MEAN
#include "OcrMarkdownPreviewHost.h"
#include "OcrMarkdownPreviewProtocol.h"
#include "WebAssetGuard.h"
#include "web-assets/WebAssetsManifest.generated.h"

#include "JsonUtils.h"
#include "OcrUtils.h"
#include "ocr/ui/dashboard/DashboardPreviewSecurity.h"
#include "dashboard/DashboardFileTypes.h"
#include "core/Base64.h"
#include "core/WideStringUtils.h"
#include "AppMessages.h"

#include <unknwn.h>
#include <WebView2.h>
#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <shlobj.h>
#include <shlwapi.h>
#include <utility>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

const wchar_t* AssetsHost() {
    return ZenCrop::WebAssets::kHostName;
}

const wchar_t* PreviewUrl() {
    return ZenCrop::WebAssets::kPreviewUrl;
}

const wchar_t* AssetsUrlPrefix() {
    return ZenCrop::WebAssets::kPreviewUrlPrefix;
}

constexpr const wchar_t* kImagesHost = L"zencrop-ocr-images.invalid";
constexpr const wchar_t* kPreviewOutputHost = L"zencrop-preview-output.invalid";
constexpr size_t kMaxPreviewMarkdownChars = 2 * 1024 * 1024;
constexpr size_t kMaxPreviewBlockContentChars = 256 * 1024;
constexpr size_t kMaxPreviewBlocks = 1000;
constexpr size_t kMaxPreviewBlocksPayloadChars = 2 * 1024 * 1024;
constexpr size_t kMaxPreviewWebMessageChars = 2 * 1024 * 1024;
constexpr size_t kMaxStructuredSelectionRequestChars = 1536 * 1024;
constexpr size_t kMaxStructuredSelectionPlanChars = 1024 * 1024;
constexpr double kPreviewZoomMin = 0.25;
constexpr double kPreviewZoomMax = 5.0;
constexpr int kPreviewFontSizeMin = 8;
constexpr int kPreviewFontSizeMax = 32;
constexpr COREWEBVIEW2_COLOR kPreviewBackgroundColor = {255, 30, 30, 30};

std::wstring FormatHResult(HRESULT hr) {
    // OWN-113: pure hex u32 label (WideStringUtils).
    return WideFormatHexU32(static_cast<unsigned int>(hr));
}

// OWN-75: thin wrapper over pure WideStartsWithNoCase.
bool StartsWithNoCaseLocal(const std::wstring& value, const std::wstring& prefix) {
    return WideStartsWithNoCase(value, prefix);
}

bool EnsureDirectory(const std::wstring& dir) {
    if (dir.empty()) return false;
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

std::wstring GetExeDir() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    // OWN-96: pure parent dir (WideStringUtils).
    return WideExeDirFromModulePath(exePath);
}

bool CanonicalizePath(std::wstring path, std::wstring& out) {
    for (wchar_t& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
    wchar_t full[MAX_PATH] = {};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    if (len == 0 || len >= MAX_PATH) return false;
    wchar_t canonical[MAX_PATH] = {};
    if (!PathCanonicalizeW(canonical, full)) return false;
    out = canonical;
    return true;
}

// OWN-75: canonicalize (Win32) then pure path-under via WideStringUtils.
bool IsPathUnderDirectory(const std::wstring& path, const std::wstring& dir) {
    std::wstring fullPath;
    std::wstring fullDir;
    if (!CanonicalizePath(path, fullPath) || !CanonicalizePath(dir, fullDir)) return false;
    return WideIsPathStrictlyUnderDirectoryNoCase(fullPath, fullDir);
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadUtf8TextFileLocal(const std::wstring& path, std::wstring& out) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    std::vector<char> bytes((size_t)size.QuadPart);
    DWORD read = 0;
    BOOL ok = bytes.empty() || ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != bytes.size()) return false;
    size_t offset = bytes.size() >= 3 &&
        (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF ? 3 : 0;
    int len = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + offset, (int)(bytes.size() - offset), nullptr, 0);
    if (len <= 0) return bytes.size() == offset;
    out.resize((size_t)len);
    return MultiByteToWideChar(CP_UTF8, 0, bytes.data() + offset, (int)(bytes.size() - offset),
        out.data(), len) == len;
}

std::wstring CombinePath(std::wstring base, const std::wstring& child) {
    // OWN-96: pure path join (WideStringUtils).
    return WideJoinPath(base, child);
}

bool FindWebAssetsDir(std::wstring& assetsDir, std::wstring& error) {
    const std::wstring candidate = CombinePath(GetExeDir(), L"webview_assets");
    const ZenCrop::WebAssets::GuardResult validation =
        ZenCrop::WebAssets::VerifyWebAssetDirectory(candidate);
    if (!validation.ok()) {
        error = validation.message;
        return false;
    }
    if (!CanonicalizePath(candidate, assetsDir)) {
        error = L"Markdown preview asset validation succeeded but its install path could not be canonicalized.";
        return false;
    }
    return true;
}

std::wstring GetWebViewUserDataFolder() {
    wchar_t localAppData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        return CombinePath(GetExeDir(), L"WebView2UserData");
    }
    std::wstring folder = localAppData;
    folder = CombinePath(folder, L"ZenCrop");
    EnsureDirectory(folder);
    folder = CombinePath(folder, L"WebView2");
    EnsureDirectory(folder);
    return folder;
}

std::wstring UrlDecodeUtf8(const std::wstring& input) {
    std::string bytes;
    bytes.reserve(input.size());
    auto hexValue = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };

    for (size_t i = 0; i < input.size(); i++) {
        wchar_t ch = input[i];
        if (ch == L'%' && i + 2 < input.size()) {
            int hi = hexValue(input[i + 1]);
            int lo = hexValue(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                bytes.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (ch == L'+') {
            bytes.push_back(' ');
        } else if (ch <= 0x7F) {
            bytes.push_back(static_cast<char>(ch));
        } else {
            char mb[8] = {};
            int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, mb, sizeof(mb), nullptr, nullptr);
            if (len > 0) bytes.append(mb, mb + len);
        }
    }

    if (bytes.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    }
    if (wlen <= 0) return L"";
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &result[0], wlen);
    return result;
}

std::wstring UrlEncodePathSegment(const std::wstring& input) {
    // 一次性把整个字符串转 UTF-8 字节流，再逐字节 % 编码。
    // 原实现逐 wchar_t 调用 WideCharToMultiByte，遇到高代理项（emoji 等 U+10000+ 字符）
    // 会因 lone surrogate 返回 0，字符被静默丢弃，生成的 URL 指向不存在的文件名。
    std::string utf8;
    if (!input.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, input.data(), (int)input.size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len <= 0) return L"";
        utf8.resize(static_cast<size_t>(len));
        int written = WideCharToMultiByte(CP_UTF8, 0, input.data(), (int)input.size(),
                                          utf8.data(), len, nullptr, nullptr);
        if (written <= 0) return L"";
        utf8.resize(static_cast<size_t>(written));
    }

    std::wstring out;
    out.reserve(utf8.size() * 3);
    for (unsigned char b : utf8) {
        char c = static_cast<char>(b);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<wchar_t>(c);
            continue;
        }
        // OWN-113: pure URL percent-byte (WideStringUtils).
        out += WideFormatUrlPercentByte(b);
    }
    return out;
}

std::wstring UrlEncodeRelativePath(const std::wstring& input) {
    std::wstring normalized = input;
    for (wchar_t& ch : normalized) {
        if (ch == L'\\') ch = L'/';
    }

    std::wstring out;
    size_t pos = 0;
    while (pos <= normalized.size()) {
        size_t slash = normalized.find(L'/', pos);
        std::wstring segment = slash == std::wstring::npos
            ? normalized.substr(pos)
            : normalized.substr(pos, slash - pos);
        if (segment.empty() || segment == L"." || segment == L"..") {
            return L"";
        }
        if (!out.empty()) out += L"/";
        out += UrlEncodePathSegment(segment);
        if (slash == std::wstring::npos) break;
        pos = slash + 1;
    }
    return out;
}

bool IsAllowedImageExtension(const std::wstring& path) {
    // OWN-95: pure allowed image-ext check (WideStringUtils).
    return WideIsAllowedImageExtension(path);
}

bool TryRewriteLocalOcrImageUrl(const std::wstring& url, std::wstring& rewritten) {
    if (!StartsWithNoCaseLocal(url, L"http://127.0.0.1:") &&
        !StartsWithNoCaseLocal(url, L"http://localhost:")) {
        return false;
    }

    size_t pathParam = url.find(L"?path=");
    if (pathParam == std::wstring::npos) pathParam = url.find(L"&path=");
    if (pathParam == std::wstring::npos) return false;
    pathParam += 6;

    size_t pathEnd = url.find(L'&', pathParam);
    std::wstring encodedPath = pathEnd == std::wstring::npos
        ? url.substr(pathParam)
        : url.substr(pathParam, pathEnd - pathParam);
    std::wstring decodedPath = UrlDecodeUtf8(encodedPath);
    if (decodedPath.empty()) return false;

    std::wstring canonicalPath;
    if (!CanonicalizePath(decodedPath, canonicalPath)) return false;
    if (!IsPathUnderDirectory(canonicalPath, GetOcrImageDir())) return false;
    if (!IsAllowedImageExtension(canonicalPath)) return false;
    if (!FileExists(canonicalPath)) return false;

    std::wstring canonicalDir;
    if (!CanonicalizePath(GetOcrImageDir(), canonicalDir)) return false;
    if (!canonicalDir.empty() && canonicalDir.back() != L'\\') canonicalDir += L'\\';

    // OWN-75: pure lower + pure path-under via WideStringUtils.
    if (!WideIsPathStrictlyUnderDirectoryNoCase(canonicalPath, canonicalDir)) return false;

    std::wstring relativePath = canonicalPath.substr(canonicalDir.size());
    std::wstring encodedRelativePath = UrlEncodeRelativePath(relativePath);
    if (encodedRelativePath.empty()) return false;
    rewritten = std::wstring(L"https://") + kImagesHost + L"/" + encodedRelativePath;
    return true;
}

// OWN-75: thin wrapper over pure WideIsUrlTerminator (union ImageLinks + PreviewHost).
// Terminators cover Markdown URL boundaries: ) ] " ' < > whitespace + ` ; ,
bool IsUrlTerminator(wchar_t ch) {
    return WideIsUrlTerminator(ch);
}

std::wstring RewriteMarkdownOcrImageUrls(const std::wstring& markdown) {
    std::wstring out;
    out.reserve(markdown.size());
    size_t pos = 0;

    while (pos < markdown.size()) {
        size_t next127 = markdown.find(L"http://127.0.0.1:", pos);
        size_t nextLocalhost = markdown.find(L"http://localhost:", pos);
        size_t next = std::wstring::npos;
        if (next127 != std::wstring::npos) next = next127;
        if (nextLocalhost != std::wstring::npos && (next == std::wstring::npos || nextLocalhost < next)) {
            next = nextLocalhost;
        }

        if (next == std::wstring::npos) {
            out.append(markdown, pos, std::wstring::npos);
            break;
        }

        out.append(markdown, pos, next - pos);
        size_t end = next;
        while (end < markdown.size() && !IsUrlTerminator(markdown[end])) end++;
        std::wstring url = markdown.substr(next, end - next);
        std::wstring rewritten;
        if (TryRewriteLocalOcrImageUrl(url, rewritten)) {
            out += rewritten;
        } else {
            out += url;
        }
        pos = end;
    }

    return out;
}

std::wstring BuildRenderMessage(
    int recordId,
    const std::wstring& markdown,
    const std::vector<OcrMarkdownPreviewHost::PreviewBlock>& blocks,
    const std::wstring& hoveredBlockId,
    const std::wstring& selectedBlockId,
    const std::wstring& editingBlockId,
    const std::wstring& originalSourceMarkdown,
    const std::wstring& renderToken = L"",
    bool compactLayout = false)
{
    std::wstring safeMarkdown = markdown.size() > kMaxPreviewMarkdownChars
        ? markdown.substr(0, kMaxPreviewMarkdownChars)
        : markdown;
    const std::wstring& fullSourceMarkdown = originalSourceMarkdown.empty()
        ? markdown
        : originalSourceMarkdown;
    std::wstring canonicalSourceMarkdown =
        DashboardSourceMap::NormalizeLf(fullSourceMarkdown);
    std::wstring sourceMarkdown = canonicalSourceMarkdown.size() > kMaxPreviewMarkdownChars
        ? canonicalSourceMarkdown.substr(0, kMaxPreviewMarkdownChars)
        : canonicalSourceMarkdown;
    if (!sourceMarkdown.empty() &&
        sourceMarkdown.back() >= 0xD800 && sourceMarkdown.back() <= 0xDBFF) {
        sourceMarkdown.pop_back();
    }
    safeMarkdown = RewriteMarkdownOcrImageUrls(safeMarkdown);

    return OcrMarkdownPreviewProtocol::BuildRenderMessage(
        recordId, safeMarkdown, sourceMarkdown,
        DashboardSourceMap::RevisionSha256(canonicalSourceMarkdown), blocks,
        hoveredBlockId, selectedBlockId, editingBlockId, renderToken, compactLayout);
}

std::wstring BuildRenderMessage(int recordId, const std::wstring& markdown) {
    static const std::vector<OcrMarkdownPreviewHost::PreviewBlock> emptyBlocks;
    return BuildRenderMessage(recordId, markdown, emptyBlocks, L"", L"", L"", markdown);
}

bool ParseUnsignedDecimal(const std::wstring& text, unsigned long long& value) {
    if (text.empty()) return false;
    unsigned long long parsed = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return false;
        unsigned digit = static_cast<unsigned>(ch - L'0');
        if (parsed > ((std::numeric_limits<unsigned long long>::max)() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

size_t CountExactOccurrences(const std::wstring& text, const std::wstring& needle) {
    if (needle.empty()) return 0;
    size_t count = 0;
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::wstring::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

bool IsAllowedWebMessageSource(const std::wstring& source) {
    return StartsWithNoCaseLocal(source, AssetsUrlPrefix());
}

bool IsAllowedNavigationUri(const std::wstring& uri) {
    return StartsWithNoCaseLocal(uri, AssetsUrlPrefix());
}

// OWN-75: pure lower via WideStringUtils.
bool IsLoopbackOrInternalHost(std::wstring host) {
    host = WideToLower(std::move(host));
    while (!host.empty() && host.back() == L'.') host.pop_back();
    if (host == L"localhost" || host == L"127.0.0.1" || host == L"[::1]" ||
        host == L"0.0.0.0" || host == AssetsHost() || host == kImagesHost) {
        return true;
    }
    if (!host.empty() && host.front() == L'[') return true;
    if (StartsWithNoCaseLocal(host, L"0.") || StartsWithNoCaseLocal(host, L"10.") ||
        StartsWithNoCaseLocal(host, L"127.") || StartsWithNoCaseLocal(host, L"169.254.") ||
        StartsWithNoCaseLocal(host, L"192.168.")) return true;
    if (StartsWithNoCaseLocal(host, L"172.")) {
        size_t dot = host.find(L'.', 4);
        if (dot != std::wstring::npos) {
            // OWN-77: pure int parse (WideStringUtils).
            int second = WideParseJsonIntToken(host.substr(4, dot - 4));
            if (second >= 16 && second <= 31) return true;
        }
    }
    return false;
}

bool ParseHttpUrlHost(const std::wstring& url, std::wstring& host) {
    std::wstring prefix;
    if (StartsWithNoCaseLocal(url, L"https://")) prefix = L"https://";
    else if (StartsWithNoCaseLocal(url, L"http://")) prefix = L"http://";
    else return false;

    size_t start = prefix.size();
    size_t end = url.find_first_of(L"/?#", start);
    std::wstring authority = end == std::wstring::npos ? url.substr(start) : url.substr(start, end - start);
    if (authority.empty()) return false;
    size_t at = authority.rfind(L'@');
    if (at != std::wstring::npos) {
        authority = authority.substr(at + 1);
    }
    if (authority.empty()) return false;
    if (!authority.empty() && authority.front() == L'[') {
        size_t close = authority.find(L']');
        if (close == std::wstring::npos) return false;
        host = authority.substr(0, close + 1);
    } else {
        size_t colon = authority.find(L':');
        host = colon == std::wstring::npos ? authority : authority.substr(0, colon);
    }
    return !host.empty();
}

bool IsAllowedExternalUrl(const std::wstring& url) {
    std::wstring host;
    if (!ParseHttpUrlHost(url, host)) return false;
    return !IsLoopbackOrInternalHost(host);
}

bool IsCtrlDown() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

bool WideToUtf8Local(const std::wstring& value, std::string& utf8) {
    utf8.clear();
    if (value.empty()) return true;
    if (value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return false;
    utf8.resize(static_cast<size_t>(required));
    return WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), utf8.data(), required,
        nullptr, nullptr) == required;
}

} // namespace

struct OcrMarkdownPreviewHost::Impl {
    struct CallbackState {
        Impl* owner = nullptr;
    };

    HWND parent = nullptr;
    RECT bounds = {};
    Callbacks callbacks;

    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

    std::shared_ptr<CallbackState> state;
    bool creating = false;
    bool ready = false;
    bool failed = false;
    bool visible = false;
    bool verticalScrollbarBoundaryHovered = false;
    double pendingZoomFactor = 1.0;
    int pendingTextFontSize = 14;
    int pendingRecordId = -1;
    std::wstring pendingMarkdown;
    std::wstring pendingSourceMarkdown;
    std::vector<OcrMarkdownPreviewHost::PreviewBlock> pendingBlocks;
    bool pendingCompactLayout = false;
    std::wstring pendingTransientMarkdown;
    int pendingTransientRecordId = -1;
    bool pendingTransientCompactLayout = false;
    bool hasPendingTransientRender = false;
    unsigned long long renderSequence = 0;
    std::wstring pendingRenderToken;
    std::wstring hoveredBlockId;
    std::wstring selectedBlockId;
    std::wstring editingBlockId;
    bool activeEditor = false;
    bool activeEditorDirty = false;
    bool activeEditorComposing = false;
    bool activeEditorCanSave = false;
    bool activeEditorActionPending = false;
    std::wstring assetsDir;
    std::wstring localAssetRoot;
    bool localAssetMappingDirty = false;
    std::wstring localAssetMappingError;
    OcrMarkdownPreviewHost::StructuredSelectionRequest pendingStructuredSelection;
    bool hasPendingStructuredSelection = false;

    Impl() : state(std::make_shared<CallbackState>()) {
        state->owner = this;
    }

    ~Impl() {
        Destroy();
    }

    void SetActiveEditorState(
        bool active, bool dirty, bool composing, bool canSave, bool actionPending) {
        if (!active) {
            dirty = false;
            composing = false;
            canSave = false;
            actionPending = false;
        }
        if (activeEditor == active && activeEditorDirty == dirty &&
            activeEditorComposing == composing && activeEditorCanSave == canSave &&
            activeEditorActionPending == actionPending) {
            return;
        }
        activeEditor = active;
        activeEditorDirty = dirty;
        activeEditorComposing = composing;
        activeEditorCanSave = canSave;
        activeEditorActionPending = actionPending;
        if (callbacks.onPreviewEditorState) {
            callbacks.onPreviewEditorState(activeEditor, activeEditorDirty,
                activeEditorComposing, activeEditorCanSave, activeEditorActionPending);
        }
    }

    bool Create(HWND parentWindow, const RECT& initialBounds, Callbacks cb) {
        // 短路检查必须在重新 make_shared state 之前：否则第二次调用会替换 state 成员，
        // 但第一次异步回调仍持有旧 state shared_ptr，旧 state->owner 还指向 this，
        // 导致新旧回调并存触发 OnEnvironmentCreated，状态机紊乱（creating/ready 错乱、重复 Navigate）。
        if (webview || controller || creating) return true;

        state = std::make_shared<CallbackState>();
        state->owner = this;
        parent = parentWindow;
        bounds = initialBounds;
        callbacks = std::move(cb);
        failed = false;
        ready = false;
        visible = true;

        if (!IsWindow(parent)) {
            Fail(L"Invalid preview parent window");
            return false;
        }
        std::wstring assetValidationError;
        if (!FindWebAssetsDir(assetsDir, assetValidationError)) {
            Fail(assetValidationError);
            return false;
        }

        LPWSTR version = nullptr;
        HRESULT versionHr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
        if (version) CoTaskMemFree(version);
        if (FAILED(versionHr)) {
            Fail(L"Markdown preview unavailable: WebView2 Runtime is missing");
            return false;
        }

        creating = true;
        return StartEnvironmentCreation();
    }

    bool StartEnvironmentCreation() {
        const std::wstring userDataFolder = GetWebViewUserDataFolder();
        auto weakState = state;
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            userDataFolder.c_str(),
            nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [weakState](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (!weakState || !weakState->owner) return S_OK;
                    weakState->owner->OnEnvironmentCreated(result, env);
                    return S_OK;
                }).Get());
        if (SUCCEEDED(hr)) return true;
        creating = false;
        Fail(L"Failed to create WebView2 environment: " + FormatHResult(hr));
        return false;
    }

    void Destroy() {
        if (state) state->owner = nullptr;
        ready = false;
        creating = false;
        pendingMarkdown.clear();
        pendingSourceMarkdown.clear();
        pendingBlocks.clear();
        pendingCompactLayout = false;
        pendingTransientMarkdown.clear();
        pendingTransientRecordId = -1;
        pendingTransientCompactLayout = false;
        hasPendingTransientRender = false;
        pendingRenderToken.clear();
        hoveredBlockId.clear();
        selectedBlockId.clear();
        editingBlockId.clear();
        activeEditor = false;
        activeEditorDirty = false;
        activeEditorComposing = false;
        activeEditorCanSave = false;
        activeEditorActionPending = false;
        verticalScrollbarBoundaryHovered = false;
        pendingStructuredSelection = {};
        hasPendingStructuredSelection = false;
        if (controller) {
            controller->Close();
        }
        webview.Reset();
        controller.Reset();
        environment.Reset();
    }

    void SetBounds(const RECT& rc) {
        if (EqualRect(&bounds, &rc)) return;
        bounds = rc;
        if (controller) {
            controller->put_Bounds(bounds);
        }
    }

    void SetZoomFactor(double zoomFactor) {
        if (!std::isfinite(zoomFactor)) zoomFactor = 1.0;
        const double next = (std::clamp)(zoomFactor, kPreviewZoomMin, kPreviewZoomMax);
        const bool changed = std::abs(next - pendingZoomFactor) >= 0.001;
        pendingZoomFactor = next;
        if (controller) {
            controller->put_ZoomFactor(pendingZoomFactor);
        }
        if (changed && callbacks.onZoomFactorChanged) {
            callbacks.onZoomFactorChanged(pendingZoomFactor);
        }
    }

    bool DispatchAccelerator(UINT virtualKey, bool ctrlDown) {
        if (callbacks.onAcceleratorKey && callbacks.onAcceleratorKey(virtualKey, ctrlDown)) {
            return true;
        }
        if (!ctrlDown) return false;
        if (virtualKey == L'0') {
            SetZoomFactor(1.0);
            return true;
        }
        if (virtualKey == VK_OEM_PLUS || virtualKey == VK_ADD) {
            SetZoomFactor(pendingZoomFactor + 0.1);
            return true;
        }
        if (virtualKey == VK_OEM_MINUS || virtualKey == VK_SUBTRACT) {
            SetZoomFactor(pendingZoomFactor - 0.1);
            return true;
        }
        return false;
    }

    void PostTextFontSize() {
        if (!ready || !webview) return;
        const std::wstring json = L"{\"type\":\"setPreviewFontSize\",\"fontSize\":" +
            std::to_wstring(pendingTextFontSize) + L"}";
        webview->PostWebMessageAsJson(json.c_str());
    }

    std::wstring BuildStructuredSelectionMessage() const {
        const auto& request = pendingStructuredSelection;
        std::wstring json = request.previewSelection
            ? L"{\"type\":\"preparePreviewSelection\""
            : L"{\"type\":\"prepareStructuredSelection\"";
        OcrMarkdownPreviewProtocol::AppendJsonStringField(
            json, L"token", request.token);
        json += L",\"generation\":" + std::to_wstring(request.generation);
        if (request.previewSelection) {
            json += L",\"selectionGeneration\":" +
                std::to_wstring(request.selectionGeneration);
        } else {
            std::string utf8;
            if (!WideToUtf8Local(request.payload, utf8)) return {};
            const std::string encoded = Base64Encode(
                reinterpret_cast<const unsigned char*>(utf8.data()), utf8.size());
            OcrMarkdownPreviewProtocol::AppendJsonStringField(
                json, L"format", request.format);
            OcrMarkdownPreviewProtocol::AppendJsonStringField(
                json, L"payloadBase64",
                std::wstring(encoded.begin(), encoded.end()));
            OcrMarkdownPreviewProtocol::AppendJsonStringField(
                json, L"sourceUrl", request.sourceUrl);
        }
        json += L"}";
        return json.size() <= kMaxStructuredSelectionRequestChars
            ? json : std::wstring();
    }

    void CompleteStructuredSelection(
        const OcrMarkdownPreviewHost::StructuredSelectionRequest& request,
        bool success,
        const std::wstring& planJson,
        const std::wstring& errorCode) {
        if (callbacks.onStructuredSelectionPrepared) {
            callbacks.onStructuredSelectionPrepared(
                request.token, request.generation, success,
                planJson, errorCode);
        }
    }

    void FailPendingStructuredSelection(const std::wstring& errorCode) {
        if (!hasPendingStructuredSelection) return;
        const auto request = pendingStructuredSelection;
        pendingStructuredSelection = {};
        hasPendingStructuredSelection = false;
        CompleteStructuredSelection(request, false, L"", errorCode);
    }

    void PostPendingStructuredSelection() {
        if (!ready || !webview || !hasPendingStructuredSelection) return;
        const std::wstring json = BuildStructuredSelectionMessage();
        if (!json.empty() &&
            SUCCEEDED(webview->PostWebMessageAsJson(json.c_str()))) {
            return;
        }
        const auto request = pendingStructuredSelection;
        pendingStructuredSelection = {};
        hasPendingStructuredSelection = false;
        CompleteStructuredSelection(request, false, L"",
            L"request_too_large_or_post_failed");
    }

    bool PrepareStructuredSelection(
        const OcrMarkdownPreviewHost::StructuredSelectionRequest& request) {
        if (request.token.empty() || request.generation == 0 ||
            (!request.previewSelection &&
             (request.payload.empty() ||
              (request.format != L"html" && request.format != L"markdown")))) {
            return false;
        }
        pendingStructuredSelection = request;
        hasPendingStructuredSelection = true;
        PostPendingStructuredSelection();
        return true;
    }

    void CancelStructuredSelection(
        const std::wstring& token, uint64_t generation,
        const std::wstring& errorCode) {
        if (!hasPendingStructuredSelection ||
            pendingStructuredSelection.token != token ||
            pendingStructuredSelection.generation != generation) {
            return;
        }
        const auto request = pendingStructuredSelection;
        pendingStructuredSelection = {};
        hasPendingStructuredSelection = false;
        CompleteStructuredSelection(request, false, L"", errorCode);
    }

    void SetTextFontSize(int fontSize) {
        pendingTextFontSize = (std::clamp)(fontSize,
            kPreviewFontSizeMin, kPreviewFontSizeMax);
        PostTextFontSize();
    }

    void Show(bool show) {
        if (!show) {
            SetVerticalScrollbarBoundaryHover(false);
            if (callbacks.onPreviewSelectionState) {
                callbacks.onPreviewSelectionState(false, 0);
            }
        }
        if (visible == show) return;
        visible = show;
        if (controller) {
            controller->put_IsVisible(show ? TRUE : FALSE);
        }
        if (show && verticalScrollbarBoundaryHovered) {
            PostVerticalScrollbarBoundaryHover();
        }
    }

    void PostVerticalScrollbarBoundaryHover() {
        if (!ready || !visible || !webview) return;
        webview->PostWebMessageAsJson(verticalScrollbarBoundaryHovered
            ? L"{\"type\":\"previewScrollbarBoundaryEnter\"}"
            : L"{\"type\":\"previewScrollbarBoundaryLeave\"}");
    }

    void SetVerticalScrollbarBoundaryHover(bool hovered) {
        if (verticalScrollbarBoundaryHovered == hovered) return;
        verticalScrollbarBoundaryHovered = hovered;
        PostVerticalScrollbarBoundaryHover();
    }

    void SetLocalAssetRoot(const std::wstring& root) {
        std::wstring canonicalRoot;
        if (!root.empty()) {
            DWORD attrs = GetFileAttributesW(root.c_str());
            if ((attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) ||
                !CanonicalizePath(root, canonicalRoot)) {
                canonicalRoot.clear();
            }
        }
        if (WideEqualsNoCase(localAssetRoot, canonicalRoot) &&
            !localAssetMappingDirty) {
            return;
        }
        localAssetRoot = canonicalRoot;
        if (webview) {
            ApplyLocalAssetRootMapping(true, true);
        }
    }

    void PostPendingRender() {
        if (!ready || !webview) return;
        std::wstring json = BuildRenderMessage(
            pendingRecordId,
            pendingMarkdown,
            pendingBlocks,
            hoveredBlockId,
            selectedBlockId,
            editingBlockId,
            pendingSourceMarkdown,
            pendingRenderToken,
            pendingCompactLayout);
        HRESULT hr = webview->PostWebMessageAsJson(json.c_str());
        if (FAILED(hr) && callbacks.onRenderError) {
            callbacks.onRenderError(
                pendingRecordId,
                L"Failed to post Markdown to preview: " + FormatHResult(hr));
        }
    }

    void RenderMarkdownBlocks(
        int recordId,
        const std::wstring& markdown,
        const std::vector<OcrMarkdownPreviewHost::PreviewBlock>& blocks,
        const std::wstring& sourceMarkdown)
    {
        // OWN-125: pure int labels (WideStringUtils).
        pendingRenderToken = WideFormatIntLabel(++renderSequence);
        pendingRecordId = recordId;
        pendingMarkdown = markdown;
        pendingSourceMarkdown = sourceMarkdown.empty() ? markdown : sourceMarkdown;
        pendingBlocks = blocks;
        pendingTransientMarkdown.clear();
        hasPendingTransientRender = false;
        SetActiveEditorState(false, false, false, false, false);
        PostPendingRender();
    }

    void RenderMarkdown(int recordId, const std::wstring& markdown, bool compactLayout) {
        static const std::vector<OcrMarkdownPreviewHost::PreviewBlock> emptyBlocks;
        pendingCompactLayout = compactLayout;
        RenderMarkdownBlocks(recordId, markdown, emptyBlocks, markdown);
    }

    void RenderTransientMarkdown(int recordId, const std::wstring& markdown, bool compactLayout) {
        if (pendingRenderToken.empty() || activeEditor) return;
        pendingTransientRecordId = recordId;
        pendingTransientMarkdown = markdown;
        pendingTransientCompactLayout = compactLayout;
        hasPendingTransientRender = true;
        PostPendingTransientRender();
    }

    void PostBlockState(const wchar_t* type, const std::wstring& id, bool ensureVisible = false) {
        if (!ready || !webview) return;
        std::wstring json = OcrMarkdownPreviewProtocol::BuildBlockState(type, id, ensureVisible);
        webview->PostWebMessageAsJson(json.c_str());
    }

    void SetHoveredBlock(const std::wstring& id) {
        if (hoveredBlockId == id) return;
        hoveredBlockId = id;
        PostBlockState(L"setPreviewHover", hoveredBlockId);
    }

    void SetSelectedBlock(const std::wstring& id, bool ensureVisible) {
        if (selectedBlockId == id && !ensureVisible) return;
        selectedBlockId = id;
        PostBlockState(L"setPreviewSelection", selectedBlockId, ensureVisible);
    }

    void SetEditingBlock(const std::wstring& id) {
        if (editingBlockId == id) return;
        editingBlockId = id;
        PostBlockState(L"setPreviewEditing", editingBlockId);
    }

    void StartDocumentEditing() {
        if (!ready || !webview) return;
        webview->PostWebMessageAsJson(
            L"{\"type\":\"setPreviewDocumentEditing\",\"editing\":true}");
    }

    void RequestActiveEditorSave() {
        if (!ready || !webview || !activeEditor || activeEditorComposing ||
            activeEditorActionPending) return;
        webview->PostWebMessageAsJson(L"{\"type\":\"requestPreviewEditorSave\"}");
    }

    void CancelActiveEditor() {
        if (!activeEditor || activeEditorActionPending) return;
        if (ready && webview) {
            webview->PostWebMessageAsJson(L"{\"type\":\"requestPreviewEditorCancel\"}");
        }
    }

    void PostPendingTransientRender() {
        if (!ready || !webview || !hasPendingTransientRender || pendingRenderToken.empty()) return;
        std::wstring markdown = pendingTransientMarkdown.size() > kMaxPreviewMarkdownChars
            ? pendingTransientMarkdown.substr(0, kMaxPreviewMarkdownChars)
            : pendingTransientMarkdown;
        markdown = RewriteMarkdownOcrImageUrls(markdown);
        const std::wstring json = OcrMarkdownPreviewProtocol::BuildTransientRenderMessage(
            pendingTransientRecordId, markdown, pendingRenderToken, pendingTransientCompactLayout);
        const HRESULT hr = webview->PostWebMessageAsJson(json.c_str());
        if (FAILED(hr) && callbacks.onRenderError) {
            callbacks.onRenderError(
                pendingTransientRecordId,
                L"Failed to post transient Markdown to preview: " + FormatHResult(hr));
        }
    }

    void PostPreviewBlockSaveResult(
        const std::wstring& id,
        const std::wstring& renderToken,
        bool success,
        const std::wstring& errorCode)
    {
        if (!ready || !webview) return;
        std::wstring json = OcrMarkdownPreviewProtocol::BuildBlockSaveResult(
            id,
            renderToken,
            success,
            errorCode);
        webview->PostWebMessageAsJson(json.c_str());
    }

    void PostPreviewBlockRestoreResult(
        const std::wstring& id,
        const std::wstring& renderToken,
        bool success,
        const std::wstring& errorCode)
    {
        if (!ready || !webview) return;
        std::wstring json = OcrMarkdownPreviewProtocol::BuildBlockRestoreResult(
            id, renderToken, success, errorCode);
        webview->PostWebMessageAsJson(json.c_str());
    }

    void PostPreviewDocumentSaveResult(
        const std::wstring& renderToken,
        bool success,
        const std::wstring& errorCode)
    {
        if (!ready || !webview) return;
        const std::wstring json = OcrMarkdownPreviewProtocol::BuildDocumentSaveResult(
            renderToken, success, errorCode);
        webview->PostWebMessageAsJson(json.c_str());
    }

    void OnEnvironmentCreated(HRESULT result, ICoreWebView2Environment* env) {
        const HRESULT environmentResult = (!env && SUCCEEDED(result))
            ? E_UNEXPECTED
            : result;
        if (FAILED(result) || !env) {
            creating = false;
            Fail(L"Failed to initialize WebView2 environment: " + FormatHResult(environmentResult));
            return;
        }
        environment = env;
        auto weakState = state;
        HRESULT hr = environment->CreateCoreWebView2Controller(
            parent,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [weakState](HRESULT controllerResult,
                            ICoreWebView2Controller* createdController) -> HRESULT {
                    if (!weakState || !weakState->owner) return S_OK;
                    weakState->owner->OnControllerCreated(
                        controllerResult, createdController);
                    return S_OK;
                }).Get());
        if (FAILED(hr)) {
            creating = false;
            Fail(L"Failed to create WebView2 controller: " + FormatHResult(hr));
        }
    }

    void OnControllerCreated(HRESULT result, ICoreWebView2Controller* createdController) {
        creating = false;
        if (FAILED(result) || !createdController) {
            Fail(L"Failed to initialize WebView2 controller: " + FormatHResult(result));
            return;
        }

        controller = createdController;
        ComPtr<ICoreWebView2Controller2> controller2;
        if (SUCCEEDED(controller.As(&controller2)) && controller2) {
            controller2->put_DefaultBackgroundColor(kPreviewBackgroundColor);
        }
        controller->get_CoreWebView2(&webview);
        if (!webview) {
            Fail(L"Failed to get WebView2 core");
            return;
        }

        controller->put_Bounds(bounds);
        controller->put_ZoomFactor(pendingZoomFactor);
        controller->put_IsVisible(visible ? TRUE : FALSE);

        ConfigureSettings();
        ConfigureVirtualHosts();
        if (failed) return;
        RegisterEvents();

        HRESULT hr = webview->Navigate(PreviewUrl());
        if (FAILED(hr)) {
            Fail(L"Failed to navigate Markdown preview: " + FormatHResult(hr));
        }
    }

    void ConfigureSettings() {
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
            settings->put_IsWebMessageEnabled(TRUE);
            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
#ifdef _DEBUG
            settings->put_AreDevToolsEnabled(TRUE);
#else
            settings->put_AreDevToolsEnabled(FALSE);
#endif
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);
        }
    }

    void ConfigureVirtualHosts() {
        ComPtr<ICoreWebView2_3> webview3;
        if (FAILED(webview.As(&webview3)) || !webview3) {
            Fail(L"WebView2 runtime does not support virtual host mappings");
            return;
        }

        HRESULT hr = webview3->SetVirtualHostNameToFolderMapping(
            AssetsHost(),
            assetsDir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        if (FAILED(hr)) {
            Fail(L"Failed to map Markdown preview assets: " + FormatHResult(hr));
            return;
        }

        std::wstring ocrImageDir = GetOcrImageDir();
        hr = webview3->SetVirtualHostNameToFolderMapping(
            kImagesHost,
            ocrImageDir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        if (FAILED(hr)) {
            Fail(L"Failed to map OCR image assets: " + FormatHResult(hr));
            return;
        }

        if (!ApplyLocalAssetRootMapping(false, false)) {
            Fail(localAssetMappingError.empty()
                ? L"Failed to configure local preview asset mapping"
                : localAssetMappingError);
        }
    }

    bool FailLocalAssetMapping(const std::wstring& message, bool notifyRenderError) {
        localAssetMappingDirty = true;
        localAssetMappingError = message;
        OutputDebugStringW((L"[OCR Preview] " + message + L"\n").c_str());
        if (notifyRenderError && callbacks.onRenderError) {
            callbacks.onRenderError(pendingRecordId, message);
        }
        return false;
    }

    bool ApplyLocalAssetRootMapping(bool reloadPage, bool notifyRenderError) {
        localAssetMappingError.clear();
        if (reloadPage) {
            // Do not post into the old document once a replacement mapping was
            // requested, including while a failed replacement is retried.
            ready = false;
        }
        if (!webview) {
            return FailLocalAssetMapping(
                L"Markdown preview WebView is unavailable for local asset mapping.",
                notifyRenderError);
        }
        ComPtr<ICoreWebView2_3> webview3;
        if (FAILED(webview.As(&webview3)) || !webview3) {
            const std::wstring message =
                L"WebView2 runtime does not support local preview asset mapping.";
            return FailLocalAssetMapping(message, notifyRenderError);
        }

        if (reloadPage) {
            // Resource loaders belonging to the current WebView2 document retain
            // its old virtual-host mapping. Stop posting into that document before
            // replacing the mapping, then reload to recreate those loaders.
            HRESULT clearHr = webview3->ClearVirtualHostNameToFolderMapping(kPreviewOutputHost);
            if (FAILED(clearHr)) {
                return FailLocalAssetMapping(
                    L"Failed to clear local preview asset mapping: " + FormatHResult(clearHr),
                    notifyRenderError);
            }
        }

        HRESULT hr = S_OK;
        if (!localAssetRoot.empty()) {
            hr = webview3->SetVirtualHostNameToFolderMapping(
                kPreviewOutputHost,
                localAssetRoot.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        if (FAILED(hr)) {
            return FailLocalAssetMapping(
                L"Failed to map local preview assets: " + FormatHResult(hr),
                notifyRenderError);
        }

        if (reloadPage) {
            hr = webview->Reload();
            if (FAILED(hr)) {
                return FailLocalAssetMapping(
                    L"Failed to reload Markdown preview after changing assets: " + FormatHResult(hr),
                    notifyRenderError);
            }
        }
        localAssetMappingDirty = false;
        localAssetMappingError.clear();
        return true;
    }

    void RegisterEvents() {
        auto weakState = state;
        EventRegistrationToken token = {};
        webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [weakState](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    if (!weakState || !weakState->owner || !args) return S_OK;
                    LPWSTR uriRaw = nullptr;
                    if (SUCCEEDED(args->get_Uri(&uriRaw)) && uriRaw) {
                        std::wstring uri(uriRaw);
                        CoTaskMemFree(uriRaw);
                        if (!IsAllowedNavigationUri(uri)) {
                            args->put_Cancel(TRUE);
                        }
                    } else {
                        args->put_Cancel(TRUE);
                    }
                    return S_OK;
                }).Get(),
            &token);

        weakState = state;
        token = {};
        webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [weakState](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    if (args) args->put_Handled(TRUE);
                    return S_OK;
                }).Get(),
            &token);

        weakState = state;
        token = {};
        webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [weakState](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    if (!weakState || !weakState->owner || !args) return S_OK;
                    weakState->owner->HandleWebMessage(args);
                    return S_OK;
                }).Get(),
            &token);

        weakState = state;
        token = {};
        controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [weakState](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                    if (!weakState || !weakState->owner || !args) return S_OK;
                    weakState->owner->HandleAcceleratorKey(args);
                    return S_OK;
                }).Get(),
            &token);

        weakState = state;
        token = {};
        controller->add_ZoomFactorChanged(
            Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                [weakState](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                    if (!weakState || !weakState->owner ||
                        !weakState->owner->controller) return S_OK;
                    double zoomFactor = 1.0;
                    if (SUCCEEDED(weakState->owner->controller->get_ZoomFactor(&zoomFactor)) &&
                        std::isfinite(zoomFactor) &&
                        weakState->owner->callbacks.onZoomFactorChanged) {
                        weakState->owner->pendingZoomFactor = (std::clamp)(zoomFactor,
                            kPreviewZoomMin, kPreviewZoomMax);
                        weakState->owner->callbacks.onZoomFactorChanged(
                            weakState->owner->pendingZoomFactor);
                    }
                    return S_OK;
                }).Get(),
            &token);

        weakState = state;
        token = {};
        webview->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                 [weakState](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                     if (!weakState || !weakState->owner) return S_OK;
                     weakState->owner->ready = false;
                     weakState->owner->failed = true;
                     weakState->owner->FailPendingStructuredSelection(
                         L"webview_process_failed");
                     if (!weakState->owner) return S_OK;
                     if (weakState->owner->callbacks.onProcessFailed) {
                         weakState->owner->callbacks.onProcessFailed();
                     }
                    return S_OK;
                }).Get(),
            &token);
    }

    void HandleAcceleratorKey(ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
        COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP;
        UINT virtualKey = 0;
        if (FAILED(args->get_KeyEventKind(&kind)) || FAILED(args->get_VirtualKey(&virtualKey))) {
            return;
        }
        if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
            kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            return;
        }
        if (DispatchAccelerator(virtualKey, IsCtrlDown())) {
            args->put_Handled(TRUE);
        }
    }

    void HandleWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args) {
        LPWSTR sourceRaw = nullptr;
        std::wstring source;
        if (SUCCEEDED(args->get_Source(&sourceRaw)) && sourceRaw) {
            source = sourceRaw;
            CoTaskMemFree(sourceRaw);
        }
        if (!IsAllowedWebMessageSource(source)) return;

        LPWSTR jsonRaw = nullptr;
        if (FAILED(args->get_WebMessageAsJson(&jsonRaw)) || !jsonRaw) return;
        std::wstring json(jsonRaw);
        CoTaskMemFree(jsonRaw);
        if (json.size() > kMaxPreviewWebMessageChars) return;

        std::wstring type = UnescapeJsonString(ExtractJsonField(json, L"type"));
        if (type == L"ready") {
            ready = true;
            failed = false;
            PostTextFontSize();
            const std::wstring renderTokenBeforeCallback = pendingRenderToken;
            if (callbacks.onReady) callbacks.onReady();
            if (verticalScrollbarBoundaryHovered) {
                PostVerticalScrollbarBoundaryHover();
            }
            if (pendingRenderToken == renderTokenBeforeCallback &&
                (!pendingMarkdown.empty() || pendingRecordId >= 0 || !pendingBlocks.empty())) {
                PostPendingRender();
                PostPendingTransientRender();
            }
            PostPendingStructuredSelection();
        } else if (type == L"renderError") {
            std::wstring record = ExtractJsonField(json, L"recordId");
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            std::wstring message = UnescapeJsonString(ExtractJsonField(json, L"message"));
            // OWN-77: pure int parse (WideStringUtils).
            int recordId = record.empty() ? -1 : WideParseJsonIntToken(record, -1);
            // A render can be superseded while Markdown is still being
            // parsed. Never let an older document hide a newer valid preview.
            // Token-less messages are accepted only when no token is pending,
            // which preserves compatibility without weakening the stale guard.
            const bool tokenMatches = DashboardPreviewRenderTokenMatches(
                pendingRenderToken, renderToken) ||
                (pendingRenderToken.empty() && renderToken.empty());
            if (tokenMatches && callbacks.onRenderError) {
                callbacks.onRenderError(recordId, message);
            }
        } else if (type == L"previewContentMetrics") {
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            const bool tokenMatches = DashboardPreviewRenderTokenMatches(
                pendingRenderToken, renderToken) ||
                (pendingRenderToken.empty() && renderToken.empty());
            if (!tokenMatches || !callbacks.onContentMetrics) return;

            const int scrollHeight = WideParseJsonIntToken(
                ExtractJsonField(json, L"scrollHeight"), 0);
            const int scrollWidth = WideParseJsonIntToken(
                ExtractJsonField(json, L"scrollWidth"), 0);
            const int clientWidth = WideParseJsonIntToken(
                ExtractJsonField(json, L"clientWidth"), 0);
            if (scrollHeight <= 0 || scrollWidth <= 0 || clientWidth <= 0) return;

            OcrMarkdownPreviewHost::PreviewContentMetrics metrics;
            metrics.scrollHeight = (std::min)(scrollHeight, 4 * 1024 * 1024);
            metrics.scrollWidth = (std::min)(scrollWidth, 4 * 1024 * 1024);
            metrics.clientWidth = (std::min)(clientWidth, 4 * 1024 * 1024);
            metrics.renderToken = renderToken;
            callbacks.onContentMetrics(metrics);
        } else if (type == L"previewImageError") {
            std::wstring record = ExtractJsonField(json, L"recordId");
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            std::wstring src = UnescapeJsonString(ExtractJsonField(json, L"src"));
            // OWN-77: pure int parse (WideStringUtils).
            int recordId = record.empty() ? -1 : WideParseJsonIntToken(record, -1);
            if (DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken) &&
                callbacks.onImageLoadError) {
                callbacks.onImageLoadError(recordId, src);
            }
        } else if (type == L"openExternal") {
            std::wstring url = UnescapeJsonString(ExtractJsonField(json, L"url"));
            if (IsAllowedExternalUrl(url) && callbacks.onOpenExternal) {
                callbacks.onOpenExternal(url);
            }
        } else if (type == L"acceleratorKey") {
            // OWN-77: pure int parse (WideStringUtils).
            int virtualKey = WideParseJsonIntToken(ExtractJsonField(json, L"virtualKey"));
            // OWN-80: pure bool parse (WideStringUtils).
            bool ctrlDown = WideParseJsonBoolToken(ExtractJsonField(json, L"ctrlKey"));
            if (virtualKey > 0) DispatchAccelerator((UINT)virtualKey, ctrlDown);
        } else if (type == L"previewZoomStep") {
            const int step = WideParseJsonIntToken(ExtractJsonField(json, L"step"));
            if (step != 0) SetZoomFactor(pendingZoomFactor + (step > 0 ? 0.1 : -0.1));
        } else if (type == L"previewSelectionState") {
            const bool hasSelection = WideParseJsonBoolToken(
                ExtractJsonField(json, L"hasSelection"));
            unsigned long long selectionGeneration = 0;
            if (ParseUnsignedDecimal(
                    ExtractJsonField(json, L"selectionGeneration"),
                    selectionGeneration) &&
                callbacks.onPreviewSelectionState) {
                callbacks.onPreviewSelectionState(
                    hasSelection, static_cast<uint64_t>(selectionGeneration));
            }
        } else if (type == L"structuredSelectionPrepared") {
            const std::wstring token = UnescapeJsonString(
                ExtractJsonField(json, L"token"));
            unsigned long long parsedGeneration = 0;
            if (!ParseUnsignedDecimal(
                    ExtractJsonField(json, L"generation"),
                    parsedGeneration) || !hasPendingStructuredSelection ||
                token != pendingStructuredSelection.token ||
                parsedGeneration != pendingStructuredSelection.generation) {
                return;
            }
            const bool success = WideParseJsonBoolToken(
                ExtractJsonField(json, L"success"));
            std::wstring planJson = UnescapeJsonString(
                ExtractJsonField(json, L"planJson"));
            std::wstring errorCode = UnescapeJsonString(
                ExtractJsonField(json, L"errorCode"));
            if (planJson.size() > kMaxStructuredSelectionPlanChars) {
                planJson.clear();
                errorCode = L"plan_too_large";
            }
            const auto request = pendingStructuredSelection;
            pendingStructuredSelection = {};
            hasPendingStructuredSelection = false;
            CompleteStructuredSelection(request,
                success && !planJson.empty(), planJson,
                success && planJson.empty() && errorCode.empty()
                    ? L"empty_plan" : errorCode);
        } else if (type == L"previewDocumentEdit") {
            const bool sourceRequired = WideParseJsonBoolToken(
                ExtractJsonField(json, L"sourceRequired"));
            if (callbacks.onPreviewDocumentEdit) {
                callbacks.onPreviewDocumentEdit(sourceRequired);
            }
        } else if (type == L"previewEditorState") {
            const std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            if (!DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken)) return;
            const bool active = WideParseJsonBoolToken(ExtractJsonField(json, L"active"));
            SetActiveEditorState(active,
                WideParseJsonBoolToken(ExtractJsonField(json, L"dirty")),
                WideParseJsonBoolToken(ExtractJsonField(json, L"composing")),
                WideParseJsonBoolToken(ExtractJsonField(json, L"canSave")),
                WideParseJsonBoolToken(ExtractJsonField(json, L"pending")));
        } else if (type == L"previewDocumentSave") {
            const std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            const std::wstring content = UnescapeJsonString(ExtractJsonField(json, L"content"));
            const std::wstring revision = UnescapeJsonString(ExtractJsonField(json, L"revisionSha256"));
            const bool validToken = DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken);
            const bool validRevision = !revision.empty() &&
                revision == DashboardSourceMap::RevisionSha256(
                    DashboardSourceMap::NormalizeLf(pendingSourceMarkdown));
            if (!validToken) {
                PostPreviewDocumentSaveResult(renderToken, false, L"stale_target");
            } else if (!validRevision || content.size() > kMaxPreviewMarkdownChars) {
                PostPreviewDocumentSaveResult(renderToken, false, L"invalid_request");
            } else if (callbacks.onPreviewDocumentSave) {
                callbacks.onPreviewDocumentSave(content, renderToken);
            } else {
                PostPreviewDocumentSaveResult(renderToken, false, L"persist_failed");
            }
        } else if (type == L"previewDocumentCancel") {
            const std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            if (!DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken)) return;
            SetActiveEditorState(false, false, false, false, false);
            if (callbacks.onPreviewDocumentCancel) callbacks.onPreviewDocumentCancel();
        } else if (type == L"previewBlockHover") {
            std::wstring id = UnescapeJsonString(ExtractJsonField(json, L"id"));
            hoveredBlockId = id;
            if (callbacks.onPreviewBlockHover) callbacks.onPreviewBlockHover(id);
        } else if (type == L"previewBlockSelect") {
            std::wstring id = UnescapeJsonString(ExtractJsonField(json, L"id"));
            selectedBlockId = id;
            if (callbacks.onPreviewBlockSelect) callbacks.onPreviewBlockSelect(id);
        } else if (type == L"previewBlockEdit") {
            std::wstring id = UnescapeJsonString(ExtractJsonField(json, L"id"));
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            bool knownBlock = std::any_of(pendingBlocks.begin(), pendingBlocks.end(),
                [&](const OcrMarkdownPreviewHost::PreviewBlock& block) {
                    return block.id == id && block.editable;
                });
            if (DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken) && knownBlock) {
                editingBlockId = id;
                if (callbacks.onPreviewBlockEdit) callbacks.onPreviewBlockEdit(id);
            }
        } else if (type == L"previewBlockEditFailed") {
            std::wstring id = UnescapeJsonString(ExtractJsonField(json, L"id"));
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            if (DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken) &&
                id == editingBlockId) {
                editingBlockId.clear();
                if (callbacks.onPreviewBlockCancel) callbacks.onPreviewBlockCancel(id);
            }
        } else if (type == L"previewBlockSave") {
            std::wstring id = UnescapeJsonString(ExtractJsonField(json, L"id"));
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            std::wstring content = UnescapeJsonString(ExtractJsonField(json, L"content"));
            DashboardSourceEditRequest sourceEdit;
            sourceEdit.canonicalSource = UnescapeJsonString(ExtractJsonField(json, L"canonicalSource"));
            sourceEdit.offsetUnit = UnescapeJsonString(ExtractJsonField(json, L"offsetUnit"));
            sourceEdit.revisionSha256 = UnescapeJsonString(ExtractJsonField(json, L"revisionSha256"));
            sourceEdit.expectedSource = UnescapeJsonString(ExtractJsonField(json, L"expectedSource"));
            unsigned long long parsedStart = 0;
            unsigned long long parsedEnd = 0;
            bool validStart = ParseUnsignedDecimal(ExtractJsonField(json, L"sourceStart"), parsedStart) &&
                parsedStart <= (std::numeric_limits<size_t>::max)();
            bool validEnd = ParseUnsignedDecimal(ExtractJsonField(json, L"sourceEnd"), parsedEnd) &&
                parsedEnd <= (std::numeric_limits<size_t>::max)();
            sourceEdit.sourceStart = validStart ? static_cast<size_t>(parsedStart) : 0;
            sourceEdit.sourceEnd = validEnd ? static_cast<size_t>(parsedEnd) : 0;
            bool knownBlock = std::any_of(pendingBlocks.begin(), pendingBlocks.end(),
                [&](const OcrMarkdownPreviewHost::PreviewBlock& block) {
                    return block.id == id && block.editable;
                });
            bool validTarget = !id.empty() &&
                DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken) &&
                knownBlock;
            std::wstring validatedSource;
            bool validSource = validStart && validEnd && !sourceEdit.expectedSource.empty() &&
                DashboardSourceMap::ApplyStrict(pendingSourceMarkdown, sourceEdit,
                    sourceEdit.expectedSource, validatedSource);
            bool validSize = content.size() <= kMaxPreviewBlockContentChars &&
                sourceEdit.expectedSource.size() <= kMaxPreviewMarkdownChars;
            if (!validTarget) {
                PostPreviewBlockSaveResult(id, renderToken, false, L"stale_target");
            } else if (!validSource || !validSize) {
                PostPreviewBlockSaveResult(id, renderToken, false, L"invalid_request");
            } else if (callbacks.onPreviewBlockSave) {
                callbacks.onPreviewBlockSave(id, content, sourceEdit, renderToken);
            } else {
                PostPreviewBlockSaveResult(id, renderToken, false, L"persist_failed");
            }
        } else if (type == L"previewBlockRestore") {
            std::wstring id = UnescapeJsonString(ExtractJsonField(json, L"id"));
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            DashboardSourceEditRequest sourceEdit;
            sourceEdit.canonicalSource = UnescapeJsonString(ExtractJsonField(json, L"canonicalSource"));
            sourceEdit.offsetUnit = UnescapeJsonString(ExtractJsonField(json, L"offsetUnit"));
            sourceEdit.revisionSha256 = UnescapeJsonString(ExtractJsonField(json, L"revisionSha256"));
            sourceEdit.expectedSource = UnescapeJsonString(ExtractJsonField(json, L"expectedSource"));
            unsigned long long parsedStart = 0;
            unsigned long long parsedEnd = 0;
            bool validStart = ParseUnsignedDecimal(ExtractJsonField(json, L"sourceStart"), parsedStart) &&
                parsedStart <= (std::numeric_limits<size_t>::max)();
            bool validEnd = ParseUnsignedDecimal(ExtractJsonField(json, L"sourceEnd"), parsedEnd) &&
                parsedEnd <= (std::numeric_limits<size_t>::max)();
            sourceEdit.sourceStart = validStart ? static_cast<size_t>(parsedStart) : 0;
            sourceEdit.sourceEnd = validEnd ? static_cast<size_t>(parsedEnd) : 0;
            auto blockIt = std::find_if(pendingBlocks.begin(), pendingBlocks.end(),
                [&](const OcrMarkdownPreviewHost::PreviewBlock& block) { return block.id == id; });
            bool validToken =
                DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken);
            bool knownBlock = !id.empty() && blockIt != pendingBlocks.end() && blockIt->editable;
            bool canRestore = knownBlock && blockIt->canRestoreOriginal;
            std::wstring validatedSource;
            bool validSource = validStart && validEnd && !sourceEdit.expectedSource.empty() &&
                DashboardSourceMap::ApplyStrict(pendingSourceMarkdown, sourceEdit,
                    sourceEdit.expectedSource, validatedSource);
            bool validSize = sourceEdit.expectedSource.size() <= kMaxPreviewMarkdownChars;
            if (!validToken) {
                PostPreviewBlockRestoreResult(id, renderToken, false, L"stale_target");
            } else if (!canRestore) {
                PostPreviewBlockRestoreResult(id, renderToken, false, L"restore_unavailable");
            } else if (!validSource || !validSize) {
                PostPreviewBlockRestoreResult(id, renderToken, false, L"invalid_request");
            } else if (callbacks.onPreviewBlockRestore) {
                callbacks.onPreviewBlockRestore(id, sourceEdit, renderToken);
            } else {
                PostPreviewBlockRestoreResult(id, renderToken, false, L"persist_failed");
            }
        } else if (type == L"previewBlockCancel") {
            std::wstring id = UnescapeJsonString(ExtractJsonField(json, L"id"));
            std::wstring renderToken = UnescapeJsonString(ExtractJsonField(json, L"renderToken"));
            if (DashboardPreviewRenderTokenMatches(pendingRenderToken, renderToken) &&
                id == editingBlockId) {
                editingBlockId.clear();
                if (callbacks.onPreviewBlockCancel) callbacks.onPreviewBlockCancel(id);
            }
        }
    }

    void Fail(const std::wstring& message) {
        failed = true;
        ready = false;
        creating = false;
        FailPendingStructuredSelection(L"preview_unavailable");
        if (callbacks.onUnavailable) callbacks.onUnavailable(message);
    }

    bool IsAvailable() const {
        return !failed && ready && webview;
    }

    bool IsCreating() const {
        // Controller creation completes before the document posts "ready".
        // Keep that navigation gap in the initializing state so callers do
        // not discard a healthy host or mark its queued render as failed.
        return creating || (!ready && !failed &&
            (environment || controller || webview));
    }

#ifdef ZENCROP_PREVIEW_HOST_TESTS
    bool ExecuteScriptForTests(
        const std::wstring& script,
        std::function<void(bool, const std::wstring&)> callback)
    {
        if (!webview || script.empty() || !callback) return false;
        auto sharedCallback =
            std::make_shared<std::function<void(bool, const std::wstring&)>>(std::move(callback));
        HRESULT hr = webview->ExecuteScript(
            script.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [sharedCallback](HRESULT result, LPCWSTR json) -> HRESULT {
                    (*sharedCallback)(SUCCEEDED(result), json ? json : L"");
                    return S_OK;
                }).Get());
        if (FAILED(hr)) {
            (*sharedCallback)(false, L"");
            return false;
        }
        return true;
    }
#endif
};

OcrMarkdownPreviewHost::OcrMarkdownPreviewHost()
    : m_impl(std::make_unique<Impl>()) {
}

OcrMarkdownPreviewHost::~OcrMarkdownPreviewHost() = default;

bool OcrMarkdownPreviewHost::Create(HWND parent, const RECT& bounds, Callbacks callbacks) {
    return m_impl->Create(parent, bounds, std::move(callbacks));
}

void OcrMarkdownPreviewHost::Destroy() {
    m_impl->Destroy();
}

void OcrMarkdownPreviewHost::SetBounds(const RECT& bounds) {
    m_impl->SetBounds(bounds);
}

void OcrMarkdownPreviewHost::SetZoomFactor(double zoomFactor) {
    m_impl->SetZoomFactor(zoomFactor);
}

void OcrMarkdownPreviewHost::SetTextFontSize(int fontSize) {
    m_impl->SetTextFontSize(fontSize);
}

void OcrMarkdownPreviewHost::Show(bool visible) {
    m_impl->Show(visible);
}

void OcrMarkdownPreviewHost::SetVerticalScrollbarBoundaryHover(bool hovered) {
    m_impl->SetVerticalScrollbarBoundaryHover(hovered);
}

void OcrMarkdownPreviewHost::SetLocalAssetRoot(const std::wstring& root) {
    m_impl->SetLocalAssetRoot(root);
}

void OcrMarkdownPreviewHost::RenderMarkdown(int recordId, const std::wstring& markdown, bool compactLayout) {
    m_impl->RenderMarkdown(recordId, markdown, compactLayout);
}

void OcrMarkdownPreviewHost::RenderTransientMarkdown(int recordId, const std::wstring& markdown, bool compactLayout) {
    m_impl->RenderTransientMarkdown(recordId, markdown, compactLayout);
}

void OcrMarkdownPreviewHost::RenderMarkdownBlocks(
    int recordId,
    const std::wstring& markdown,
    const std::vector<PreviewBlock>& blocks,
    const std::wstring& sourceMarkdown)
{
    m_impl->RenderMarkdownBlocks(recordId, markdown, blocks, sourceMarkdown);
}

void OcrMarkdownPreviewHost::SetHoveredBlock(const std::wstring& id) {
    m_impl->SetHoveredBlock(id);
}

void OcrMarkdownPreviewHost::SetSelectedBlock(const std::wstring& id, bool ensureVisible) {
    m_impl->SetSelectedBlock(id, ensureVisible);
}

void OcrMarkdownPreviewHost::SetEditingBlock(const std::wstring& id) {
    m_impl->SetEditingBlock(id);
}

void OcrMarkdownPreviewHost::StartDocumentEditing() {
    m_impl->StartDocumentEditing();
}

void OcrMarkdownPreviewHost::RequestActiveEditorSave() {
    m_impl->RequestActiveEditorSave();
}

void OcrMarkdownPreviewHost::CancelActiveEditor() {
    m_impl->CancelActiveEditor();
}

void OcrMarkdownPreviewHost::PostPreviewBlockSaveResult(
    const std::wstring& id,
    const std::wstring& renderToken,
    bool success,
    const std::wstring& errorCode)
{
    m_impl->PostPreviewBlockSaveResult(id, renderToken, success, errorCode);
}

void OcrMarkdownPreviewHost::PostPreviewBlockRestoreResult(
    const std::wstring& id,
    const std::wstring& renderToken,
    bool success,
    const std::wstring& errorCode)
{
    m_impl->PostPreviewBlockRestoreResult(id, renderToken, success, errorCode);
}

void OcrMarkdownPreviewHost::PostPreviewDocumentSaveResult(
    const std::wstring& renderToken,
    bool success,
    const std::wstring& errorCode)
{
    m_impl->PostPreviewDocumentSaveResult(renderToken, success, errorCode);
}

bool OcrMarkdownPreviewHost::PrepareStructuredSelection(
    const StructuredSelectionRequest& request) {
    return m_impl->PrepareStructuredSelection(request);
}

void OcrMarkdownPreviewHost::CancelStructuredSelection(
    const std::wstring& token, uint64_t generation,
    const std::wstring& errorCode) {
    m_impl->CancelStructuredSelection(token, generation, errorCode);
}

bool OcrMarkdownPreviewHost::IsReady() const {
    return m_impl->ready;
}

bool OcrMarkdownPreviewHost::IsAvailable() const {
    return m_impl->IsAvailable();
}

bool OcrMarkdownPreviewHost::IsCreating() const {
    return m_impl->IsCreating();
}

bool OcrMarkdownPreviewHost::HasActiveEditor() const {
    return m_impl->activeEditor;
}

bool OcrMarkdownPreviewHost::HasDirtyEditor() const {
    return m_impl->activeEditor && m_impl->activeEditorDirty;
}

bool OcrMarkdownPreviewHost::IsEditorComposing() const {
    return m_impl->activeEditor && m_impl->activeEditorComposing;
}

bool OcrMarkdownPreviewHost::CanSaveActiveEditor() const {
    return m_impl->activeEditor && m_impl->activeEditorCanSave;
}

bool OcrMarkdownPreviewHost::IsEditorActionPending() const {
    return m_impl->activeEditor && m_impl->activeEditorActionPending;
}

#ifdef ZENCROP_PREVIEW_HOST_TESTS
bool OcrMarkdownPreviewHost::ExecuteScriptForTests(
    const std::wstring& script,
    std::function<void(bool, const std::wstring&)> callback)
{
    return m_impl->ExecuteScriptForTests(script, std::move(callback));
}

bool OcrMarkdownPreviewHost::RunStaticContractForTests(std::wstring& error) {
    std::wstring assetsDir;
    if (!FindWebAssetsDir(assetsDir, error)) {
        return false;
    }

    const wchar_t* requiredAssets[] = {
        L"ocr-preview\\index.html",
        L"ocr-preview\\preview.js",
        L"ocr-preview\\markdown.js",
        L"ocr-preview\\blocks.js",
        L"ocr-preview\\editor-table.js",
        L"ocr-preview\\editor-markdown.js",
        L"ocr-preview\\rich-editor-ui.js",
        L"ocr-preview\\rich-editor.js",
        L"ocr-preview\\edit-transaction.js",
        L"ocr-preview\\formula-editor.js",
        L"ocr-preview\\structured-selection.js",
        L"ocr-preview\\security.js",
        L"ocr-preview\\preview.css",
        L"vendor\\markdown-it.min.js",
        L"vendor\\purify.min.js",
        L"vendor\\turndown\\turndown.js",
        L"vendor\\turndown\\turndown-plugin-gfm.js",
        L"vendor\\turndown\\LICENSE.txt"
    };
    for (const wchar_t* asset : requiredAssets) {
        if (!FileExists(CombinePath(assetsDir, asset))) {
            error = std::wstring(L"missing preview asset: ") + asset;
            return false;
        }
    }

    std::wstring ocrDir = GetOcrImageDir();
    EnsureDirectory(ocrDir);
    std::wstring fakeImage = CombinePath(ocrDir, L"preview test.png");
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82
    };
    HANDLE file = CreateFileW(fakeImage.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"failed to create preview image fixture";
        return false;
    }
    DWORD written = 0;
    BOOL wrote = WriteFile(file, kPng, static_cast<DWORD>(sizeof(kPng)), &written, nullptr);
    CloseHandle(file);
    if (!wrote || written != sizeof(kPng)) {
        error = L"failed to write preview image fixture";
        return false;
    }

    std::wstring encodedPath = UrlEncodeRelativePath(fakeImage);
    std::wstring localUrl = L"http://127.0.0.1:53210/ocr-image?path=" + encodedPath;
    std::wstring markdown = L"# Preview\n\n![scan](" + localUrl + L")\n\n\"quoted\"\nline";
    std::wstring message = BuildRenderMessage(42, markdown);
    if (message.find(L"\"recordId\":42") == std::wstring::npos ||
        message.find(L"\\\"quoted\\\"") == std::wstring::npos ||
        message.find(L"\\nline") == std::wstring::npos) {
        error = L"render message JSON escaping contract failed";
        return false;
    }
    if (message.find(L"\"sourceMarkdown\":") == std::wstring::npos ||
        message.find(localUrl) == std::wstring::npos ||
        message.find(std::wstring(L"https://") + kImagesHost + L"/preview%20test.png") == std::wstring::npos) {
        error = L"local OCR image URL rewrite contract failed";
        return false;
    }
    const std::wstring transientMessage =
        OcrMarkdownPreviewProtocol::BuildTransientRenderMessage(
            42, L"streamed\nmarkdown", L"stream-token", true);
    if (transientMessage.find(L"\"type\":\"renderTransient\"") == std::wstring::npos ||
        transientMessage.find(L"\"renderToken\":\"stream-token\"") == std::wstring::npos ||
        transientMessage.find(L"streamed\\nmarkdown") == std::wstring::npos ||
        transientMessage.find(L"\"compactLayout\":true") == std::wstring::npos) {
        error = L"transient render message contract failed";
        return false;
    }

    std::wstring outsideUrl = L"http://localhost:53210/ocr-image?path=C%3A%5CWindows%5Cnotepad.exe";
    std::wstring outsideMessage = BuildRenderMessage(1, outsideUrl);
    if (outsideMessage.find(outsideUrl) == std::wstring::npos) {
        error = L"outside OCR image URL should not be rewritten";
        return false;
    }

    std::wstring longSource(kMaxPreviewMarkdownChars + 64, L'x');
    longSource.replace(32, 2, L"\r\n");
    static const std::vector<OcrMarkdownPreviewHost::PreviewBlock> noBlocks;
    std::wstring longMessage = BuildRenderMessage(
        2, L"# visible prefix", noBlocks, L"", L"", L"", longSource, L"long");
    std::wstring longCanonical = DashboardSourceMap::NormalizeLf(longSource);
    std::wstring transmittedSource =
        UnescapeJsonString(ExtractJsonField(longMessage, L"sourceMarkdown"));
    std::wstring transmittedRevision =
        UnescapeJsonString(ExtractJsonField(longMessage, L"revisionSha256"));
    if (transmittedSource.size() > kMaxPreviewMarkdownChars ||
        transmittedSource != longCanonical.substr(0, transmittedSource.size()) ||
        transmittedRevision != DashboardSourceMap::RevisionSha256(longCanonical) ||
        transmittedRevision == DashboardSourceMap::RevisionSha256(transmittedSource)) {
        error = L"long preview source must transmit a truncated mapping prefix with the full canonical revision";
        return false;
    }

    std::vector<OcrMarkdownPreviewHost::PreviewBlock> previewBlocks;
    OcrMarkdownPreviewHost::PreviewBlock block;
    block.id = L"page_1:block_1";
    block.pageIndex = 0;
    block.order = 1;
    block.label = L"doc_title";
    block.displayLabel = L"Title";
    block.content = L"## Block title";
    block.groupId = L"group_1";
    block.contentOwnerId = L"page_1:block_1";
    block.edited = true;
        block.canRestoreOriginal = true;
        block.editable = true;
    previewBlocks.push_back(block);
    std::wstring blockMessage = BuildRenderMessage(
        43,
        markdown,
        previewBlocks,
        L"page_1:block_1",
        L"page_1:block_1",
        L"",
        markdown);
    if (blockMessage.find(L"\"blocks\":[") == std::wstring::npos ||
        blockMessage.find(L"\"id\":\"page_1:block_1\"") == std::wstring::npos ||
        blockMessage.find(L"\"edited\":true") == std::wstring::npos ||
        blockMessage.find(L"\"groupId\":\"group_1\"") == std::wstring::npos ||
        blockMessage.find(L"\"contentOwnerId\":\"page_1:block_1\"") == std::wstring::npos ||
        blockMessage.find(L"\"canRestoreOriginal\":true") == std::wstring::npos ||
        blockMessage.find(L"\"editable\":true") == std::wstring::npos ||
        blockMessage.find(L"\"selectedBlockId\":\"page_1:block_1\"") == std::wstring::npos) {
        error = L"block-aware preview render message contract failed";
        return false;
    }
    std::vector<OcrMarkdownPreviewHost::PreviewBlock> oversizedBlocks(20, block);
    for (size_t i = 0; i < oversizedBlocks.size(); ++i) {
        // OWN-125: pure count prefix (WideStringUtils).
        oversizedBlocks[i].id = WideFormatCountPrefix(L"oversized:", (int)i);
        oversizedBlocks[i].content.assign(kMaxPreviewBlockContentChars, L'x');
    }
    std::wstring oversizedMessage = BuildRenderMessage(
        44, markdown, oversizedBlocks, L"", L"", L"", markdown);
    if (oversizedMessage.size() > 3 * 1024 * 1024 ||
        oversizedMessage.find(L"\"blocksTruncated\":true") == std::wstring::npos) {
        error = L"preview block aggregate payload limit failed";
        return false;
    }
    std::wstring stateMessage = OcrMarkdownPreviewProtocol::BuildBlockState(L"setPreviewSelection", L"page_1:block_1", true);
    if (stateMessage.find(L"\"type\":\"setPreviewSelection\"") == std::wstring::npos ||
        stateMessage.find(L"\"ensureVisible\":true") == std::wstring::npos) {
        error = L"preview block state message contract failed";
        return false;
    }
    std::wstring staleRestoreMessage = OcrMarkdownPreviewProtocol::BuildBlockRestoreResult(
        L"page_1:block_1", L"old", false, L"stale_target");
    if (staleRestoreMessage.find(L"\"type\":\"previewBlockRestoreResult\"") == std::wstring::npos ||
        staleRestoreMessage.find(L"\"errorCode\":\"stale_target\"") == std::wstring::npos) {
        error = L"stale Restore OCR result message contract failed";
        return false;
    }
    const std::wstring documentSaveMessage =
        OcrMarkdownPreviewProtocol::BuildDocumentSaveResult(
            L"document-token", false, L"stale_target");
    if (documentSaveMessage.find(L"\"type\":\"previewDocumentSaveResult\"") ==
            std::wstring::npos ||
        documentSaveMessage.find(L"\"renderToken\":\"document-token\"") ==
            std::wstring::npos) {
        error = L"preview document save result message contract failed";
        return false;
    }

    std::wstring previewJs;
    std::wstring previewJsPath = CombinePath(assetsDir, L"ocr-preview\\preview.js");
    if (!ReadUtf8TextFileLocal(previewJsPath, previewJs)) {
        error = L"preview block interaction script could not be read";
        return false;
    }
    const wchar_t* requiredPreviewTokens[] = {
        L"previewBlockHover", L"setPreviewSelection", L"decorateRenderedMarkdownWithBlocks",
        L"ocr-preview-linked-block", L"requestPreviewEdit", L"Restore OCR",
        L"previewDocumentEdit", L"previewEditorState", L"previewDocumentSave",
        L"setPreviewDocumentEditing", L"requestPreviewEditorSave", L"requestPreviewEditorCancel",
        L"previewZoomStep", L"renderTransient",
        L"stale_target: \"The preview changed before the restore completed",
        L"event.key === \"Escape\"", L"type: \"previewBlockSelect\", id: \"\"",
        L"revisionSha256", L"buildFormulaEditor", L"buildTableEditor",
        L"ocr-preview-table-grid-scroll", L"buildImageEditor",
        L"ocr-preview-image-editor-preview", L"ocr-preview-block-limit-notice",
        L"observeRenderedImages", L"ocr-preview-inline-editor",
        L"ocr-preview-floating-toolbar", L"renderError",
        L"renderToken: currentRenderToken", L"previewScrollbarBoundaryEnter",
        L"previewScrollbarBoundaryLeave", L"scheduleOverlayScrollbarReveal",
        L"setOverlayScrollbarBoundaryHover", L"syncOverlayScrollbar",
        L"beginOverlayScrollbarDrag",
        L"preview-scrollbar"
    };
    for (const wchar_t* token : requiredPreviewTokens) {
        if (previewJs.find(token) == std::wstring::npos) {
            error = std::wstring(L"preview block interaction script is missing token: ") + token;
            return false;
        }
    }
    std::wstring previewHtml;
    std::wstring previewCss;
    if (!ReadUtf8TextFileLocal(
            CombinePath(assetsDir, L"ocr-preview\\index.html"), previewHtml) ||
        previewHtml.find(L"id=\"preview-shell\"") == std::wstring::npos ||
        previewHtml.find(L"id=\"preview-scrollbar\"") == std::wstring::npos ||
        !ReadUtf8TextFileLocal(
            CombinePath(assetsDir, L"ocr-preview\\preview.css"), previewCss) ||
        previewCss.find(L"#preview::-webkit-scrollbar {\n  width: 0;\n}") == std::wstring::npos ||
        previewCss.find(L".preview-scrollbar.is-visible") == std::wstring::npos) {
        error = L"preview overlay scrollbar asset contract failed";
        return false;
    }
    std::wstring formulaEditorJs;
    std::wstring editorMarkdownJs;
    std::wstring richEditorJs;
    std::wstring editTransactionJs;
    std::wstring securityJs;
    if (!ReadUtf8TextFileLocal(
            CombinePath(assetsDir, L"ocr-preview\\formula-editor.js"), formulaEditorJs) ||
        formulaEditorJs.find(L"ocr-preview-formula-preview") == std::wstring::npos ||
        !ReadUtf8TextFileLocal(
            CombinePath(assetsDir, L"ocr-preview\\editor-markdown.js"), editorMarkdownJs) ||
        editorMarkdownJs.find(L"function list(") == std::wstring::npos ||
        !ReadUtf8TextFileLocal(
            CombinePath(assetsDir, L"ocr-preview\\rich-editor.js"), richEditorJs) ||
        richEditorJs.find(L"ocr-preview-rich-editor-toolbar") != std::wstring::npos ||
        !ReadUtf8TextFileLocal(
            CombinePath(assetsDir, L"ocr-preview\\edit-transaction.js"), editTransactionJs) ||
        editTransactionJs.find(L"previewBlockSave") == std::wstring::npos ||
        editTransactionJs.find(L"previewBlockRestore") == std::wstring::npos ||
        editTransactionJs.find(L"sourceStart") == std::wstring::npos ||
        editTransactionJs.find(L"expectedSource") == std::wstring::npos ||
        !ReadUtf8TextFileLocal(
            CombinePath(assetsDir, L"ocr-preview\\security.js"), securityJs) ||
        securityJs.find(L"previewImageError") == std::wstring::npos ||
        securityJs.find(L"isPreviewAssetsHost") == std::wstring::npos) {
        error = L"preview editor module contract failed";
        return false;
    }
    std::wstring blocksJs;
    std::wstring blocksJsPath = CombinePath(assetsDir, L"ocr-preview\\blocks.js");
    if (!ReadUtf8TextFileLocal(blocksJsPath, blocksJs) ||
        blocksJs.find(L"contentOwnerId") == std::wstring::npos ||
        blocksJs.find(L"isLayoutOnly") == std::wstring::npos ||
        blocksJs.find(L"alignCandidates") == std::wstring::npos ||
        blocksJs.find(L"findBestRange") == std::wstring::npos) {
        error = L"preview block mapping script contract failed";
        return false;
    }
    if (previewJs.find(L"preview-blocks") != std::wstring::npos ||
        previewJs.find(L"Empty block") != std::wstring::npos ||
        previewJs.find(L"renderBlocks(payload") != std::wstring::npos) {
        error = L"preview should preserve markdown rendering instead of card block rendering";
        return false;
    }

    if (!IsAllowedWebMessageSource(std::wstring(AssetsUrlPrefix()) + L"index.html") ||
        IsAllowedWebMessageSource(L"https://example.com/ocr-preview/index.html")) {
        error = L"web message source allow-list contract failed";
        return false;
    }
    if (!IsAllowedNavigationUri(PreviewUrl()) ||
        IsAllowedNavigationUri(std::wstring(L"https://") + AssetsHost() + L"/other/index.html")) {
        error = L"navigation allow-list contract failed";
        return false;
    }
    if (!IsAllowedExternalUrl(L"https://example.com/docs") ||
        IsAllowedExternalUrl(L"http://127.0.0.1:8080/") ||
        IsAllowedExternalUrl(PreviewUrl())) {
        error = L"external URL allow-list contract failed";
        return false;
    }

    return true;
}
#endif
