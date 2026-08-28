#include "LlamaServerManager.h"
#include "Settings.h"
#include "TcpHelper.h"
// Stage3 3-A: net→ocr_engine cycle edge deleted. Layout cleanup via SetShutdownHook.
#include "core/WideStringUtils.h"
#include "core/NarrowStringUtils.h"
#include "core/OcrModelRegistry.h"
#include "AppMessages.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

// OWN-79: thin wrapper over pure WideStringUtils.
std::wstring LowerCopy(std::wstring value) {
    return WideToLower(std::move(value));
}

bool FindFilePattern(const std::wstring& directory, const wchar_t* pattern) {
    WIN32_FIND_DATAW data = {};
    // OWN-119: pure path join (WideStringUtils).
    HANDLE find = FindFirstFileW(WideJoinPath(directory, pattern).c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;
    FindClose(find);
    return true;
}

std::wstring DetectBackend(const std::wstring& serverExe) {
    // OWN-96: pure parent dir (WideStringUtils).
    std::wstring directory = WideExeDirFromModulePath(serverExe);
    const std::wstring lowerExe = LowerCopy(serverExe);
    if (lowerExe.find(L"cuda") != std::wstring::npos ||
        FindFilePattern(directory, L"ggml-cuda*.dll")) return L"cuda";
    if (lowerExe.find(L"vulkan") != std::wstring::npos ||
        FindFilePattern(directory, L"ggml-vulkan*.dll")) return L"vulkan";
    return L"cpu";
}

struct FileIdentity {
    uint64_t size = 0;
    uint64_t lastWrite = 0;
};

bool ReadFileIdentity(const std::wstring& path, FileIdentity& identity) {
    identity = {};
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    ULARGE_INTEGER size = {};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    ULARGE_INTEGER write = {};
    write.HighPart = data.ftLastWriteTime.dwHighDateTime;
    write.LowPart = data.ftLastWriteTime.dwLowDateTime;
    identity.size = size.QuadPart;
    identity.lastWrite = write.QuadPart;
    return true;
}

class GgufReader {
public:
    explicit GgufReader(const std::wstring& path) {
        m_file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (m_file == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(m_file, &size) || size.QuadPart < 0) {
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
            return;
        }
        m_size = static_cast<uint64_t>(size.QuadPart);
    }

    ~GgufReader() {
        if (m_file != INVALID_HANDLE_VALUE) CloseHandle(m_file);
    }

    bool IsOpen() const { return m_file != INVALID_HANDLE_VALUE; }

    bool Read(void* output, size_t bytes) {
        if (!IsOpen() || bytes > Remaining()) return false;
        auto* cursor = static_cast<unsigned char*>(output);
        size_t left = bytes;
        while (left > 0) {
            const DWORD chunk = static_cast<DWORD>((std::min)(
                left, static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD read = 0;
            if (!ReadFile(m_file, cursor, chunk, &read, nullptr) || read != chunk) {
                return false;
            }
            cursor += read;
            left -= read;
            m_position += read;
        }
        return true;
    }

    bool Skip(uint64_t bytes) {
        if (!IsOpen() || bytes > Remaining() ||
            bytes > static_cast<uint64_t>((std::numeric_limits<LONGLONG>::max)())) {
            return false;
        }
        LARGE_INTEGER distance = {};
        distance.QuadPart = static_cast<LONGLONG>(bytes);
        if (!SetFilePointerEx(m_file, distance, nullptr, FILE_CURRENT)) return false;
        m_position += bytes;
        return true;
    }

private:
    uint64_t Remaining() const {
        return m_position <= m_size ? m_size - m_position : 0;
    }

    HANDLE m_file = INVALID_HANDLE_VALUE;
    uint64_t m_size = 0;
    uint64_t m_position = 0;
};

template <typename T>
bool ReadGgufScalar(GgufReader& reader, T& value) {
    return reader.Read(&value, sizeof(value));
}

bool ReadGgufString(GgufReader& reader, std::string& value) {
    uint64_t length = 0;
    if (!ReadGgufScalar(reader, length) || length > 16 * 1024 * 1024) return false;
    value.resize(static_cast<size_t>(length));
    return length == 0 || reader.Read(value.data(), value.size());
}

bool SkipGgufString(GgufReader& reader) {
    uint64_t length = 0;
    return ReadGgufScalar(reader, length) && reader.Skip(length);
}

size_t GgufFixedTypeSize(uint32_t type) {
    switch (type) {
    case 0:  // uint8
    case 1:  // int8
    case 7:  // bool
        return 1;
    case 2:  // uint16
    case 3:  // int16
        return 2;
    case 4:  // uint32
    case 5:  // int32
    case 6:  // float32
        return 4;
    case 10: // uint64
    case 11: // int64
    case 12: // float64
        return 8;
    default:
        return 0;
    }
}

bool ReadGgufValue(
    GgufReader& reader,
    uint32_t type,
    bool captureUnsigned,
    uint64_t& captured)
{
    captured = 0;
    switch (type) {
    case 0: {
        uint8_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned) captured = value;
        return true;
    }
    case 1: {
        int8_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned && value >= 0) captured = static_cast<uint64_t>(value);
        return !captureUnsigned || value >= 0;
    }
    case 2: {
        uint16_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned) captured = value;
        return true;
    }
    case 3: {
        int16_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned && value >= 0) captured = static_cast<uint64_t>(value);
        return !captureUnsigned || value >= 0;
    }
    case 4: {
        uint32_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned) captured = value;
        return true;
    }
    case 5: {
        int32_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned && value >= 0) captured = static_cast<uint64_t>(value);
        return !captureUnsigned || value >= 0;
    }
    case 6: {
        float value = 0.0f;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned && std::isfinite(value) && value >= 0.0f &&
            value <= static_cast<float>((std::numeric_limits<uint64_t>::max)())) {
            captured = static_cast<uint64_t>(value);
            return true;
        }
        return !captureUnsigned;
    }
    case 7: {
        uint8_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned) captured = value != 0 ? 1 : 0;
        return true;
    }
    case 8:
        return SkipGgufString(reader) && !captureUnsigned;
    case 9: {
        uint32_t elementType = 0;
        uint64_t count = 0;
        if (!ReadGgufScalar(reader, elementType) ||
            !ReadGgufScalar(reader, count) || captureUnsigned) {
            return false;
        }
        const size_t fixed = GgufFixedTypeSize(elementType);
        if (fixed > 0) {
            if (count > (std::numeric_limits<uint64_t>::max)() / fixed) return false;
            return reader.Skip(count * fixed);
        }
        if (elementType != 8 || count > 100000000) return false;
        for (uint64_t index = 0; index < count; ++index) {
            if (!SkipGgufString(reader)) return false;
        }
        return true;
    }
    case 10: {
        uint64_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned) captured = value;
        return true;
    }
    case 11: {
        int64_t value = 0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned && value >= 0) captured = static_cast<uint64_t>(value);
        return !captureUnsigned || value >= 0;
    }
    case 12: {
        double value = 0.0;
        if (!ReadGgufScalar(reader, value)) return false;
        if (captureUnsigned && std::isfinite(value) && value >= 0.0 &&
            value <= static_cast<double>((std::numeric_limits<uint64_t>::max)())) {
            captured = static_cast<uint64_t>(value);
            return true;
        }
        return !captureUnsigned;
    }
    default:
        return false;
    }
}

