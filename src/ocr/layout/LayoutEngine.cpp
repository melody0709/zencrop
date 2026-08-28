#include "LayoutEngine.h"
#include "OcrUtils.h"
#include "PaddleDocLayoutPreprocessor.h"
#include "PaddleDocLayoutPostprocess.h"
#include "Settings.h"
#include "core/Sha256.h"
#include "core/WideStringUtils.h"
#include "core/NarrowStringUtils.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <numeric>

#define ORT_NO_EXCEPTIONS
#include "onnxruntime_c_api.h"

static const OrtApi* g_ort = nullptr;
static bool g_ortInitialized = false;

static bool InitOrtApi() {
    if (g_ortInitialized) return g_ort != nullptr;
    g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    g_ortInitialized = true;
    if (!g_ort) OutputDebugStringA("[LayoutEngine] Failed to get ONNX Runtime API\n");
    return g_ort != nullptr;
}

static bool ShouldKeepDetectedLayoutClassLegacy(int classId) {
    const auto* info = GetLayoutClassInfo(classId);
    if (!info) return false;

    if (info->skipRecognition && !info->cropImage) {
        return info->ignoreInMarkdown;
    }

    return true;
}

struct LayoutTensorMeta {
    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    std::vector<int64_t> shape;
};

static bool ReadSessionOutputMeta(
    OrtSession* session,
    size_t index,
    LayoutTensorMeta& meta)
{
    OrtTypeInfo* typeInfo = nullptr;
    OrtStatus* status = g_ort->SessionGetOutputTypeInfo(session, index, &typeInfo);
    if (status) {
        g_ort->ReleaseStatus(status);
        return false;
    }
    const OrtTensorTypeAndShapeInfo* tensorInfo = nullptr;
    status = g_ort->CastTypeInfoToTensorInfo(typeInfo, &tensorInfo);
    if (status || !tensorInfo) {
        if (status) g_ort->ReleaseStatus(status);
        g_ort->ReleaseTypeInfo(typeInfo);
        return false;
    }
    status = g_ort->GetTensorElementType(tensorInfo, &meta.type);
    if (status) {
        g_ort->ReleaseStatus(status);
        g_ort->ReleaseTypeInfo(typeInfo);
        return false;
    }
    size_t dimensions = 0;
    status = g_ort->GetDimensionsCount(tensorInfo, &dimensions);
    if (status) {
        g_ort->ReleaseStatus(status);
        g_ort->ReleaseTypeInfo(typeInfo);
        return false;
    }
    meta.shape.assign(dimensions, 0);
    if (dimensions > 0) {
        status = g_ort->GetDimensions(tensorInfo, meta.shape.data(), dimensions);
        if (status) {
            g_ort->ReleaseStatus(status);
            g_ort->ReleaseTypeInfo(typeInfo);
            return false;
        }
    }
    g_ort->ReleaseTypeInfo(typeInfo);
    return true;
}

static bool ReadValueMeta(OrtValue* value, LayoutTensorMeta& meta) {
    if (!value) return false;
    OrtTensorTypeAndShapeInfo* tensorInfo = nullptr;
    OrtStatus* status = g_ort->GetTensorTypeAndShape(value, &tensorInfo);
    if (status || !tensorInfo) {
        if (status) g_ort->ReleaseStatus(status);
        return false;
    }
    status = g_ort->GetTensorElementType(tensorInfo, &meta.type);
    if (status) {
        g_ort->ReleaseStatus(status);
        g_ort->ReleaseTensorTypeAndShapeInfo(tensorInfo);
        return false;
    }
    size_t dimensions = 0;
    status = g_ort->GetDimensionsCount(tensorInfo, &dimensions);
    if (status) {
        g_ort->ReleaseStatus(status);
        g_ort->ReleaseTensorTypeAndShapeInfo(tensorInfo);
        return false;
    }
    meta.shape.assign(dimensions, 0);
    if (dimensions > 0) {
        status = g_ort->GetDimensions(tensorInfo, meta.shape.data(), dimensions);
        if (status) {
            g_ort->ReleaseStatus(status);
            g_ort->ReleaseTensorTypeAndShapeInfo(tensorInfo);
            return false;
        }
    }
    g_ort->ReleaseTensorTypeAndShapeInfo(tensorInfo);
    return true;
}

static bool IsOutputName(const std::string& name, const char* canonical, const char* alias) {
    return name == canonical || (alias != nullptr && name == alias);
}

static uint64_t GetFileByteSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return 0;
    }
    return (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) |
        static_cast<uint64_t>(attributes.nFileSizeLow);
}

LayoutEngine::LayoutEngine() {}

LayoutEngine::~LayoutEngine() {
    if (m_session && g_ort) {
        g_ort->ReleaseSession((OrtSession*)m_session);
        m_session = nullptr;
    }
    if (m_env && g_ort) {
        g_ort->ReleaseEnv((OrtEnv*)m_env);
        m_env = nullptr;
    }
}

void LayoutEngine::Reset() {
    if (m_session && g_ort) {
        g_ort->ReleaseSession((OrtSession*)m_session);
        m_session = nullptr;
    }
    if (m_env && g_ort) {
        g_ort->ReleaseEnv((OrtEnv*)m_env);
        m_env = nullptr;
    }
    m_allocator = nullptr;
    m_loaded = false;
    m_modelPath.clear();
    m_modelBytes = 0;
    m_modelSha256.clear();
    m_modelSha256Error.clear();
    m_modelFamily = LayoutModelFamily::Unknown;
    m_profile = {};
    m_inputNames.clear();
    m_outputNames.clear();
    m_boxesOutputIndex = -1;
    m_bboxNumOutputIndex = -1;
    m_masksOutputIndex = -1;
}

