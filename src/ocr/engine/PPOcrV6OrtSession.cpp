#include "PPOcrV6OrtSession.h"

#include "BitmapUtils.h"
#include "PPOcrV6CtcDecode.h"
#include "core/NarrowStringUtils.h"
#include "core/WideStringUtils.h"

#include <windows.h>
#include <gdiplus.h>

#define ORT_NO_EXCEPTIONS
#include "onnxruntime_c_api.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <utility>

#ifdef ZENCROP_WITH_OPENCV_DBPOST
#include <opencv2/core.hpp>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>
#endif

#pragma comment(lib, "gdiplus.lib")

namespace {

const OrtApi* g_ort = nullptr;
std::once_flag g_ortInitOnce;

bool InitOrtApi() {
    std::call_once(g_ortInitOnce, []() {
        g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!g_ort) OutputDebugStringA("[PPOCRv6] Failed to get ONNX Runtime API\n");
    });
    return g_ort != nullptr;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), len);
    return out;
}

} // namespace

struct PPOcrV6OrtSession::State {
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtAllocator* allocator = nullptr;
    std::string inputName;
    std::string outputName;

    void Reset() {
        if (session && g_ort) g_ort->ReleaseSession(session);
        if (env && g_ort) g_ort->ReleaseEnv(env);
        session = nullptr;
        env = nullptr;
        allocator = nullptr;
        inputName.clear();
        outputName.clear();
    }
};

PPOcrV6OrtSession::PPOcrV6OrtSession()
    : m_state(std::make_unique<State>())
{
}

PPOcrV6OrtSession::~PPOcrV6OrtSession() {
    m_state->Reset();
}

bool PPOcrV6OrtSession::Initialize(
    const std::wstring& modelPath,
    const char* tag,
    int cpuThreads,
    std::wstring& error)
{
    m_state->Reset();
    if (!InitOrtApi()) {
        error = L"ONNX Runtime API is not available.";
        return false;
    }

    OrtStatus* status = g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, tag, &m_state->env);
    if (status) {
        error = L"Failed to create ONNX Runtime environment.";
        g_ort->ReleaseStatus(status);
        return false;
    }

    OrtSessionOptions* opts = nullptr;
    g_ort->CreateSessionOptions(&opts);
    g_ort->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL);
    g_ort->SetInterOpNumThreads(opts, 1);
    g_ort->SetIntraOpNumThreads(opts, cpuThreads);

    status = g_ort->CreateSession(m_state->env, modelPath.c_str(), opts, &m_state->session);
    g_ort->ReleaseSessionOptions(opts);
    if (status) {
        const char* msg = g_ort->GetErrorMessage(status);
        error = L"Failed to load ONNX model: " + Utf8ToWide(msg ? msg : "");
        g_ort->ReleaseStatus(status);
        return false;
    }

    g_ort->GetAllocatorWithDefaultOptions(&m_state->allocator);
    char* name = nullptr;
    if (g_ort->SessionGetInputName(m_state->session, 0, m_state->allocator, &name) == nullptr && name) {
        m_state->inputName = name;
        g_ort->AllocatorFree(m_state->allocator, name);
    }
    name = nullptr;
    if (g_ort->SessionGetOutputName(m_state->session, 0, m_state->allocator, &name) == nullptr && name) {
        m_state->outputName = name;
        g_ort->AllocatorFree(m_state->allocator, name);
    }
    if (m_state->inputName.empty() || m_state->outputName.empty()) {
        error = L"Failed to inspect ONNX model I/O names.";
        return false;
    }

    OutputDebugStringA(NarrowFormatPpocrv6LoadedModel(
        tag, m_state->inputName.c_str(), m_state->outputName.c_str()).c_str());
    return true;
}