bool ReadGgufPixelMetadata(
    const std::wstring& path,
    uint64_t& minimumPixels,
    uint64_t& maximumPixels)
{
    minimumPixels = 0;
    maximumPixels = 0;
    GgufReader reader(path);
    if (!reader.IsOpen()) return false;
    char magic[4] = {};
    uint32_t version = 0;
    uint64_t tensorCount = 0;
    uint64_t metadataCount = 0;
    if (!reader.Read(magic, sizeof(magic)) || std::memcmp(magic, "GGUF", 4) != 0 ||
        !ReadGgufScalar(reader, version) || version < 1 || version > 3 ||
        !ReadGgufScalar(reader, tensorCount) ||
        !ReadGgufScalar(reader, metadataCount) || metadataCount > 1000000) {
        return false;
    }

    bool foundMinimum = false;
    bool foundMaximum = false;
    for (uint64_t index = 0; index < metadataCount; ++index) {
        std::string key;
        uint32_t type = 0;
        if (!ReadGgufString(reader, key) || !ReadGgufScalar(reader, type)) return false;
        const bool captureMinimum = key == "clip.vision.image_min_pixels";
        const bool captureMaximum = key == "clip.vision.image_max_pixels";
        uint64_t value = 0;
        if (!ReadGgufValue(
                reader, type, captureMinimum || captureMaximum, value)) {
            return false;
        }
        if (captureMinimum) {
            minimumPixels = value;
            foundMinimum = true;
        } else if (captureMaximum) {
            maximumPixels = value;
            foundMaximum = true;
        }
        if (foundMinimum && foundMaximum) return true;
    }
    return false;
}