bool LayoutEngine::Initialize(const std::wstring& modelPath) {
    if (m_loaded) return true;
    if (!InitOrtApi()) {
        OutputDebugStringA("[LayoutEngine] ONNX Runtime not available\n");
        return false;
    }

    OrtStatus* status = nullptr;

    OrtEnv* env = nullptr;
    status = g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "LayoutEngine", &env);
    if (status) { g_ort->ReleaseStatus(status); return false; }
    m_env = env;

    OrtSessionOptions* opts = nullptr;
    g_ort->CreateSessionOptions(&opts);
    g_ort->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL);
    g_ort->SetInterOpNumThreads(opts, 1);
    g_ort->SetIntraOpNumThreads(opts, 4);

    OrtSession* session = nullptr;
    status = g_ort->CreateSession(env, modelPath.c_str(), opts, &session);
    g_ort->ReleaseSessionOptions(opts);
    if (status) {
        const char* msg = g_ort->GetErrorMessage(status);
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLayoutCreateSessionFailed(msg).c_str());
        g_ort->ReleaseStatus(status);
        Reset();
        return false;
    }
    m_session = session;

    OrtAllocator* allocator = nullptr;
    g_ort->GetAllocatorWithDefaultOptions(&allocator);
    m_allocator = allocator;

    OcrSettings settings = LoadOcrSettings();
    m_modelPath = modelPath;
    m_modelBytes = GetFileByteSize(modelPath);
    m_modelSha256.clear();
    m_modelSha256Error.clear();
    ComputeFileSha256Hex(modelPath, m_modelSha256, m_modelSha256Error);
    m_modelFamily = ResolveLayoutModelFamily(settings.layoutModelFamily, modelPath);
    m_profile = BuildPaddleDocLayoutProfile(
        m_modelFamily,
        ParseLayoutThresholdProfile(settings.layoutThresholdProfile));

    size_t numInputs = 0;
    size_t numOutputs = 0;
    g_ort->SessionGetInputCount(session, &numInputs);
    g_ort->SessionGetOutputCount(session, &numOutputs);
    m_inputNames.clear();
    m_outputNames.clear();
    m_inputNames.reserve(numInputs);
    m_outputNames.reserve(numOutputs);

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutOnnxLoaded(
        LayoutModelFamilyName(m_modelFamily), numInputs, numOutputs).c_str());
    if (m_modelSha256.empty()) {
        OutputDebugStringW((L"[LayoutEngine] ONNX SHA-256 unavailable: " +
            m_modelSha256Error + L"\n").c_str());
    }

    for (size_t i = 0; i < numInputs; ++i) {
        char* name = nullptr;
        g_ort->SessionGetInputName(session, i, allocator, &name);
        m_inputNames.emplace_back(name ? name : "");
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLayoutInput(i, m_inputNames.back().c_str()).c_str());
        if (name) g_ort->AllocatorFree(allocator, name);
    }

    std::vector<LayoutTensorMeta> outputMeta(numOutputs);
    for (size_t i = 0; i < numOutputs; ++i) {
        char* name = nullptr;
        g_ort->SessionGetOutputName(session, i, allocator, &name);
        m_outputNames.emplace_back(name ? name : "");
        if (name) g_ort->AllocatorFree(allocator, name);

        const std::string& outputName = m_outputNames.back();
        if (IsOutputName(outputName, "fetch_name_0", "boxes")) {
            m_boxesOutputIndex = (int)i;
        } else if (IsOutputName(outputName, "fetch_name_1", "bbox_num")) {
            m_bboxNumOutputIndex = (int)i;
        } else if (IsOutputName(outputName, "fetch_name_2", "masks")) {
            m_masksOutputIndex = (int)i;
        }

        if (!ReadSessionOutputMeta(session, i, outputMeta[i])) {
            OutputDebugStringA("[LayoutEngine] Failed to inspect output tensor metadata\n");
            Reset();
            return false;
        }
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLayoutOutput(
            i, outputName.c_str(), (int)outputMeta[i].type,
            outputMeta[i].shape.size()).c_str());
    }

    const bool knownFamily = m_modelFamily == LayoutModelFamily::PPDocLayoutV3 ||
        m_modelFamily == LayoutModelFamily::PPDocLayoutV2;
    bool hasExpectedInputs = numInputs == 3;
    bool sawImage = false;
    bool sawImageShape = false;
    bool sawScaleFactor = false;
    for (const auto& name : m_inputNames) {
        sawImage |= name == "image";
        sawImageShape |= name == "im_shape";
        sawScaleFactor |= name == "scale_factor";
    }
    hasExpectedInputs &= sawImage && sawImageShape && sawScaleFactor;

    bool outputContractValid = m_boxesOutputIndex >= 0 &&
        m_bboxNumOutputIndex >= 0;
    if (outputContractValid) {
        const auto& boxes = outputMeta[(size_t)m_boxesOutputIndex];
        const auto& bboxNum = outputMeta[(size_t)m_bboxNumOutputIndex];
        outputContractValid = boxes.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
            boxes.shape.size() == 2 &&
            (m_modelFamily == LayoutModelFamily::PPDocLayoutV3
                ? ((boxes.shape[0] == 300 || boxes.shape[0] == -1) &&
                    boxes.shape[1] == 7)
                : (boxes.shape[1] == 6 || boxes.shape[1] == 7)) &&
            bboxNum.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 &&
            bboxNum.shape.size() == 1 &&
            (bboxNum.shape[0] == 1 || bboxNum.shape[0] == -1);
    }
    if (m_modelFamily == LayoutModelFamily::PPDocLayoutV3) {
        outputContractValid &= m_masksOutputIndex >= 0;
        if (m_masksOutputIndex >= 0) {
            const auto& masks = outputMeta[(size_t)m_masksOutputIndex];
            outputContractValid &= masks.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 &&
                masks.shape.size() == 3 &&
                (masks.shape[0] == 300 || masks.shape[0] == -1) &&
                masks.shape[1] == 200 && masks.shape[2] == 200;
        }
    }

    if (knownFamily && (!hasExpectedInputs || !outputContractValid)) {
        OutputDebugStringA(
            "[LayoutEngine] Model family/output shape contract mismatch; initialization rejected\n");
        Reset();
        return false;
    }
    if (!knownFamily) {
        m_boxesOutputIndex = numOutputs > 0 ? 0 : -1;
        OutputDebugStringA(
            "[LayoutEngine] WARNING: unknown layout family; using explicit legacy postprocess\n");
        if (m_boxesOutputIndex < 0 || numInputs < 3) {
            Reset();
            return false;
        }
    }

    m_loaded = true;
    OutputDebugStringA("[LayoutEngine] Initialized\n");
    return true;
}

std::vector<float> LayoutEngine::Preprocess(HBITMAP hBitmap, int& origW, int& origH,
                                              float& scaleH, float& scaleW) {
    PaddleDocLayoutInput input;
    std::string error;
    if (!BuildPaddleDocLayoutInput(hBitmap, input, &error)) {
        origW = 0;
        origH = 0;
        scaleH = 1.0f;
        scaleW = 1.0f;
        if (!error.empty()) {
            OutputDebugStringA("[LayoutEngine] Preprocess failed: ");
            OutputDebugStringA(error.c_str());
            OutputDebugStringA("\n");
        }
        return {};
    }
    origW = input.originalWidth;
    origH = input.originalHeight;
    scaleH = input.scaleHeight;
    scaleW = input.scaleWidth;
    return std::move(input.chw);
}