bool PPOcrV6OrtSession::Run(
    const std::vector<float>& input,
    const std::vector<int64_t>& inputShape,
    PPOcrV6TensorOutput& output,
    std::wstring& error) const
{
    output = {};
    if (!m_state->session || !g_ort) {
        error = L"ONNX session is not initialized.";
        return false;
    }

    OrtMemoryInfo* memInfo = nullptr;
    OrtStatus* status = g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memInfo);
    if (status) {
        error = L"Failed to create ONNX memory info.";
        g_ort->ReleaseStatus(status);
        return false;
    }

    OrtValue* inputTensor = nullptr;
    status = g_ort->CreateTensorWithDataAsOrtValue(
        memInfo, const_cast<float*>(input.data()), input.size() * sizeof(float),
        inputShape.data(), inputShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensor);
    g_ort->ReleaseMemoryInfo(memInfo);
    if (status) {
        error = L"Failed to create ONNX input tensor.";
        g_ort->ReleaseStatus(status);
        return false;
    }

    const char* inputNames[] = { m_state->inputName.c_str() };
    const char* outputNames[] = { m_state->outputName.c_str() };
    OrtValue* outputTensor = nullptr;
    status = g_ort->Run(m_state->session, nullptr, inputNames, &inputTensor, 1,
        outputNames, 1, &outputTensor);
    g_ort->ReleaseValue(inputTensor);
    if (status) {
        const char* msg = g_ort->GetErrorMessage(status);
        error = L"ONNX inference failed: " + Utf8ToWide(msg ? msg : "");
        g_ort->ReleaseStatus(status);
        return false;
    }

    OrtTensorTypeAndShapeInfo* shapeInfo = nullptr;
    g_ort->GetTensorTypeAndShape(outputTensor, &shapeInfo);
    size_t dimCount = 0;
    g_ort->GetDimensionsCount(shapeInfo, &dimCount);
    output.shape.resize(dimCount);
    g_ort->GetDimensions(shapeInfo, output.shape.data(), dimCount);
    size_t elemCount = 0;
    g_ort->GetTensorShapeElementCount(shapeInfo, &elemCount);
    g_ort->ReleaseTensorTypeAndShapeInfo(shapeInfo);

    float* data = nullptr;
    g_ort->GetTensorMutableData(outputTensor, (void**)&data);
    if (data && elemCount > 0) output.values.assign(data, data + elemCount);
    g_ort->ReleaseValue(outputTensor);
    return !output.values.empty();
}

struct PPOcrV6Runtime {
    std::wstring detModelPath, recModelPath, dictPath, provider;
    int cpuThreads = 0;
    PPOcrV6OrtSession detSession, recSession;
    std::vector<std::wstring> dict;

    bool Matches(const PPOcrV6Config& config) const {
        return detModelPath == config.detModelPath && recModelPath == config.recModelPath &&
            dictPath == config.dictPath && provider == config.provider &&
            cpuThreads == config.cpuThreads;
    }
};

namespace {

std::mutex g_runtimeCacheMutex;
std::shared_ptr<PPOcrV6Runtime> g_runtimeCache;
constexpr size_t kIdeographicSpaceIndex = 1748;

bool IsPPOcrV6PathType(const std::wstring& path, DWORD requiredType) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == requiredType;
}

std::vector<std::wstring> LoadBaseDict(const std::wstring& dictPath) {
    std::ifstream file(dictPath, std::ios::binary);
    std::vector<std::wstring> dict;
    if (!file.is_open()) return dict;
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            first = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
                line.erase(0, 3);
            }
        }
        dict.push_back(Utf8ToWide(line));
    }
    return dict;
}

bool ValidateBaseDictContract(const std::vector<std::wstring>& baseDict,
    const std::wstring& dictPath, std::wstring& error)
{
    if (baseDict.empty()) {
        error = L"Failed to load PP-OCRv6 recognition dictionary:\n" + dictPath;
        return false;
    }
    if (static_cast<int>(baseDict.size()) != PPOcrV6Ctc::kExpectedBaseDictSize) {
        error = L"PP-OCRv6 recognition dictionary size mismatch.\n"
            L"got base_dict_size=" + WideFormatIntLabel(static_cast<int>(baseDict.size()))
            + L", expected " + WideFormatIntLabel(PPOcrV6Ctc::kExpectedBaseDictSize)
            + L".\nOpen Settings -> OCR -> Manage Models, then run Verify / Repair.";
        return false;
    }
    if (baseDict.size() <= kIdeographicSpaceIndex) {
        error = L"PP-OCRv6 recognition dictionary too short for U+3000 contract check.";
        return false;
    }
    const std::wstring& ideographic = baseDict[kIdeographicSpaceIndex];
    if (ideographic != L"　") {
        std::wstring gotDesc = L"(empty — typical of pre-fix strip() export)";
        if (!ideographic.empty()) gotDesc = WideFormatUPlusCodepoint(static_cast<unsigned>(ideographic[0]));
        error = L"PP-OCRv6 recognition dictionary is outdated or corrupted.\n"
            L"Index 1748 must be U+3000 IDEOGRAPHIC SPACE, got " + gotDesc
            + L".\nOpen Settings -> OCR -> Manage Models, then run Verify / Repair.";
        return false;
    }
    return true;
}