std::wstring Sha256File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return L"";
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD hashBytes = 0;
    DWORD copied = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptGetProperty(
        algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &copied, 0);
    if (status >= 0) status = BCryptGetProperty(
        algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes), &copied, 0);
    std::vector<UCHAR> object(objectBytes);
    std::vector<UCHAR> digest(hashBytes);
    if (status >= 0) status = BCryptCreateHash(
        algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0);
    std::vector<UCHAR> buffer(4 * 1024 * 1024);
    while (status >= 0) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            status = -1;
            break;
        }
        if (read == 0) break;
        status = BCryptHashData(hash, buffer.data(), read, 0);
    }
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), hashBytes, 0);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (status < 0 || digest.size() != 32) return L"";
    static const wchar_t hex[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve(64);
    for (UCHAR byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::wstring NormalizeHashCachePath(const std::wstring& path) {
    std::wstring normalized = path;
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed > 0 && needed < 32768) {
        std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1, L'\0');
        const DWORD written = GetFullPathNameW(
            path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (written > 0 && written < buffer.size()) {
            normalized.assign(buffer.data(), written);
        }
    }
    for (wchar_t& ch : normalized) {
        if (ch == L'/') ch = L'\\';
    }
    return LowerCopy(std::move(normalized));
}

std::wstring LlamaHashCachePath() {
    wchar_t localAppData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(
            nullptr, CSIDL_LOCAL_APPDATA, nullptr,
            SHGFP_TYPE_CURRENT, localAppData))) {
        return L"";
    }
    std::wstring directory = localAppData;
    directory += L"\\ZenCrop";
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(directory.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return L"";
        }
    } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return L"";
    }
    // OWN-119: pure path join (WideStringUtils).
    return WideJoinPath(directory, L"llama_model_hashes.ini");
}

std::wstring HashCacheSection(const std::wstring& normalizedPath) {
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t ch : normalizedPath) {
        hash ^= static_cast<uint16_t>(ch);
        hash *= 1099511628211ull;
    }
    // OWN-113: pure hash composition (WideStringUtils).
    return L"file_" + WideFormatHash016(static_cast<unsigned long long>(hash));
}

bool IsSha256Hex(const std::wstring& value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'f') ||
            (ch >= L'A' && ch <= L'F');
    });
}

bool ReadCachedSha256(
    const std::wstring& path,
    const FileIdentity& identity,
    std::wstring& sha256)
{
    sha256.clear();
    const std::wstring cachePath = LlamaHashCachePath();
    if (cachePath.empty()) return false;
    const std::wstring normalizedPath = NormalizeHashCachePath(path);
    const std::wstring section = HashCacheSection(normalizedPath);
    std::vector<wchar_t> pathBuffer(32768, L'\0');
    wchar_t sizeBuffer[32] = {};
    wchar_t writeBuffer[32] = {};
    wchar_t shaBuffer[80] = {};
    GetPrivateProfileStringW(
        section.c_str(), L"Path", L"", pathBuffer.data(),
        static_cast<DWORD>(pathBuffer.size()), cachePath.c_str());
    GetPrivateProfileStringW(
        section.c_str(), L"Size", L"", sizeBuffer,
        static_cast<DWORD>(_countof(sizeBuffer)), cachePath.c_str());
    GetPrivateProfileStringW(
        section.c_str(), L"LastWrite", L"", writeBuffer,
        static_cast<DWORD>(_countof(writeBuffer)), cachePath.c_str());
    GetPrivateProfileStringW(
        section.c_str(), L"Sha256", L"", shaBuffer,
        static_cast<DWORD>(_countof(shaBuffer)), cachePath.c_str());
    const std::wstring cachedSha = LowerCopy(shaBuffer);
    // OWN-124: pure ull labels (WideStringUtils).
    if (normalizedPath != pathBuffer.data() ||
        WideFormatUll(identity.size) != sizeBuffer ||
        WideFormatUll(identity.lastWrite) != writeBuffer ||
        !IsSha256Hex(cachedSha)) {
        return false;
    }
    sha256 = cachedSha;
    return true;
}