std::vector<LayoutRegion> LayoutEngine::DetectSingle(
    HBITMAP hBitmap,
    LONG offsetX,
    LONG offsetY,
    int orderBase,
    bool logDetails,
    LayoutDetectionStageCounts* stageCounts)
{
    if (stageCounts) *stageCounts = {};
    std::vector<LayoutRegion> regions;
    if (!m_loaded || !m_session || m_inputNames.size() < 3 ||
        m_outputNames.empty() || m_boxesOutputIndex < 0 || !hBitmap) {
        return regions;
    }

    int origW = 0;
    int origH = 0;
    float scaleH = 1.0f;
    float scaleW = 1.0f;
    auto inputBlob = Preprocess(hBitmap, origW, origH, scaleH, scaleW);
    if (origW <= 0 || origH <= 0) return regions;

    if (logDetails) {
        const float aspect = origH > 0 ? (float)origW / origH : 0.0f;
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatLayoutDetectImage(
            origW, origH, aspect, scaleH, scaleW,
            LayoutModelFamilyName(m_modelFamily)).c_str());
    }

    OrtMemoryInfo* memoryInfo = nullptr;
    OrtStatus* status = g_ort->CreateCpuMemoryInfo(
        OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo);
    if (status || !memoryInfo) {
        if (status) g_ort->ReleaseStatus(status);
        if (stageCounts) stageCounts->error = "failed to create ONNX memory info";
        return regions;
    }

    OrtValue* shapeTensor = nullptr;
    OrtValue* imageTensor = nullptr;
    OrtValue* scaleTensor = nullptr;
    const int64_t pairShape[] = { 1, 2 };
    const int64_t imageShape[] = { 1, 3, 800, 800 };
    float shapeData[] = { 800.0f, 800.0f };
    float scaleData[] = { scaleH, scaleW };

    auto releaseInputs = [&]() {
        if (shapeTensor) g_ort->ReleaseValue(shapeTensor);
        if (imageTensor) g_ort->ReleaseValue(imageTensor);
        if (scaleTensor) g_ort->ReleaseValue(scaleTensor);
        shapeTensor = nullptr;
        imageTensor = nullptr;
        scaleTensor = nullptr;
    };

    status = g_ort->CreateTensorWithDataAsOrtValue(
        memoryInfo, shapeData, sizeof(shapeData), pairShape, 2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &shapeTensor);
    if (!status) {
        status = g_ort->CreateTensorWithDataAsOrtValue(
            memoryInfo, inputBlob.data(), inputBlob.size() * sizeof(float),
            imageShape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &imageTensor);
    }
    if (!status) {
        status = g_ort->CreateTensorWithDataAsOrtValue(
            memoryInfo, scaleData, sizeof(scaleData), pairShape, 2,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &scaleTensor);
    }
    g_ort->ReleaseMemoryInfo(memoryInfo);
    if (status || !shapeTensor || !imageTensor || !scaleTensor) {
        if (status) g_ort->ReleaseStatus(status);
        releaseInputs();
        if (stageCounts) stageCounts->error = "failed to create ONNX input tensors";
        return regions;
    }

    std::vector<const char*> inputNamePointers;
    std::vector<const OrtValue*> inputValues;
    inputNamePointers.reserve(m_inputNames.size());
    inputValues.reserve(m_inputNames.size());
    for (size_t index = 0; index < m_inputNames.size(); ++index) {
        const std::string& name = m_inputNames[index];
        inputNamePointers.push_back(name.c_str());
        if (name == "im_shape") inputValues.push_back(shapeTensor);
        else if (name == "image") inputValues.push_back(imageTensor);
        else if (name == "scale_factor") inputValues.push_back(scaleTensor);
        else if (index == 0) inputValues.push_back(shapeTensor);
        else if (index == 1) inputValues.push_back(imageTensor);
        else inputValues.push_back(scaleTensor);
    }

    std::vector<const char*> outputNamePointers;
    outputNamePointers.reserve(m_outputNames.size());
    for (const auto& name : m_outputNames) {
        outputNamePointers.push_back(name.c_str());
    }
    std::vector<OrtValue*> outputs(m_outputNames.size(), nullptr);
    status = g_ort->Run(
        (OrtSession*)m_session, nullptr,
        inputNamePointers.data(), inputValues.data(), inputValues.size(),
        outputNamePointers.data(), outputNamePointers.size(), outputs.data());
    releaseInputs();
    if (status) {
        const char* message = g_ort->GetErrorMessage(status);
        const std::string errorMessage = message ? message : "unknown";
        OutputDebugStringA("[LayoutEngine] Run failed: ");
        OutputDebugStringA(errorMessage.c_str());
        OutputDebugStringA("\n");
        g_ort->ReleaseStatus(status);
        for (OrtValue* value : outputs) if (value) g_ort->ReleaseValue(value);
        if (stageCounts) {
            stageCounts->error = "ONNX Run failed: ";
            stageCounts->error += errorMessage;
        }
        return regions;
    }

    auto releaseOutputs = [&]() {
        for (OrtValue*& value : outputs) {
            if (value) g_ort->ReleaseValue(value);
            value = nullptr;
        }
    };
    auto contractFailure = [&](const char* message) {
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        if (stageCounts) stageCounts->error = message;
        releaseOutputs();
    };

    if ((size_t)m_boxesOutputIndex >= outputs.size()) {
        contractFailure("[LayoutEngine] boxes output index is invalid");
        return regions;
    }
    OrtValue* boxesValue = outputs[(size_t)m_boxesOutputIndex];
    LayoutTensorMeta boxesMeta;
    if (!ReadValueMeta(boxesValue, boxesMeta) ||
        boxesMeta.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        boxesMeta.shape.size() != 2 || boxesMeta.shape[0] < 0 ||
        boxesMeta.shape[1] < 6) {
        contractFailure("[LayoutEngine] boxes tensor runtime contract mismatch");
        return regions;
    }
    const size_t boxCapacity = (size_t)boxesMeta.shape[0];
    const size_t columns = (size_t)boxesMeta.shape[1];
    if (m_modelFamily == LayoutModelFamily::PPDocLayoutV3 &&
        (boxCapacity != 300 || columns != 7)) {
        contractFailure("[LayoutEngine] V3 boxes must have shape [300,7]");
        return regions;
    }

    float* boxesData = nullptr;
    status = g_ort->GetTensorMutableData(boxesValue, (void**)&boxesData);
    if (status || !boxesData) {
        if (status) g_ort->ReleaseStatus(status);
        contractFailure("[LayoutEngine] boxes tensor data unavailable");
        return regions;
    }

    size_t validCount = boxCapacity;
    PaddleDocMaskTensorView maskView;
    const bool knownFamily = m_modelFamily == LayoutModelFamily::PPDocLayoutV3 ||
        m_modelFamily == LayoutModelFamily::PPDocLayoutV2;
    if (knownFamily) {
        if (m_bboxNumOutputIndex < 0 ||
            (size_t)m_bboxNumOutputIndex >= outputs.size()) {
            contractFailure("[LayoutEngine] bbox_num output missing");
            return regions;
        }
        OrtValue* countValue = outputs[(size_t)m_bboxNumOutputIndex];
        LayoutTensorMeta countMeta;
        if (!ReadValueMeta(countValue, countMeta) ||
            countMeta.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 ||
            countMeta.shape.size() != 1 || countMeta.shape[0] != 1) {
            contractFailure("[LayoutEngine] bbox_num tensor runtime contract mismatch");
            return regions;
        }
        int32_t* countData = nullptr;
        status = g_ort->GetTensorMutableData(countValue, (void**)&countData);
        if (status || !countData) {
            if (status) g_ort->ReleaseStatus(status);
            contractFailure("[LayoutEngine] bbox_num tensor data unavailable");
            return regions;
        }

        size_t maskCapacity = 0;
        if (m_modelFamily == LayoutModelFamily::PPDocLayoutV3) {
            if (m_masksOutputIndex < 0 ||
                (size_t)m_masksOutputIndex >= outputs.size()) {
                contractFailure("[LayoutEngine] V3 masks output missing");
                return regions;
            }
            OrtValue* masksValue = outputs[(size_t)m_masksOutputIndex];
            LayoutTensorMeta masksMeta;
            if (!ReadValueMeta(masksValue, masksMeta) ||
                masksMeta.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 ||
                masksMeta.shape.size() != 3 || masksMeta.shape[0] != 300 ||
                masksMeta.shape[1] != 200 || masksMeta.shape[2] != 200) {
                contractFailure("[LayoutEngine] V3 masks tensor runtime contract mismatch");
                return regions;
            }
            int32_t* masksData = nullptr;
            status = g_ort->GetTensorMutableData(masksValue, (void**)&masksData);
            if (status || !masksData) {
                if (status) g_ort->ReleaseStatus(status);
                contractFailure("[LayoutEngine] V3 masks tensor data unavailable");
                return regions;
            }
            maskCapacity = (size_t)masksMeta.shape[0];
            maskView = { masksData, maskCapacity, 200, 200 };
        }
        if (!ValidatePaddleDocBboxCount(
            countData[0], boxCapacity, maskCapacity,
            m_modelFamily == LayoutModelFamily::PPDocLayoutV3)) {
            contractFailure("[LayoutEngine] bbox_num prefix is outside output capacity");
            return regions;
        }
        validCount = (size_t)countData[0];
    }

    if (logDetails) {
        const size_t dumpCount = (std::min)((size_t)10, validCount);
        for (size_t index = 0; index < dumpCount; ++index) {
            const float* row = boxesData + index * columns;
            // OWN-116: pure narrow debug (NarrowStringUtils).
            OutputDebugStringA(NarrowFormatLayoutQueryRow(
                index, (int)row[0], row[1], row[2], row[3], row[4], row[5],
                columns >= 7 ? (int)row[6] : (int)index).c_str());
        }
    }

    if (m_profile.legacyPostprocess) {
        if (stageCounts) stageCounts->raw = validCount;
        for (size_t index = 0; index < validCount; ++index) {
            const float* row = boxesData + index * columns;
            const int classId = (int)row[0];
            if (classId < 0 || classId >= LAYOUT_NUM_CLASSES ||
                !PaddleDocScorePasses(m_profile, classId, row[1]) ||
                !ShouldKeepDetectedLayoutClassLegacy(classId)) {
                continue;
            }
            const double left = (std::max)(0.0, (double)row[2]);
            const double top = (std::max)(0.0, (double)row[3]);
            const double right = (std::min)((double)origW, (double)row[4]);
            const double bottom = (std::min)((double)origH, (double)row[5]);
            if (right - left < 10.0 || bottom - top < 10.0) continue;
            const auto* info = GetLayoutClassInfo(classId);
            LayoutRegion region;
            region.bbox = {
                (LONG)left + offsetX, (LONG)top + offsetY,
                (LONG)right + offsetX, (LONG)bottom + offsetY
            };
            region.classId = classId;
            region.className = info->name;
            region.confidence = row[1];
            region.readingOrder = orderBase +
                (columns >= 7 ? (int)row[6] : (int)index);
            region.vlmPrompt = info->vlmPrompt;
            region.headingLevel = info->headingLevel;
            region.queryIndex = index;
            region.polygon = {
                {(float)region.bbox.left, (float)region.bbox.top},
                {(float)region.bbox.right, (float)region.bbox.top},
                {(float)region.bbox.right, (float)region.bbox.bottom},
                {(float)region.bbox.left, (float)region.bbox.bottom},
            };
            regions.push_back(std::move(region));
        }
        if (stageCounts) {
            stageCounts->scorePassed = regions.size();
            stageCounts->nmsKept = regions.size();
            stageCounts->imageAreaKept = regions.size();
            stageCounts->classModeKept = regions.size();
            stageCounts->overlapKept = regions.size();
            stageCounts->finalCount = regions.size();
        }
        releaseOutputs();
        return regions;
    }

    std::vector<PaddleDocLayoutCandidate> candidates;
    candidates.reserve(validCount);
    for (size_t index = 0; index < validCount; ++index) {
        const float* row = boxesData + index * columns;
        candidates.push_back({
            (int)row[0], row[1], row[2], row[3], row[4], row[5],
            columns >= 7 ? (int)row[6] : (int)index, index, {}, false
        });
    }

    PaddleDocPostprocessOptions options;
    options.profile = m_profile;
    options.imageWidth = origW;
    options.imageHeight = origH;
    options.modelInputWidth = 800;
    options.modelInputHeight = 800;
    options.offsetX = offsetX;
    options.offsetY = offsetY;
    options.readingOrderBase = orderBase;
    PaddleDocPostprocessStats stats;
    auto processed = PostprocessPaddleDocLayoutCandidates(
        std::move(candidates), maskView, options, &stats);
    releaseOutputs();

    if (stageCounts) {
        stageCounts->raw = stats.raw;
        stageCounts->scorePassed = stats.scorePassed;
        stageCounts->nmsKept = stats.nmsKept;
        stageCounts->imageAreaKept = stats.imageAreaKept;
        stageCounts->classModeKept = stats.classModeKept;
        stageCounts->polygonFallbacks = stats.polygonFallbacks;
        stageCounts->overlapKept = stats.overlapKept;
        stageCounts->finalCount = stats.finalCount;
        stageCounts->exactScoreTies = stats.exactScoreTies;
        stageCounts->polygonDegraded = stats.v3PolygonDegraded;
        stageCounts->error = stats.error;
    }

    if (!stats.error.empty()) {
        OutputDebugStringA("[LayoutEngine] Official postprocess failed: ");
        OutputDebugStringA(stats.error.c_str());
        OutputDebugStringA("\n");
        return regions;
    }

    for (auto& candidate : processed) {
        const auto* info = GetLayoutClassInfo(candidate.classId);
        if (!info) continue;
        LayoutRegion region;
        region.bbox = {
            (LONG)PaddleDocRoundNearestEven(candidate.left),
            (LONG)PaddleDocRoundNearestEven(candidate.top),
            (LONG)PaddleDocRoundNearestEven(candidate.right),
            (LONG)PaddleDocRoundNearestEven(candidate.bottom),
        };
        region.classId = candidate.classId;
        region.className = info->name;
        region.confidence = candidate.confidence;
        region.readingOrder = candidate.readingOrder;
        region.vlmPrompt = info->vlmPrompt;
        region.headingLevel = info->headingLevel;
        region.polygonFromMask = candidate.polygonFromMask;
        region.queryIndex = candidate.queryIndex;
        region.polygon.reserve(candidate.polygon.size());
        for (const auto& point : candidate.polygon) {
            region.polygon.push_back({ point.x, point.y });
        }
        regions.push_back(std::move(region));
    }

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutPostprocessStats(
        stats.raw, stats.scorePassed, stats.nmsKept, stats.imageAreaKept,
        stats.classModeKept, stats.polygonFallbacks, stats.overlapKept,
        stats.finalCount, stats.exactScoreTies, stats.v3PolygonDegraded ? 1 : 0).c_str());
    return regions;
}