void WarnIfManifestContractMismatch(const std::wstring& dictPath) {
    namespace fs = std::filesystem;
    try {
        const fs::path manifest = fs::path(dictPath).parent_path() / L"manifest.json";
        if (!fs::exists(manifest)) {
            OutputDebugStringA("[PPOCRv6] manifest.json missing; base dict contract still enforced\n");
            return;
        }
        std::ifstream file(manifest, std::ios::binary);
        if (!file.is_open()) return;
        const std::string content((std::istreambuf_iterator<char>(file)), {});
        const auto hasField = [&](const char* key, int expected) {
            const std::string needle = std::string("\"") + key + "\"";
            size_t pos = content.find(needle);
            if (pos == std::string::npos || (pos = content.find(':', pos + needle.size())) == std::string::npos) return false;
            for (++pos; pos < content.size() && (content[pos] == ' ' || content[pos] == '\t'); ++pos) {}
            int value = 0; bool any = false;
            while (pos < content.size() && content[pos] >= '0' && content[pos] <= '9') {
                any = true; value = value * 10 + (content[pos++] - '0');
            }
            return any && value == expected;
        };
        if (content.find("\"manifest_version\"") != std::string::npos && !hasField("manifest_version", 2))
            OutputDebugStringA("[PPOCRv6] warning: manifest_version is not 2; re-export recommended\n");
        if (content.find("\"base_dict_size\"") != std::string::npos && !hasField("base_dict_size", PPOcrV6Ctc::kExpectedBaseDictSize))
            OutputDebugStringA("[PPOCRv6] warning: manifest base_dict_size mismatch; re-export recommended\n");
        if (content.find("\"expected_output_classes\"") != std::string::npos && !hasField("expected_output_classes", PPOcrV6Ctc::kExpectedOutputClasses))
            OutputDebugStringA("[PPOCRv6] warning: manifest expected_output_classes mismatch; re-export recommended\n");
        if (content.find("\"append_space\"") != std::string::npos &&
            content.find("\"append_space\": true") == std::string::npos &&
            content.find("\"append_space\":true") == std::string::npos)
            OutputDebugStringA("[PPOCRv6] warning: manifest append_space is not true; re-export recommended\n");
    } catch (...) {
        // Manifest is advisory only.
    }
}

std::vector<std::wstring> LoadEffectiveDict(const std::wstring& dictPath, std::wstring& error) {
    error.clear();
    std::vector<std::wstring> dict = LoadBaseDict(dictPath);
    if (!ValidateBaseDictContract(dict, dictPath, error)) return {};
    WarnIfManifestContractMismatch(dictPath);
    PPOcrV6Ctc::AppendOfficialSpaceChar(dict);
    return dict;
}

} // namespace

std::wstring ValidatePPOcrV6Config(const PPOcrV6Config& config) {
    if (config.modelDir.empty()) {
        return L"PP-OCRv6 model directory is not configured.";
    }
    if (!IsPPOcrV6PathType(config.modelDir, FILE_ATTRIBUTE_DIRECTORY)) {
        return L"PP-OCRv6 model directory does not exist.";
    }
    if (!IsPPOcrV6PathType(config.detModelPath, 0)) {
        return L"PP-OCRv6 detection model not found:\n" + config.detModelPath;
    }
    if (!IsPPOcrV6PathType(config.recModelPath, 0)) {
        return L"PP-OCRv6 recognition model not found:\n" + config.recModelPath;
    }
    if (!IsPPOcrV6PathType(config.dictPath, 0)) {
        return L"PP-OCRv6 recognition dictionary not found:\n" + config.dictPath;
    }
    return L"";
}