void WriteCachedSha256(
    const std::wstring& path,
    const FileIdentity& identity,
    const std::wstring& sha256)
{
    if (!IsSha256Hex(sha256)) return;
    const std::wstring cachePath = LlamaHashCachePath();
    if (cachePath.empty()) return;
    const std::wstring normalizedPath = NormalizeHashCachePath(path);
    const std::wstring section = HashCacheSection(normalizedPath);
    WritePrivateProfileStringW(
        section.c_str(), L"Path", normalizedPath.c_str(), cachePath.c_str());
    // OWN-124: pure ull labels (WideStringUtils).
    WritePrivateProfileStringW(
        section.c_str(), L"Size", WideFormatUll(identity.size).c_str(),
        cachePath.c_str());
    WritePrivateProfileStringW(
        section.c_str(), L"LastWrite", WideFormatUll(identity.lastWrite).c_str(),
        cachePath.c_str());
    WritePrivateProfileStringW(
        section.c_str(), L"Sha256", LowerCopy(sha256).c_str(), cachePath.c_str());
}

std::wstring Sha256FileCached(
    const std::wstring& path,
    const FileIdentity& identity,
    bool& cacheHit)
{
    std::wstring sha256;
    if (ReadCachedSha256(path, identity, sha256)) {
        cacheHit = true;
        return sha256;
    }
    cacheHit = false;
    sha256 = Sha256File(path);
    if (!sha256.empty()) WriteCachedSha256(path, identity, sha256);
    return sha256;
}

std::wstring QueryServerVersion(const std::wstring& serverExe) {
    SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return L"";
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup = {sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process = {};
    std::wstring command = L"\"" + serverExe + L"\" --version";
    BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        return L"";
    }
    if (WaitForSingleObject(process.hProcess, 10000) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
    }
    std::string output;
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        output.append(buffer, read);
    }
    CloseHandle(readPipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    int length = MultiByteToWideChar(
        CP_UTF8, 0, output.data(), static_cast<int>(output.size()), nullptr, 0);
    if (length <= 0) return L"";
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, output.data(), static_cast<int>(output.size()),
        wide.data(), length);
    size_t version = wide.find(L"version:");
    if (version == std::wstring::npos) return L"";
    size_t end = wide.find_first_of(L"\r\n", version);
    std::wstring result = wide.substr(version, end - version);
    size_t built = wide.find(L"built with", end);
    if (built != std::wstring::npos) {
        size_t builtEnd = wide.find_first_of(L"\r\n", built);
        result += L"; " + wide.substr(built, builtEnd - built);
    }
    return result;
}

std::wstring FindPaddle16ChatTemplate(const std::wstring& modelPath) {
    if (LowerCopy(modelPath).find(L"paddleocr-vl-1.6") == std::wstring::npos) return L"";
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    // OWN-96: pure parent + join (WideStringUtils).
    const std::wstring deployed = WideJoinPath(
        WideJoinPath(WideExeDirFromModulePath(executable), L"ocr_templates"),
        L"paddleocr-vl-1.6.jinja");
    if (PathFileExistsW(deployed.c_str())) return deployed;
    const std::wstring development =
        L"src\\ocr\\chat_templates\\paddleocr-vl-1.6.jinja";
    if (PathFileExistsW(development.c_str())) return development;
    return L"";
}

} // namespace

struct LlamaIdleShutdownParams {
    LlamaServerManager* manager;
    unsigned int generation;
    int timeoutMs;
};

LlamaServerManager& LlamaServerManager::Instance() {
    static LlamaServerManager instance;
    return instance;
}

LlamaServerManager::LlamaServerManager() {
    InitializeCriticalSection(&m_cs);
    m_csInitialized = true;
}