static void RecursiveXYCut(const std::vector<LayoutRegion>& regions, const std::vector<int>& activeIndices, std::vector<int>& sortedIndices) {
    if (activeIndices.empty()) return;
    if (activeIndices.size() == 1) {
        sortedIndices.push_back(activeIndices[0]);
        return;
    }

    LONG minX = regions[activeIndices[0]].bbox.left;
    LONG maxX = regions[activeIndices[0]].bbox.right;
    LONG minY = regions[activeIndices[0]].bbox.top;
    LONG maxY = regions[activeIndices[0]].bbox.bottom;
    for (int idx : activeIndices) {
        minX = (std::min)(minX, regions[idx].bbox.left);
        maxX = (std::max)(maxX, regions[idx].bbox.right);
        minY = (std::min)(minY, regions[idx].bbox.top);
        maxY = (std::max)(maxY, regions[idx].bbox.bottom);
    }

    // 1. Try vertical cut (X-gap) - Prioritized for stable column division
    int xLen = maxX - minX + 1;
    if (xLen > 0 && xLen < 20000) {
        std::vector<int> xProj(xLen, 0);
        for (int idx : activeIndices) {
            int start = (std::max)(0, (int)(regions[idx].bbox.left - minX));
            int end = (std::min)(xLen - 1, (int)(regions[idx].bbox.right - minX));
            for (int x = start; x < end; ++x) {
                xProj[x]++;
            }
        }

        int bestCutX = -1;
        int maxGapLen = 0;
        int currentGapStart = -1;
        for (int x = 0; x < xLen; ++x) {
            if (xProj[x] == 0) {
                if (currentGapStart == -1) {
                    currentGapStart = x;
                }
            } else {
                if (currentGapStart != -1) {
                    int gapLen = x - currentGapStart;
                    if (gapLen > maxGapLen) {
                        maxGapLen = gapLen;
                        bestCutX = currentGapStart + gapLen / 2;
                    }
                    currentGapStart = -1;
                }
            }
        }
        if (currentGapStart != -1) {
            int gapLen = xLen - currentGapStart;
            if (gapLen > maxGapLen) {
                maxGapLen = gapLen;
                bestCutX = currentGapStart + gapLen / 2;
            }
        }

        if (bestCutX != -1 && maxGapLen >= 3) {
            LONG cutX = minX + bestCutX;
            std::vector<int> left, right;
            for (int idx : activeIndices) {
                LONG centerX = (regions[idx].bbox.left + regions[idx].bbox.right) / 2;
                if (centerX < cutX) {
                    left.push_back(idx);
                } else {
                    right.push_back(idx);
                }
            }
            if (!left.empty() && !right.empty()) {
                RecursiveXYCut(regions, left, sortedIndices);
                RecursiveXYCut(regions, right, sortedIndices);
                return;
            }
        }
    }

    // 2. Try horizontal cut (Y-gap) - Fallback for row-based division
    int yLen = maxY - minY + 1;
    if (yLen > 0 && yLen < 20000) {
        std::vector<int> yProj(yLen, 0);
        for (int idx : activeIndices) {
            int start = (std::max)(0, (int)(regions[idx].bbox.top - minY));
            int end = (std::min)(yLen - 1, (int)(regions[idx].bbox.bottom - minY));
            for (int y = start; y < end; ++y) {
                yProj[y]++;
            }
        }

        int bestCutY = -1;
        int maxGapLen = 0;
        int currentGapStart = -1;
        for (int y = 0; y < yLen; ++y) {
            if (yProj[y] == 0) {
                if (currentGapStart == -1) {
                    currentGapStart = y;
                }
            } else {
                if (currentGapStart != -1) {
                    int gapLen = y - currentGapStart;
                    if (gapLen > maxGapLen) {
                        maxGapLen = gapLen;
                        bestCutY = currentGapStart + gapLen / 2;
                    }
                    currentGapStart = -1;
                }
            }
        }
        if (currentGapStart != -1) {
            int gapLen = yLen - currentGapStart;
            if (gapLen > maxGapLen) {
                maxGapLen = gapLen;
                bestCutY = currentGapStart + gapLen / 2;
            }
        }

        if (bestCutY != -1 && maxGapLen >= 3) {
            LONG cutY = minY + bestCutY;
            std::vector<int> above, below;
            for (int idx : activeIndices) {
                LONG centerY = (regions[idx].bbox.top + regions[idx].bbox.bottom) / 2;
                if (centerY < cutY) {
                    above.push_back(idx);
                } else {
                    below.push_back(idx);
                }
            }
            if (!above.empty() && !below.empty()) {
                RecursiveXYCut(regions, above, sortedIndices);
                RecursiveXYCut(regions, below, sortedIndices);
                return;
            }
        }
    }

    // 3. Fallback: Sort by top-to-bottom, left-to-right (simple row-based sorting)
    std::vector<int> fallbackIndices = activeIndices;
    std::sort(fallbackIndices.begin(), fallbackIndices.end(), [&regions](int aIdx, int bIdx) {
        const auto& a = regions[aIdx];
        const auto& b = regions[bIdx];
        LONG ah = a.bbox.bottom - a.bbox.top;
        LONG bh = b.bbox.bottom - b.bbox.top;
        LONG rowTolerance = (std::max)(20L, (std::min)(ah, bh) / 2);
        if (std::abs(a.bbox.top - b.bbox.top) > rowTolerance) {
            return a.bbox.top < b.bbox.top;
        }
        if (a.bbox.left != b.bbox.left) return a.bbox.left < b.bbox.left;
        return a.readingOrder < b.readingOrder;
    });

    for (int idx : fallbackIndices) {
        sortedIndices.push_back(idx);
    }
}