std::shared_ptr<PPOcrV6Runtime> AcquirePPOcrV6Runtime(
    const PPOcrV6Config& config,
    std::wstring& error)
{
    error = ValidatePPOcrV6Config(config);
    if (!error.empty()) return nullptr;
    {
        std::lock_guard<std::mutex> lock(g_runtimeCacheMutex);
        if (g_runtimeCache && g_runtimeCache->Matches(config)) {
            OutputDebugStringA("[PPOCRv6] Reusing cached ONNX runtime\n");
            return g_runtimeCache;
        }
    }
    auto runtime = std::make_shared<PPOcrV6Runtime>();
    runtime->detModelPath = config.detModelPath;
    runtime->recModelPath = config.recModelPath;
    runtime->dictPath = config.dictPath;
    runtime->provider = config.provider;
    runtime->cpuThreads = config.cpuThreads;
    runtime->dict = LoadEffectiveDict(runtime->dictPath, error);
    if (runtime->dict.empty()) {
        if (error.empty()) error = L"Failed to load PP-OCRv6 recognition dictionary.";
        return nullptr;
    }
    if (runtime->dict.size() != static_cast<size_t>(PPOcrV6Ctc::kExpectedEffectiveDictSize)) {
        error = L"PP-OCRv6 effective dictionary size mismatch after append_space: got "
            + WideFormatIntLabel(static_cast<int>(runtime->dict.size())) + L", expected "
            + WideFormatIntLabel(PPOcrV6Ctc::kExpectedEffectiveDictSize) + L".";
        return nullptr;
    }
    OutputDebugStringA("[PPOCRv6] Loaded effective CTC dict with official ASCII space class\n");
    if (!runtime->detSession.Initialize(runtime->detModelPath, "PPOCRv6Det", runtime->cpuThreads, error) ||
        !runtime->recSession.Initialize(runtime->recModelPath, "PPOCRv6Rec", runtime->cpuThreads, error)) {
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_runtimeCacheMutex);
        g_runtimeCache = runtime;
    }
    OutputDebugStringA("[PPOCRv6] Cached ONNX runtime\n");
    return runtime;
}

bool RunPPOcrV6Detector(PPOcrV6Runtime& runtime, const std::vector<float>& input,
    const std::vector<int64_t>& shape, PPOcrV6TensorOutput& output, std::wstring& error)
{
    return runtime.detSession.Run(input, shape, output, error);
}

bool RunPPOcrV6Recognizer(PPOcrV6Runtime& runtime, const std::vector<float>& input,
    const std::vector<int64_t>& shape, PPOcrV6TensorOutput& output, std::wstring& error)
{
    return runtime.recSession.Run(input, shape, output, error);
}

bool RunPPOcrV6RecognitionBatch(PPOcrV6Runtime& runtime,
    const std::vector<PPOcrV6RecognitionInput>& inputs,
    std::vector<PPOcrV6RecognitionResult>& results, std::wstring& error)
{
    results.clear();
    if (inputs.empty()) return true;

    constexpr int kRecognitionHeight = 48;
    const int batchSize = static_cast<int>(inputs.size());
    int maxWidth = 0;
    for (const auto& input : inputs) {
        if (input.chw.empty() || input.width <= 0) return false;
        maxWidth = (std::max)(maxWidth, input.width);
    }
    if (maxWidth <= 0) return false;

    std::vector<float> batch(static_cast<size_t>(batchSize) * 3 * kRecognitionHeight * maxWidth, 0.0f);
    for (int batchIndex = 0; batchIndex < batchSize; ++batchIndex) {
        const auto& input = inputs[batchIndex];
        for (int channel = 0; channel < 3; ++channel) {
            for (int y = 0; y < kRecognitionHeight; ++y) {
                const size_t source = (static_cast<size_t>(channel) * kRecognitionHeight + y) * input.width;
                const size_t destination =
                    ((static_cast<size_t>(batchIndex) * 3 + channel) * kRecognitionHeight + y) * maxWidth;
                std::copy_n(input.chw.data() + source, input.width, batch.data() + destination);
            }
        }
    }

    PPOcrV6TensorOutput output;
    const std::vector<int64_t> shape = { batchSize, 3, kRecognitionHeight, maxWidth };
    if (!RunPPOcrV6Recognizer(runtime, batch, shape, output, error)) return false;

    error.clear();
    results.reserve(inputs.size());
    for (int batchIndex = 0; batchIndex < batchSize; ++batchIndex) {
        auto decoded = PPOcrV6Ctc::DecodeFromShape(
            output.values.data(), output.values.size(), output.shape, batchIndex, runtime.dict);
        if (!decoded.ok) {
            error = decoded.error.empty() ? L"PP-OCRv6 CTC decode failed." : decoded.error;
            OutputDebugStringW((L"[PPOCRv6] " + error + L"\n").c_str());
            results.clear();
            return false;
        }
        results.push_back({ std::move(decoded.text), decoded.score });
    }
    return true;
}