LlamaServerManager::~LlamaServerManager() {
    StopServer();
    if (m_hJob) {
        // 关闭 job handle 触发 KILL_ON_JOB_CLOSE，杀掉所有未及时退出的子进程。
        // 正常路径下 StopServer 已 TerminateProcess 子进程，这里只是兜底。
        CloseHandle(m_hJob);
        m_hJob = nullptr;
    }
    if (m_csInitialized) {
        DeleteCriticalSection(&m_cs);
        m_csInitialized = false;
    }
}

void LlamaServerManager::EnsureJobObject() {
    if (m_hJob) return;

    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (!hJob) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLlamaCreateJobFailed(GetLastError()).c_str());
        return;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
    jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                 &jobInfo, sizeof(jobInfo))) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLlamaSetJobInfoFailed(GetLastError()).c_str());
        CloseHandle(hJob);
        return;
    }

    m_hJob = hJob;
}

bool LlamaServerManager::FindServerExe(std::wstring& exePath) {
    OcrSettings settings = LoadOcrSettings();
    return OcrModelRegistryFindPaddleVlServer(
        OcrModelRegistryBuildPlan(settings, OcrModelRegistryProcessDir()), exePath);
}

bool LlamaServerManager::FindModelFiles(std::wstring& modelPath, std::wstring& mmprojPath) {
    OcrSettings settings = LoadOcrSettings();
    const bool found = OcrModelRegistryFindPaddleVlModelPair(
        OcrModelRegistryBuildPlan(settings, OcrModelRegistryProcessDir()), modelPath, mmprojPath);
    if (found) ExtractModelName(modelPath);
    return found;
}

void LlamaServerManager::ExtractModelName(const std::wstring& modelPath) {
    std::wstring filename = modelPath;
    size_t slash = filename.find_last_of(L"\\/");
    if (slash != std::wstring::npos) filename = filename.substr(slash + 1);
    size_t dot = filename.rfind(L".gguf");
    if (dot != std::wstring::npos) filename = filename.substr(0, dot);
    const wchar_t* ggufSuffix = L"-GGUF";
    size_t suffixLen = 5;
    if (filename.length() > suffixLen &&
        filename.compare(filename.length() - suffixLen, suffixLen, ggufSuffix) == 0) {
        filename = filename.substr(0, filename.length() - suffixLen);
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        m_modelName.assign(static_cast<size_t>(len), '\0');
        const int written = WideCharToMultiByte(
            CP_UTF8, 0, filename.c_str(), -1,
            m_modelName.data(), len, nullptr, nullptr);
        if (written > 0) m_modelName.resize(static_cast<size_t>(written - 1));
        else m_modelName.clear();
    }
}

int LlamaServerManager::FindFreePort() {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return 0;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return 0;
    }
    int addrLen = sizeof(addr);
    if (getsockname(sock, (sockaddr*)&addr, &addrLen) == SOCKET_ERROR) {
        closesocket(sock);
        return 0;
    }

    int port = ntohs(addr.sin_port);
    closesocket(sock);

    return port;
}