void LayoutEngine::SortRegions(
    std::vector<LayoutRegion>& regions,
    bool sortByPosition)
{
    if (regions.empty()) return;
    if (!sortByPosition) {
        std::stable_sort(
            regions.begin(), regions.end(),
            [](const LayoutRegion& first, const LayoutRegion& second) {
                if (first.readingOrder != second.readingOrder) {
                    return first.readingOrder < second.readingOrder;
                }
                return first.queryIndex < second.queryIndex;
            });
        return;
    }

    std::vector<int> activeIndices(regions.size());
    std::iota(activeIndices.begin(), activeIndices.end(), 0);
    std::vector<int> sortedIndices;
    sortedIndices.reserve(regions.size());
    RecursiveXYCut(regions, activeIndices, sortedIndices);

    std::vector<LayoutRegion> sortedRegions;
    sortedRegions.reserve(regions.size());
    for (int index : sortedIndices) {
        sortedRegions.push_back(std::move(regions[(size_t)index]));
    }
    regions = std::move(sortedRegions);
}

namespace {

bool IsInternalTileFragment(
    const LayoutRegion& region,
    int pageWidth,
    int pageHeight)
{
    if (!region.fromTile) return false;
    constexpr LONG kSeamMargin = 32;
    const RECT& tile = region.sourceTile;
    if (tile.right <= tile.left || tile.bottom <= tile.top) return false;
    const bool touchesInternalLeft = tile.left > 0 &&
        region.bbox.left <= tile.left + kSeamMargin;
    const bool touchesInternalTop = tile.top > 0 &&
        region.bbox.top <= tile.top + kSeamMargin;
    const bool touchesInternalRight = tile.right < pageWidth &&
        region.bbox.right >= tile.right - kSeamMargin;
    const bool touchesInternalBottom = tile.bottom < pageHeight &&
        region.bbox.bottom >= tile.bottom - kSeamMargin;
    return touchesInternalLeft || touchesInternalTop ||
        touchesInternalRight || touchesInternalBottom;
}

double RegionSmallOverlap(const LayoutRegion& first, const LayoutRegion& second) {
    return PaddleDocExclusiveSmallOverlap(
        first.bbox.left, first.bbox.top, first.bbox.right, first.bbox.bottom,
        second.bbox.left, second.bbox.top, second.bbox.right, second.bbox.bottom);
}

bool PolygonSmallOverlapPasses(
    const LayoutRegion& first,
    const LayoutRegion& second)
{
    if (!first.polygonFromMask || !second.polygonFromMask) return false;
    std::vector<PaddleDocPointF> firstPolygon;
    std::vector<PaddleDocPointF> secondPolygon;
    firstPolygon.reserve(first.polygon.size());
    secondPolygon.reserve(second.polygon.size());
    for (const auto& point : first.polygon) firstPolygon.push_back({point.x, point.y});
    for (const auto& point : second.polygon) secondPolygon.push_back({point.x, point.y});
    double overlap = 0.0;
    return PaddleDocPolygonSmallOverlap(firstPolygon, secondPolygon, overlap) &&
        overlap >= 0.7;
}

} // namespace