const std::vector<std::wstring>& GetPPOcrV6RuntimeDictionary(const PPOcrV6Runtime& runtime) {
    return runtime.dict;
}

bool ResizePPOcrV6BitmapToPixels(HBITMAP bitmap, int width, int height,
    std::vector<unsigned char>& pixels)
{
    if (!bitmap || width <= 0 || height <= 0) return false;
    Gdiplus::Bitmap source(bitmap, nullptr);
    if (source.GetLastStatus() != Gdiplus::Ok) return false;
    Gdiplus::Bitmap scaled(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&scaled);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    graphics.DrawImage(&source, 0, 0, width, height);

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data = {};
    if (scaled.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return false;
    pixels.resize((size_t)width * height * 4);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = static_cast<const unsigned char*>(data.Scan0) + y * data.Stride;
        std::memcpy(pixels.data() + (size_t)y * width * 4, row, (size_t)width * 4);
    }
    scaled.UnlockBits(&data);
    return true;
}

PPOcrV6DetectionInput BuildPPOcrV6DetectionInput(
    HBITMAP bitmap, const PPOcrV6Config& config, int originalWidth, int originalHeight)
{
    PPOcrV6DetectionInput output;
    if (originalWidth <= 0 || originalHeight <= 0) return output;

    const int minSide = (std::min)(originalWidth, originalHeight);
    const int maxSide = (std::max)(originalWidth, originalHeight);
    double scale = 1.0;
    if (config.detLimitType == L"max") {
        scale = maxSide > config.detLimitSideLen
            ? static_cast<double>(config.detLimitSideLen) / maxSide : 1.0;
    } else {
        scale = minSide < config.detLimitSideLen
            ? static_cast<double>(config.detLimitSideLen) / minSide : 1.0;
    }
    if (maxSide * scale > config.detMaxSideLimit) {
        scale = static_cast<double>(config.detMaxSideLimit) / maxSide;
    }
    const auto round32 = [](int value) {
        return value <= 0 ? 32 : (std::max)(32, static_cast<int>(std::round(value / 32.0)) * 32);
    };
    int resizeWidth = (std::max)(32, round32(static_cast<int>(originalWidth * scale)));
    int resizeHeight = (std::max)(32, round32(static_cast<int>(originalHeight * scale)));
    resizeWidth = (std::min)(resizeWidth, config.detMaxSideLimit);
    resizeHeight = (std::min)(resizeHeight, config.detMaxSideLimit);
    resizeWidth = round32(resizeWidth);
    resizeHeight = round32(resizeHeight);

    std::vector<unsigned char> pixels;
    if (!ResizePPOcrV6BitmapToPixels(bitmap, resizeWidth, resizeHeight, pixels)) return output;

    output.width = resizeWidth;
    output.height = resizeHeight;
    output.chw.assign(static_cast<size_t>(3) * resizeWidth * resizeHeight, 0.0f);
    constexpr float mean[3] = { 0.485f, 0.456f, 0.406f };
    constexpr float stdv[3] = { 0.229f, 0.224f, 0.225f };
    const size_t plane = static_cast<size_t>(resizeWidth) * resizeHeight;
    for (int y = 0; y < resizeHeight; ++y) {
        for (int x = 0; x < resizeWidth; ++x) {
            const size_t source = (static_cast<size_t>(y) * resizeWidth + x) * 4;
            const float b = pixels[source] / 255.0f;
            const float g = pixels[source + 1] / 255.0f;
            const float r = pixels[source + 2] / 255.0f;
            const size_t destination = static_cast<size_t>(y) * resizeWidth + x;
            output.chw[destination] = (b - mean[0]) / stdv[0];
            output.chw[plane + destination] = (g - mean[1]) / stdv[1];
            output.chw[plane * 2 + destination] = (r - mean[2]) / stdv[2];
        }
    }
    return output;
}