bool LlamaServerManager::StartServer() {
    std::wstring serverExe;
    if (!FindServerExe(serverExe)) {
        OutputDebugStringA("[LlamaServer] llama-server.exe not found\n");
        return false;
    }

    std::wstring modelPath, mmprojPath;
    if (!FindModelFiles(modelPath, mmprojPath)) {
        OutputDebugStringA("[LlamaServer] GGUF model files not found\n");
        return false;
    }

    OcrSettings settings = LoadOcrSettings();
    m_port = (settings.paddleLocalPort > 0) ? settings.paddleLocalPort : FindFreePort();

    m_launchDiagnostics = {};
    m_launchDiagnostics.serverExePath = serverExe;
    m_launchDiagnostics.modelPath = modelPath;
    m_launchDiagnostics.mmprojPath = mmprojPath;
    m_launchDiagnostics.backend = DetectBackend(serverExe);
    m_launchDiagnostics.serverVersion = QueryServerVersion(serverExe);
    FileIdentity modelIdentity;
    FileIdentity mmprojIdentity;
    const bool modelIdentityValid = ReadFileIdentity(modelPath, modelIdentity);
    const bool mmprojIdentityValid = ReadFileIdentity(mmprojPath, mmprojIdentity);
    m_launchDiagnostics.modelBytes = modelIdentity.size;
    m_launchDiagnostics.mmprojBytes = mmprojIdentity.size;
    ReadGgufPixelMetadata(
        mmprojPath,
        m_launchDiagnostics.imageMinPixels,
        m_launchDiagnostics.imageMaxPixels);
    m_launchDiagnostics.chatTemplatePath = FindPaddle16ChatTemplate(modelPath);

    // OWN-112: pure int label for port (WideStringUtils).
    std::wstring portStr = WideFormatIntLabel(m_port);

    std::wstring cmdLine = L"\"" + serverExe + L"\""
        L" -m \"" + modelPath + L"\""
        L" --mmproj \"" + mmprojPath + L"\""
        L" --port " + portStr +
        L" --host 127.0.0.1"
        L" --temp 0"
        L" -ngl 99"
        L" --jinja";
    if (!m_launchDiagnostics.chatTemplatePath.empty()) {
        cmdLine += L" --chat-template-file \"" +
            m_launchDiagnostics.chatTemplatePath + L"\"";
    }
    cmdLine += L" --log-disable";

    // OWN-124: pure ull/int labels (WideStringUtils).
    OutputDebugStringW((L"[LlamaServer] version=" +
        m_launchDiagnostics.serverVersion + L" backend=" +
        m_launchDiagnostics.backend + L" modelBytes=" +
        WideFormatUll(m_launchDiagnostics.modelBytes) + L" mmprojBytes=" +
        WideFormatUll(m_launchDiagnostics.mmprojBytes) + L" imagePixels=" +
        WideFormatIntLabel(m_launchDiagnostics.imageMinPixels) + L"/" +
        WideFormatIntLabel(m_launchDiagnostics.imageMaxPixels) + L" chatTemplate=" +
        m_launchDiagnostics.chatTemplatePath + L"\n").c_str());

    OutputDebugStringW((L"[LlamaServer] Starting: " + cmdLine + L"\n").c_str());

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&m_procInfo, sizeof(m_procInfo));

    std::wstring mutableCmd = cmdLine;
    BOOL ok = CreateProcessW(
        nullptr,
        mutableCmd.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &m_procInfo);

    if (!ok) {
        DWORD err = GetLastError();
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLlamaCreateProcessFailed(err).c_str());
        return false;
    }

    // 绑定子进程到 Job Object：ZenCrop 不论正常退出还是被杀/崩溃，
    // OS 关闭 job handle 时都会连带终止 llama-server.exe，避免孤儿进程。
    EnsureJobObject();
    if (m_hJob && !AssignProcessToJobObject(m_hJob, m_procInfo.hProcess)) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLlamaAssignJobFailed(GetLastError()).c_str());
        // 非致命：服务仍在运行，只是失去了死亡兜底。
    }

    // Start hashing only after llama-server is running so a first-time cache
    // miss overlaps model loading instead of serially delaying process launch.
    // Unchanged files hit a persistent size/mtime-keyed cache on later app and
    // idle-restart cold starts.
    const ULONGLONG shaStart = GetTickCount64();
    if (modelIdentityValid) {
        m_launchDiagnostics.modelSha256 = Sha256FileCached(
            modelPath, modelIdentity, m_launchDiagnostics.modelSha256CacheHit);
    } else {
        m_launchDiagnostics.modelSha256 = Sha256File(modelPath);
    }
    if (mmprojIdentityValid) {
        m_launchDiagnostics.mmprojSha256 = Sha256FileCached(
            mmprojPath, mmprojIdentity, m_launchDiagnostics.mmprojSha256CacheHit);
    } else {
        m_launchDiagnostics.mmprojSha256 = Sha256File(mmprojPath);
    }
    m_launchDiagnostics.sha256Ms = static_cast<DWORD>(
        GetTickCount64() - shaStart);
    // OWN-124: pure ull labels (WideStringUtils).
    OutputDebugStringW((L"[LlamaServer] modelSha256=" +
        m_launchDiagnostics.modelSha256 + L" cache=" +
        (m_launchDiagnostics.modelSha256CacheHit ? L"hit" : L"miss") +
        L" mmprojSha256=" + m_launchDiagnostics.mmprojSha256 + L" cache=" +
        (m_launchDiagnostics.mmprojSha256CacheHit ? L"hit" : L"miss") +
        L" sha256Ms=" + WideFormatUll(m_launchDiagnostics.sha256Ms) +
        L"\n").c_str());

    OutputDebugStringA("[LlamaServer] Server process started\n");
    return true;
}