void LayoutEngine::ReconcileTileRegions(
    std::vector<LayoutRegion>& regions,
    int pageWidth,
    int pageHeight)
{
    if (regions.size() < 2) return;
    std::vector<size_t> order(regions.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&regions, pageWidth, pageHeight](size_t first, size_t second) {
        const bool firstFragment = IsInternalTileFragment(
            regions[first], pageWidth, pageHeight);
        const bool secondFragment = IsInternalTileFragment(
            regions[second], pageWidth, pageHeight);
        if (firstFragment != secondFragment) return !firstFragment;
        if (regions[first].confidence != regions[second].confidence) {
            return regions[first].confidence > regions[second].confidence;
        }
        if (regions[first].readingOrder != regions[second].readingOrder) {
            return regions[first].readingOrder > regions[second].readingOrder;
        }
        return regions[first].queryIndex > regions[second].queryIndex;
    });

    std::vector<bool> suppressed(regions.size(), false);
    std::vector<size_t> selected;
    selected.reserve(regions.size());
    for (size_t position = 0; position < order.size(); ++position) {
        const size_t current = order[position];
        if (suppressed[current]) continue;
        selected.push_back(current);
        for (size_t nextPosition = position + 1; nextPosition < order.size(); ++nextPosition) {
            const size_t other = order[nextPosition];
            if (suppressed[other]) continue;
            const double overlap = PaddleDocInclusiveIou(
                regions[current].bbox.left, regions[current].bbox.top,
                regions[current].bbox.right, regions[current].bbox.bottom,
                regions[other].bbox.left, regions[other].bbox.top,
                regions[other].bbox.right, regions[other].bbox.bottom);
            const double threshold = regions[current].classId == regions[other].classId
                ? m_profile.nmsSameClass
                : m_profile.nmsCrossClass;
            const bool sameClass = regions[current].classId == regions[other].classId;
            const bool containmentDuplicate = sameClass &&
                RegionSmallOverlap(regions[current], regions[other]) >= 0.7;
            if (overlap < threshold && !containmentDuplicate) continue;
            if (!m_profile.rectMode &&
                !PolygonSmallOverlapPasses(regions[current], regions[other])) {
                // V3 degraded mode must not guess from rectangles alone.
                continue;
            }
            suppressed[other] = true;
        }
    }

    std::vector<LayoutRegion> reconciled;
    reconciled.reserve(selected.size());
    for (size_t index : selected) reconciled.push_back(std::move(regions[index]));
    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutTileReconciliation(
        regions.size(), reconciled.size()).c_str());
    regions = std::move(reconciled);
}

