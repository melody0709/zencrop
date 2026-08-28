#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <gdiplus.h>

struct OcrSettings;

// Immutable pipeline snapshot. Factory creates it from Settings; PP-OCRv6
// runtime and adapter consume it without depending on Settings.
struct PPOcrV6Config {
    std::wstring modelDir, variant, detModelPath, recModelPath, dictPath;
    std::wstring provider = L"cpu", detLimitType = L"min";
    int cpuThreads = 4, recBatchSize = 1, detLimitSideLen = 960, detMaxSideLimit = 4000;
    float detThresh = 0.20f, detBoxThresh = 0.45f, detUnclipRatio = 1.40f, recScoreThresh = 0.0f;
};
PPOcrV6Config BuildPPOcrV6Config(const OcrSettings& settings);
std::wstring ValidatePPOcrV6Config(const PPOcrV6Config& config);

struct PPOcrV6TensorOutput {
    std::vector<float> values;
    std::vector<int64_t> shape;
};

// Typed ONNX execution boundary. PP-OCRv6 adapter owns pipeline semantics, not
// Ort handles, model I/O discovery or raw tensor invocation.
class PPOcrV6OrtSession final {
public:
    PPOcrV6OrtSession();
    ~PPOcrV6OrtSession();

    bool Initialize(const std::wstring& modelPath, const char* tag, int cpuThreads, std::wstring& error);
    bool Run(const std::vector<float>& input, const std::vector<int64_t>& inputShape,
             PPOcrV6TensorOutput& output, std::wstring& error) const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

struct PPOcrV6Runtime;
std::shared_ptr<PPOcrV6Runtime> AcquirePPOcrV6Runtime(
    const PPOcrV6Config& config,
    std::wstring& error);
bool RunPPOcrV6Detector(PPOcrV6Runtime& runtime,
    const std::vector<float>& input, const std::vector<int64_t>& shape,
    PPOcrV6TensorOutput& output, std::wstring& error);
bool RunPPOcrV6Recognizer(PPOcrV6Runtime& runtime,
    const std::vector<float>& input, const std::vector<int64_t>& shape,
    PPOcrV6TensorOutput& output, std::wstring& error);
const std::vector<std::wstring>& GetPPOcrV6RuntimeDictionary(
    const PPOcrV6Runtime& runtime);

struct PPOcrV6RecognitionImage {
    int width = 0;
    std::vector<float> chw;
};
bool ResizePPOcrV6BitmapToPixels(HBITMAP bitmap, int width, int height,
    std::vector<unsigned char>& pixels);
struct PPOcrV6DetectionInput {
    int width = 0;
    int height = 0;
    std::vector<float> chw;
};
PPOcrV6DetectionInput BuildPPOcrV6DetectionInput(
    HBITMAP bitmap, const PPOcrV6Config& config, int originalWidth, int originalHeight);
PPOcrV6RecognitionImage BuildPPOcrV6RecognitionImage(HBITMAP bitmap);
struct PPOcrV6RecognitionInput {
    int width = 0;
    std::vector<float> chw;
    size_t sourceBoxIndex = SIZE_MAX;
};
struct PPOcrV6RecognitionResult {
    std::wstring text;
    float score = 0.0f;
};
bool RunPPOcrV6RecognitionBatch(PPOcrV6Runtime& runtime,
    const std::vector<PPOcrV6RecognitionInput>& inputs,
    std::vector<PPOcrV6RecognitionResult>& results, std::wstring& error);

struct PPOcrV6DetectionBox {
    RECT rect = {};
    std::array<Gdiplus::PointF, 4> points = {};
    float score = 0.0f;
};
HBITMAP CropPPOcrV6PerspectiveBitmap(HBITMAP source,
    const PPOcrV6DetectionBox& box);
std::vector<PPOcrV6DetectionBox> ExtractPPOcrV6DetectionBoxes(
    const PPOcrV6TensorOutput& tensor, const PPOcrV6Config& config,
    int originalWidth, int originalHeight);
inline const char* PPOcrV6DetectorPostprocessName() {
#ifdef ZENCROP_WITH_OPENCV_DBPOST
    return "opencv+clipper2";
#else
    return "fallback+clipper2";
#endif
}