bool LlamaServerManager::WaitForServerReady(int timeoutMs) {
    DWORD start = GetTickCount();
    while (GetTickCount() - start < (DWORD)timeoutMs) {
        HINTERNET hSession = WinHttpOpen(L"ZenCrop/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            Sleep(500);
            continue;
        }

        HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)m_port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            Sleep(500);
            continue;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/health",
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            Sleep(500);
            continue;
        }

        BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (sent) {
            WinHttpReceiveResponse(hRequest, nullptr);
            DWORD statusCode = 0;
            DWORD size = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);

            if (statusCode == 200) {
                // OWN-116: pure narrow debug (NarrowStringUtils).
                OutputDebugStringA(NarrowFormatLlamaServerReady(m_port).c_str());
                return true;
            }
        } else {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
        }

        Sleep(500);
    }

    OutputDebugStringA("[LlamaServer] Server failed to start within timeout\n");
    return false;
}

void LlamaServerManager::StopServerLocked() {
    m_idleGeneration++;
    m_activeRequests = 0;
    m_lastActivityTick = 0;

    if (m_procInfo.hProcess) {
        OutputDebugStringA("[LlamaServer] Stopping server\n");
        TerminateProcess(m_procInfo.hProcess, 0);
        WaitForSingleObject(m_procInfo.hProcess, 3000);
        CloseHandle(m_procInfo.hProcess);
        CloseHandle(m_procInfo.hThread);
        ZeroMemory(&m_procInfo, sizeof(m_procInfo));
    }
    m_serverStarted = false;
    m_serverFailed = false;
    m_port = 0;
}

void LlamaServerManager::StopServer() {
    EnterCriticalSection(&m_cs);
    StopServerLocked();
    LeaveCriticalSection(&m_cs);
}

int LlamaServerManager::GetIdleTimeoutMs() const {
    OcrSettings settings = LoadOcrSettings();
    HotkeySettings hotkeys = LoadHotkeySettings();
    int minutes = ResolveOcrLlamaIdleTimeoutMin(settings, hotkeys);
    if (minutes <= 0) return 0;
    return minutes * 60 * 1000;
}

void LlamaServerManager::ScheduleIdleShutdownLocked(int timeoutMs) {
    if (timeoutMs <= 0 || !m_serverStarted || m_activeRequests > 0) {
        return;
    }

    if (m_lastActivityTick == 0) {
        m_lastActivityTick = GetTickCount64();
    }

    auto* params = new LlamaIdleShutdownParams{ this, m_idleGeneration, timeoutMs };
    HANDLE hThread = CreateThread(nullptr, 0, IdleShutdownThread, params, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete params;
    }
}

DWORD WINAPI LlamaServerManager::IdleShutdownThread(LPVOID param) {
    auto* p = (LlamaIdleShutdownParams*)param;
    Sleep((DWORD)p->timeoutMs);

    if (p->manager->StopServerIfIdle(p->generation, p->timeoutMs)) {
        OutputDebugStringA("[LlamaServer] Idle timeout reached; shutting down local OCR resources\n");
    }

    delete p;
    return 0;
}

bool LlamaServerManager::StopServerIfIdle(unsigned int generation, int timeoutMs) {
    EnterCriticalSection(&m_cs);
    ULONGLONG now = GetTickCount64();
    ULONGLONG idleMs = now - m_lastActivityTick;
    bool shouldStop =
        generation == m_idleGeneration &&
        m_serverStarted &&
        m_activeRequests == 0 &&
        m_lastActivityTick != 0 &&
        idleMs >= (ULONGLONG)timeoutMs;

    if (shouldStop) {
        // Stage3 3-A: run registered shutdown hook (engine layout cleanup) without engine include.
        RunShutdownHook();
        StopServerLocked();
    }

    LeaveCriticalSection(&m_cs);
    return shouldStop;
}

void LlamaServerManager::BeginRequest() {
    EnterCriticalSection(&m_cs);
    m_activeRequests++;
    m_lastActivityTick = GetTickCount64();
    m_idleGeneration++;
    LeaveCriticalSection(&m_cs);
}