PPOcrV6RecognitionImage BuildPPOcrV6RecognitionImage(HBITMAP bitmap) {
    PPOcrV6RecognitionImage output;
    if (!bitmap) return output;
    BITMAP bm = {};
    if (!GetObject(bitmap, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return output;
    constexpr int kHeight = 48, kMinWidth = 320, kMaxWidth = 3200;
    int contentWidth = (int)std::ceil((double)bm.bmWidth * kHeight / (double)bm.bmHeight);
    contentWidth = (std::clamp)(contentWidth, 1, kMaxWidth);
    output.width = (std::max)(kMinWidth, contentWidth);
    output.chw.assign((size_t)3 * kHeight * output.width, 0.0f);
    std::vector<unsigned char> pixels;
    if (!ResizePPOcrV6BitmapToPixels(bitmap, contentWidth, kHeight, pixels)) {
        output = {};
        return output;
    }
    const size_t plane = (size_t)kHeight * output.width;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < contentWidth; ++x) {
            const size_t source = ((size_t)y * contentWidth + x) * 4;
            const size_t destination = (size_t)y * output.width + x;
            output.chw[destination] = (pixels[source] / 255.0f - 0.5f) / 0.5f;
            output.chw[plane + destination] = (pixels[source + 1] / 255.0f - 0.5f) / 0.5f;
            output.chw[plane * 2 + destination] = (pixels[source + 2] / 255.0f - 0.5f) / 0.5f;
        }
    }
    return output;
}

namespace {

int ClampCropInt(int value, int low, int high) {
    return (std::max)(low, (std::min)(value, high));
}

float CropPointDistance(const Gdiplus::PointF& left, const Gdiplus::PointF& right) {
    const float dx = left.X - right.X;
    const float dy = left.Y - right.Y;
    return std::sqrt(dx * dx + dy * dy);
}

#ifdef ZENCROP_WITH_OPENCV_DBPOST
cv::Mat BitmapToCvBgra(HBITMAP bitmap) {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    GetBitmapBits32(bitmap, width, height, pixels);
    if (width <= 0 || height <= 0 || pixels.empty()) return {};
    return cv::Mat(height, width, CV_8UC4, pixels.data()).clone();
}

HBITMAP CvBgraToBitmap(const cv::Mat& image) {
    if (image.empty() || image.cols <= 0 || image.rows <= 0) return nullptr;
    cv::Mat bgra;
    if (image.channels() == 4) bgra = image;
    else if (image.channels() == 3) cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
    else if (image.channels() == 1) cv::cvtColor(image, bgra, cv::COLOR_GRAY2BGRA);
    else return nullptr;
    if (!bgra.isContinuous()) bgra = bgra.clone();

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bgra.cols;
    info.bmiHeader.biHeight = -bgra.rows;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP output = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!output || !bits) {
        if (output) DeleteObject(output);
        return nullptr;
    }
    std::memcpy(bits, bgra.data, (size_t)bgra.cols * (size_t)bgra.rows * 4);
    return output;
}

HBITMAP CropPerspectiveBitmapOpenCv(HBITMAP source, const PPOcrV6DetectionBox& box) {
    try {
        cv::Mat image = BitmapToCvBgra(source);
        if (image.empty()) return nullptr;
        const float topWidth = CropPointDistance(box.points[0], box.points[1]);
        const float bottomWidth = CropPointDistance(box.points[3], box.points[2]);
        const float leftHeight = CropPointDistance(box.points[0], box.points[3]);
        const float rightHeight = CropPointDistance(box.points[1], box.points[2]);
        const int width = ClampCropInt((int)std::round((std::max)(topWidth, bottomWidth)), 2, 3200);
        const int height = ClampCropInt((int)std::round((std::max)(leftHeight, rightHeight)), 2, 2048);
        if (width <= 1 || height <= 1) return nullptr;
        const std::vector<cv::Point2f> sourcePoints = {
            {box.points[0].X, box.points[0].Y}, {box.points[1].X, box.points[1].Y},
            {box.points[2].X, box.points[2].Y}, {box.points[3].X, box.points[3].Y}};
        const std::vector<cv::Point2f> destinationPoints = {
            {0.0f, 0.0f}, {(float)width, 0.0f}, {(float)width, (float)height}, {0.0f, (float)height}};
        cv::Mat crop;
        cv::warpPerspective(image, crop, cv::getPerspectiveTransform(sourcePoints, destinationPoints),
            cv::Size(width, height), cv::INTER_CUBIC, cv::BORDER_REPLICATE);
        if (crop.rows > 0 && crop.cols > 0 && (double)crop.rows / (double)crop.cols >= 1.5)
            cv::rotate(crop, crop, cv::ROTATE_90_COUNTERCLOCKWISE);
        return CvBgraToBitmap(crop);
    } catch (const cv::Exception& ex) {
        OutputDebugStringA(NarrowFormatPpocrv6CropFailed(ex.what()).c_str());
        return nullptr;
    }
}
#endif

} // namespace