std::vector<LayoutRegion> LayoutEngine::FuseFullAndTileRegions(
    const std::vector<LayoutRegion>& fullRegions,
    const std::vector<LayoutRegion>& tileRegions,
    int pageWidth,
    int pageHeight)
{
    std::vector<LayoutRegion> fused = fullRegions;
    size_t accepted = 0;
    for (const auto& candidate : tileRegions) {
        // A full-image prediction is the authority for normal page content.
        // A tile candidate touching an internal seam is only a partial view,
        // never enough evidence to create a new recognition region.
        if (IsInternalTileFragment(candidate, pageWidth, pageHeight)) continue;

        bool duplicatesFull = false;
        for (const auto& full : fullRegions) {
            const bool sameClass = candidate.classId == full.classId;
            const double iou = PaddleDocInclusiveIou(
                candidate.bbox.left, candidate.bbox.top,
                candidate.bbox.right, candidate.bbox.bottom,
                full.bbox.left, full.bbox.top,
                full.bbox.right, full.bbox.bottom);
            const double threshold = sameClass
                ? m_profile.nmsSameClass : m_profile.nmsCrossClass;
            const bool containment = sameClass &&
                RegionSmallOverlap(candidate, full) >= 0.7;
            if (iou < threshold && !containment) continue;
            if (!m_profile.rectMode &&
                !PolygonSmallOverlapPasses(candidate, full)) {
                continue;
            }
            duplicatesFull = true;
            break;
        }
        if (duplicatesFull) continue;
        fused.push_back(candidate);
        accepted++;
    }

    SortRegions(fused, true);
    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutTileFusion(
        fullRegions.size(), tileRegions.size(), accepted, fused.size()).c_str());
    return fused;
}

void LayoutEngine::PostprocessRegions(std::vector<LayoutRegion>& regions, bool sortByPosition) {
    // === Post-processing: dedup and merge ===

    // Helper: compute containment ratio (how much of B is inside A)
    auto containmentRatio = [](const RECT& a, const RECT& b) -> float {
        LONG il = (std::max)(a.left, b.left);
        LONG it = (std::max)(a.top, b.top);
        LONG ir = (std::min)(a.right, b.right);
        LONG ib = (std::min)(a.bottom, b.bottom);
        if (il >= ir || it >= ib) return 0.0f;
        LONG interArea = (ir - il) * (ib - it);
        LONG areaB = (b.right - b.left) * (b.bottom - b.top);
        return areaB > 0 ? (float)interArea / (float)areaB : 0.0f;
    };

    // Helper: IoU
    auto iou = [](const RECT& a, const RECT& b) -> float {
        LONG il = (std::max)(a.left, b.left);
        LONG it = (std::max)(a.top, b.top);
        LONG ir = (std::min)(a.right, b.right);
        LONG ib = (std::min)(a.bottom, b.bottom);
        if (il >= ir || it >= ib) return 0.0f;
        LONG interArea = (ir - il) * (ib - it);
        LONG areaA = (a.right - a.left) * (a.bottom - a.top);
        LONG areaB = (b.right - b.left) * (b.bottom - b.top);
        LONG unionArea = areaA + areaB - interArea;
        return unionArea > 0 ? (float)interArea / (float)unionArea : 0.0f;
    };

    // Step 1: containment removal (B >60% inside A -> remove B).
    // This intentionally applies across classes. It prevents text/table parent
    // blocks and their internal formula/cell fragments from being recognized
    // twice, which is more stable for screenshot OCR than preserving every
    // possible child box.
    {
        std::vector<bool> suppressed(regions.size(), false);
        for (size_t i = 0; i < regions.size(); i++) {
            if (suppressed[i]) continue;
            for (size_t j = 0; j < regions.size(); j++) {
                if (i == j || suppressed[j]) continue;
                float c = containmentRatio(regions[i].bbox, regions[j].bbox);
                if (c > 0.6f) {
                    suppressed[j] = true;
                }
            }
        }
        std::vector<LayoutRegion> filtered;
        for (size_t i = 0; i < regions.size(); i++) {
            if (!suppressed[i]) filtered.push_back(regions[i]);
        }
        regions.swap(filtered);
    }

    // Step 2: IoU NMS (threshold 0.4, keep higher confidence).
    {
        std::vector<bool> suppressed(regions.size(), false);
        for (size_t i = 0; i < regions.size(); i++) {
            if (suppressed[i]) continue;
            for (size_t j = i + 1; j < regions.size(); j++) {
                if (suppressed[j]) continue;
                if (iou(regions[i].bbox, regions[j].bbox) > 0.4f) {
                    if (regions[i].confidence >= regions[j].confidence) {
                        suppressed[j] = true;
                    } else {
                        suppressed[i] = true;
                        break;
                    }
                }
            }
        }
        std::vector<LayoutRegion> filtered;
        for (size_t i = 0; i < regions.size(); i++) {
            if (!suppressed[i]) filtered.push_back(regions[i]);
        }
        regions.swap(filtered);
    }

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutAfterDedup(regions.size()).c_str());

    // Step 4: Sort by reading order
    if (!regions.empty()) {
        if (sortByPosition) {
            std::vector<int> activeIndices(regions.size());
            for (size_t i = 0; i < regions.size(); ++i) {
                activeIndices[i] = (int)i;
            }
            std::vector<int> sortedIndices;
            sortedIndices.reserve(regions.size());
            RecursiveXYCut(regions, activeIndices, sortedIndices);

            std::vector<LayoutRegion> sortedRegions;
            sortedRegions.reserve(regions.size());
            for (int idx : sortedIndices) {
                sortedRegions.push_back(regions[idx]);
            }
            regions.swap(sortedRegions);
        } else {
            std::sort(regions.begin(), regions.end(), [](const LayoutRegion& a, const LayoutRegion& b) {
                return a.readingOrder < b.readingOrder;
            });
        }
    }

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutDetectDone(regions.size()).c_str());
}

bool LayoutEngine::ShouldRunTiled(int origW, int origH, size_t fullRegionCount) const {
    if (origW <= 0 || origH <= 0) return false;

    int maxDim = (std::max)(origW, origH);
    int minDim = (std::min)(origW, origH);
    float aspect = minDim > 0 ? (float)maxDim / (float)minDim : 1.0f;
    float minScale = (std::min)(800.0f / (float)origW, 800.0f / (float)origH);

    // PP-DocLayoutV3 is trained for a full direct 800x800 page input. A
    // normal high-DPI A4/Letter page must not switch algorithms merely because
    // one edge crosses an arbitrary pixel count. Tiles are a long-document
    // fallback only: use them for extreme strips, or when a truly severe
    // full-page downscale produced an implausibly sparse result.
    bool extremeAspect = aspect > 3.0f;
    bool severeDownscale = minScale < 0.20f;
    bool sparseFull = fullRegionCount <= 2;
    return extremeAspect || (severeDownscale && sparseFull);
}