void LlamaServerManager::EndRequest() {
    int timeoutMs = GetIdleTimeoutMs();

    EnterCriticalSection(&m_cs);
    if (m_activeRequests > 0) {
        m_activeRequests--;
    }
    m_lastActivityTick = GetTickCount64();
    m_idleGeneration++;
    ScheduleIdleShutdownLocked(timeoutMs);
    LeaveCriticalSection(&m_cs);
}

void LlamaServerManager::RefreshIdleShutdown() {
    int timeoutMs = GetIdleTimeoutMs();

    EnterCriticalSection(&m_cs);
    m_lastActivityTick = GetTickCount64();
    m_idleGeneration++;
    ScheduleIdleShutdownLocked(timeoutMs);
    LeaveCriticalSection(&m_cs);
}

bool LlamaServerManager::EnsureServerStarted() {
    EnterCriticalSection(&m_cs);

    if (m_serverStarted) {
        OcrSettings settings = LoadOcrSettings();
        std::wstring currentDir = settings.paddleLocalModelDir;
        // OWN-124: pure int labels (WideStringUtils).
        std::wstring currentPortStr = WideFormatIntLabel(settings.paddleLocalPort);
        if (currentDir != m_lastModelDir || currentPortStr != m_lastPortStr) {
            OutputDebugStringA("[LlamaServer] Config changed, restarting server\n");
            StopServerLocked();
        }
    }

    if (m_serverStarted) {
        LeaveCriticalSection(&m_cs);
        return true;
    }

    if (m_starting) {
        LeaveCriticalSection(&m_cs);
        while (m_starting) Sleep(100);
        EnterCriticalSection(&m_cs);
        bool ok = m_serverStarted;
        LeaveCriticalSection(&m_cs);
        return ok;
    }

    if (m_serverFailed) {
        LeaveCriticalSection(&m_cs);
        return false;
    }

    OcrSettings settings = LoadOcrSettings();
    int configuredPort = settings.paddleLocalPort;
    if (configuredPort > 0 && TcpPortIsOpen(L"127.0.0.1", configuredPort, 1000)) {
        OutputDebugStringA("[LlamaServer] Port already in use, reusing existing server\n");
        m_port = configuredPort;
        m_serverStarted = true;
        m_lastModelDir = settings.paddleLocalModelDir;
        // OWN-124: pure int labels (WideStringUtils).
        m_lastPortStr = WideFormatIntLabel(settings.paddleLocalPort);
        LeaveCriticalSection(&m_cs);
        return true;
    }

    m_starting = true;
    LeaveCriticalSection(&m_cs);

    bool ok = false;
    if (StartServer() && WaitForServerReady(60000)) {
        ok = true;
    } else {
        StopServer();
    }

    EnterCriticalSection(&m_cs);
    if (ok) {
        m_serverStarted = true;
        settings = LoadOcrSettings();
        m_lastModelDir = settings.paddleLocalModelDir;
        // OWN-124: pure int labels (WideStringUtils).
        m_lastPortStr = WideFormatIntLabel(settings.paddleLocalPort);
    } else {
        m_serverFailed = true;
    }
    m_starting = false;
    LeaveCriticalSection(&m_cs);
    return ok;
}

bool LlamaServerManager::IsServerRunning() {
    EnterCriticalSection(&m_cs);
    int port = m_port;
    bool started = m_serverStarted;
    LeaveCriticalSection(&m_cs);
    if (started && port > 0) {
        return TcpPortIsOpen(L"127.0.0.1", port, 1000);
    }
    return false;
}

void LlamaServerManager::GlobalShutdown() {
    EnterCriticalSection(&m_cs);
    // Stage3 3-A: run registered shutdown hook (engine layout cleanup) without engine include.
    RunShutdownHook();
    StopServerLocked();
    LeaveCriticalSection(&m_cs);
}

void LlamaServerManager::SetShutdownHook(ShutdownHook hook) {
    EnterCriticalSection(&m_cs);
    m_shutdownHook = hook;
    LeaveCriticalSection(&m_cs);
}

void LlamaServerManager::RunShutdownHook() {
    // Caller holds m_cs (StopServerIfIdle / GlobalShutdown).
    if (m_shutdownHook) {
        m_shutdownHook();
    }
}

int LlamaServerManager::GetPort() const {
    return m_port;
}