HBITMAP CropPPOcrV6PerspectiveBitmap(HBITMAP source, const PPOcrV6DetectionBox& box) {
#ifdef ZENCROP_WITH_OPENCV_DBPOST
    if (HBITMAP crop = CropPerspectiveBitmapOpenCv(source, box)) return crop;
#endif
    float angle = std::abs(std::atan2(box.points[1].Y - box.points[0].Y, box.points[1].X - box.points[0].X));
    angle = (std::min)(angle, (float)std::abs(3.14159265358979323846 - angle));
    if (angle < 0.21f) return CropBitmap(source, box.rect);
    const float topWidth = CropPointDistance(box.points[0], box.points[1]);
    const float bottomWidth = CropPointDistance(box.points[3], box.points[2]);
    const float leftHeight = CropPointDistance(box.points[0], box.points[3]);
    const float rightHeight = CropPointDistance(box.points[1], box.points[2]);
    const int width = ClampCropInt((int)std::round((std::max)(topWidth, bottomWidth)), 2, 3200);
    const int height = ClampCropInt((int)std::round((std::max)(leftHeight, rightHeight)), 2, 2048);
    if (width <= 1 || height <= 1) return CropBitmap(source, box.rect);

    int sourceWidth = 0, sourceHeight = 0;
    std::vector<uint8_t> sourcePixels;
    GetBitmapBits32(source, sourceWidth, sourceHeight, sourcePixels);
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourcePixels.empty()) return CropBitmap(source, box.rect);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP output = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!output || !bits) {
        if (output) DeleteObject(output);
        return CropBitmap(source, box.rect);
    }
    const auto sample = [&](float fx, float fy, int channel) {
        fx = (std::clamp)(fx, 0.0f, (float)(sourceWidth - 1));
        fy = (std::clamp)(fy, 0.0f, (float)(sourceHeight - 1));
        const int x0 = ClampCropInt((int)std::floor(fx), 0, sourceWidth - 1);
        const int y0 = ClampCropInt((int)std::floor(fy), 0, sourceHeight - 1);
        const int x1 = ClampCropInt(x0 + 1, 0, sourceWidth - 1);
        const int y1 = ClampCropInt(y0 + 1, 0, sourceHeight - 1);
        const float dx = fx - x0, dy = fy - y0;
        const auto at = [&](int x, int y) { return (float)sourcePixels[((size_t)y * sourceWidth + x) * 4 + channel]; };
        const float top = at(x0, y0) * (1.0f - dx) + at(x1, y0) * dx;
        const float bottom = at(x0, y1) * (1.0f - dx) + at(x1, y1) * dx;
        return (uint8_t)ClampCropInt((int)std::round(top * (1.0f - dy) + bottom * dy), 0, 255);
    };
    const Gdiplus::PointF first = box.points[0], second = box.points[1], fourth = box.points[3];
    auto* outputPixels = static_cast<uint8_t*>(bits);
    for (int y = 0; y < height; ++y) {
        const float v = height > 1 ? (float)y / (float)(height - 1) : 0.0f;
        for (int x = 0; x < width; ++x) {
            const float u = width > 1 ? (float)x / (float)(width - 1) : 0.0f;
            const float sx = first.X + (second.X - first.X) * u + (fourth.X - first.X) * v;
            const float sy = first.Y + (second.Y - first.Y) * u + (fourth.Y - first.Y) * v;
            const size_t destination = ((size_t)y * width + x) * 4;
            outputPixels[destination] = sample(sx, sy, 0);
            outputPixels[destination + 1] = sample(sx, sy, 1);
            outputPixels[destination + 2] = sample(sx, sy, 2);
            outputPixels[destination + 3] = 255;
        }
    }
    return output;
}