std::vector<LayoutRegion> LayoutEngine::DetectTiled(
    HBITMAP hBitmap,
    int origW,
    int origH,
    LayoutDetectionStageCounts* stageCounts)
{
    if (stageCounts) *stageCounts = {};
    std::vector<LayoutRegion> regions;
    if (!hBitmap || origW <= 0 || origH <= 0) return regions;

    const int TILE = 1600;
    const int OVERLAP = 200;
    const int STEP = TILE - OVERLAP;

    int tileIndex = 0;
    for (int y = 0;;) {
        int tileH = (std::min)(TILE, origH);
        int top = y;
        if (top + tileH > origH) top = origH - tileH;

        for (int x = 0;;) {
            int tileW = (std::min)(TILE, origW);
            int left = x;
            if (left + tileW > origW) left = origW - tileW;

            RECT rect = { left, top, left + tileW, top + tileH };
            HBITMAP tile = CropBitmap(hBitmap, rect);
            if (tile) {
                LayoutDetectionStageCounts tileCounts;
                auto tileRegions = DetectSingle(
                    tile, left, top, tileIndex * 1000, false, &tileCounts);
                for (auto& region : tileRegions) {
                    region.fromTile = true;
                    region.sourceTile = rect;
                }
                if (stageCounts) {
                    stageCounts->raw += tileCounts.raw;
                    stageCounts->scorePassed += tileCounts.scorePassed;
                    stageCounts->nmsKept += tileCounts.nmsKept;
                    stageCounts->imageAreaKept += tileCounts.imageAreaKept;
                    stageCounts->classModeKept += tileCounts.classModeKept;
                    stageCounts->polygonFallbacks += tileCounts.polygonFallbacks;
                    stageCounts->overlapKept += tileCounts.overlapKept;
                    stageCounts->finalCount += tileCounts.finalCount;
                    stageCounts->exactScoreTies += tileCounts.exactScoreTies;
                    stageCounts->polygonDegraded |= tileCounts.polygonDegraded;
                    if (stageCounts->error.empty() && !tileCounts.error.empty()) {
                        stageCounts->error = tileCounts.error;
                    }
                }
                regions.insert(regions.end(), tileRegions.begin(), tileRegions.end());
                DeleteObject(tile);
            }
            tileIndex++;

            if (left + tileW >= origW) break;
            x = left + STEP;
        }

        if (top + tileH >= origH) break;
        y = top + STEP;
    }

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutTiledRaw(regions.size(), tileIndex).c_str());
    return regions;
}

std::vector<LayoutRegion> LayoutEngine::Detect(
    HBITMAP hBitmap,
    LayoutDetectionDiagnostics* diagnostics)
{
    LayoutDetectionDiagnostics localDiagnostics;
    LayoutDetectionDiagnostics& detection = diagnostics
        ? *diagnostics : localDiagnostics;
    detection = {};
    detection.family = m_modelFamily;
    detection.modelPath = m_modelPath;
    detection.modelBytes = m_modelBytes;
    detection.modelSha256 = m_modelSha256;
    detection.modelSha256Error = m_modelSha256Error;
    std::vector<LayoutRegion> empty;
    if (!m_loaded || !m_env || !m_session || !m_allocator) return empty;
    if (!hBitmap) return empty;

    BITMAP bm = {};
    if (!GetObject(hBitmap, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        return empty;
    }

    OcrSettings settings = LoadOcrSettings();
    const LayoutModelFamily currentFamily = ResolveLayoutModelFamily(
        settings.layoutModelFamily, m_modelPath);
    if (currentFamily != m_modelFamily) {
        OutputDebugStringA(
            "[LayoutEngine] WARNING: layout family setting changed; cached session family remains active until reload\n");
    }
    m_profile = BuildPaddleDocLayoutProfile(
        m_modelFamily,
        ParseLayoutThresholdProfile(settings.layoutThresholdProfile));

    const std::wstring wdbg = WideFormatLayoutEngineDebug(
        LayoutModelFamilyName(m_modelFamily),
        LayoutThresholdProfileName(m_profile.thresholdProfile),
        PaddleDocClassThreshold(m_profile, 22),
        PaddleDocClassThreshold(m_profile, 21));
    OutputDebugStringW(wdbg.c_str());

    auto fullRegions = DetectSingle(
        hBitmap, 0, 0, 0, true, &detection.full);
    if (!detection.full.error.empty()) {
        detection.error = detection.full.error;
        return empty;
    }
    if (m_profile.legacyPostprocess) {
        PostprocessRegions(fullRegions, settings.docUsePhysicalSorting);
    } else if (settings.docUsePhysicalSorting) {
        SortRegions(fullRegions, true);
    }

    int maxDim = (std::max)(bm.bmWidth, bm.bmHeight);
    int minDim = (std::min)(bm.bmWidth, bm.bmHeight);
    float aspect = minDim > 0 ? (float)maxDim / (float)minDim : 1.0f;
    float scaleH = 800.0f / (float)bm.bmHeight;
    float scaleW = 800.0f / (float)bm.bmWidth;
    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutFullStats(
        bm.bmWidth, bm.bmHeight, aspect, scaleH, scaleW, fullRegions.size()).c_str());

    if (!ShouldRunTiled(bm.bmWidth, bm.bmHeight, fullRegions.size())) {
        detection.returnedRegions = fullRegions.size();
        return fullRegions;
    }

    OutputDebugStringA("[LayoutEngine] Tiled detection triggered\n");
    detection.tiledTriggered = true;
    auto tileRegions = DetectTiled(
        hBitmap, bm.bmWidth, bm.bmHeight, &detection.tiled);
    if (!detection.tiled.error.empty()) {
        OutputDebugStringA(
            "[LayoutEngine] Tiled inference failed; keeping valid full-image result\n");
        detection.returnedRegions = fullRegions.size();
        return fullRegions;
    }
    if (m_profile.legacyPostprocess) {
        PostprocessRegions(tileRegions, true);
    } else {
        ReconcileTileRegions(tileRegions, bm.bmWidth, bm.bmHeight);
        SortRegions(tileRegions, true);
    }

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatLayoutTiledStats(
        fullRegions.size(), tileRegions.size()).c_str());

    auto fusedRegions = FuseFullAndTileRegions(
        fullRegions, tileRegions, bm.bmWidth, bm.bmHeight);
    if (fusedRegions.size() > fullRegions.size()) {
        OutputDebugStringA("[LayoutEngine] Using full+tile fused layout result\n");
        detection.usedTiled = true;
        detection.returnedRegions = fusedRegions.size();
        return fusedRegions;
    }

    OutputDebugStringA("[LayoutEngine] Keeping full-image layout result\n");
    detection.returnedRegions = fullRegions.size();
    return fullRegions;
}

