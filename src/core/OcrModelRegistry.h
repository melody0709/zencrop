#pragma once

#include "Settings.h"
#include "WideStringUtils.h"

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

// Stage 4-B production owner for local OCR model locations.  It only resolves
// names and reads file metadata; it never opens a model, starts a server or
// contacts a network endpoint.

enum class OcrModelFamily {
    Unknown = 0,
    PpOcrV6,
    PaddleOcrVl15,
    PaddleOcrVl16,
    Shared,
    OnnxRuntime,
};

struct OcrModelRegistryEntry {
    OcrModelFamily family = OcrModelFamily::Unknown;
    const wchar_t* relativeDir = L"";
    const wchar_t* id = L"";
    bool requiredForDefaultLocal = false;
};

inline const std::vector<OcrModelRegistryEntry>& OcrModelRegistryCatalog()
{
    static const std::vector<OcrModelRegistryEntry> kCatalog = {
        { OcrModelFamily::PpOcrV6, L"pp-ocrv6", L"pp-ocrv6", true },
        { OcrModelFamily::PaddleOcrVl16, L"paddleocr-vl-1.6", L"paddleocr-vl-1.6", false },
        { OcrModelFamily::Shared, L"shared", L"shared", false },
        { OcrModelFamily::OnnxRuntime, L"onnxruntime", L"onnxruntime", true },
    };
    return kCatalog;
}

inline const OcrModelRegistryEntry* OcrModelRegistryFindById(const std::wstring& id)
{
    for (const auto& entry : OcrModelRegistryCatalog()) {
        if (id == entry.id) return &entry;
    }
    return nullptr;
}

inline std::wstring OcrModelRegistryTrimPath(std::wstring path)
{
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

inline std::wstring OcrModelRegistryNormalizePPOcrV6Variant(std::wstring variant)
{
    variant = WideToLower(std::move(variant));
    return variant == L"medium" ? L"medium" : L"small";
}

inline std::wstring OcrModelRegistryProcessDir()
{
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) return L"";
    return WideExeDirFromModulePath(modulePath);
}

struct OcrModelRegistryPPOcrV6Paths {
    std::wstring modelDir;
    std::wstring variant;
    std::wstring detModelPath;
    std::wstring recModelPath;
    std::wstring dictPath;
};

struct OcrModelRegistryPlan {
    std::wstring paddleVlRoot;
    std::wstring docLayoutRoot;
    std::wstring explicitDocLayoutModelPath;
    OcrModelRegistryPPOcrV6Paths ppocrv6;
};

// `processDir` is explicit for deterministic dry-run contracts. Production
// callers pass OcrModelRegistryProcessDir(); tests pass a synthetic directory.
inline OcrModelRegistryPlan OcrModelRegistryBuildPlan(
    const OcrSettings& settings,
    const std::wstring& processDir)
{
    OcrModelRegistryPlan plan;
    plan.paddleVlRoot = settings.paddleLocalModelDir.empty()
        ? processDir
        : OcrModelRegistryTrimPath(settings.paddleLocalModelDir);
    plan.docLayoutRoot = settings.paddleLocalModelDir.empty()
        ? WideJoinPath(processDir, L"ocr")
        : OcrModelRegistryTrimPath(settings.paddleLocalModelDir);
    plan.explicitDocLayoutModelPath = settings.docLayoutModelPath;

    plan.ppocrv6.modelDir = OcrModelRegistryTrimPath(settings.ppocrv6ModelDir);
    plan.ppocrv6.variant = OcrModelRegistryNormalizePPOcrV6Variant(settings.ppocrv6Variant);
    const std::wstring variantDir = WideJoinPath(plan.ppocrv6.modelDir, plan.ppocrv6.variant);
    plan.ppocrv6.detModelPath = WideJoinPath(WideJoinPath(variantDir, L"det"), L"inference.onnx");
    plan.ppocrv6.recModelPath = WideJoinPath(WideJoinPath(variantDir, L"rec"), L"inference.onnx");
    plan.ppocrv6.dictPath = WideJoinPath(
        WideJoinPath(variantDir, L"rec"), L"ppocrv6_rec_dict.txt");
    return plan;
}

inline bool OcrModelRegistryPathExists(const std::wstring& path)
{
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline bool OcrModelRegistryFindPaddleVlServerInDirectory(
    const std::wstring& directory,
    std::wstring& exePath)
{
    static const wchar_t* kCandidates[] = {
        L"llama-server.exe",
        L"llama-server-cuda.exe",
        L"llama-server-vulkan.exe",
        L"server.exe",
    };
    for (const wchar_t* name : kCandidates) {
        const std::wstring path = WideJoinPath(directory, name);
        if (OcrModelRegistryPathExists(path)) {
            exePath = path;
            return true;
        }
    }
    return false;
}

inline std::vector<std::wstring> OcrModelRegistryChildDirectories(
    const std::wstring& directory)
{
    std::vector<std::wstring> children;
    WIN32_FIND_DATAW findData = {};
    const std::wstring pattern = WideJoinPath(directory, L"*");
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) return children;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            findData.cFileName[0] == L'.') {
            continue;
        }
        children.push_back(WideJoinPath(directory, findData.cFileName));
    } while (FindNextFileW(find, &findData));
    FindClose(find);
    return children;
}

inline bool OcrModelRegistryFindPaddleVlServer(
    const OcrModelRegistryPlan& plan,
    std::wstring& exePath)
{
    exePath.clear();
    if (OcrModelRegistryFindPaddleVlServerInDirectory(plan.paddleVlRoot, exePath)) return true;

    for (const std::wstring& child : OcrModelRegistryChildDirectories(plan.paddleVlRoot)) {
        if (OcrModelRegistryFindPaddleVlServerInDirectory(child, exePath)) return true;
        for (const std::wstring& grandchild : OcrModelRegistryChildDirectories(child)) {
            if (OcrModelRegistryFindPaddleVlServerInDirectory(grandchild, exePath)) {
                return true;
            }
        }
    }
    return false;
}

inline bool OcrModelRegistryFindPaddleVlModelPairInDirectory(
    const std::wstring& directory,
    std::wstring& modelPath,
    std::wstring& mmprojPath)
{
    WIN32_FIND_DATAW findData = {};
    const std::wstring pattern = WideJoinPath(directory, L"*.gguf");
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) return false;

    std::wstring foundModel;
    std::wstring foundMmproj;
    do {
        const std::wstring filename = findData.cFileName;
        const std::wstring path = WideJoinPath(directory, filename);
        if (filename.find(L"mmproj") != std::wstring::npos) {
            foundMmproj = path;
        } else {
            foundModel = path;
        }
    } while (FindNextFileW(find, &findData));
    FindClose(find);

    if (foundModel.empty() || foundMmproj.empty()) return false;
    modelPath = std::move(foundModel);
    mmprojPath = std::move(foundMmproj);
    return true;
}

inline bool OcrModelRegistryFindPaddleVlModelPair(
    const OcrModelRegistryPlan& plan,
    std::wstring& modelPath,
    std::wstring& mmprojPath)
{
    modelPath.clear();
    mmprojPath.clear();
    if (OcrModelRegistryFindPaddleVlModelPairInDirectory(
            plan.paddleVlRoot, modelPath, mmprojPath)) {
        return true;
    }

    for (const std::wstring& child : OcrModelRegistryChildDirectories(plan.paddleVlRoot)) {
        if (OcrModelRegistryFindPaddleVlModelPairInDirectory(
                child, modelPath, mmprojPath)) {
            return true;
        }
        for (const std::wstring& grandchild : OcrModelRegistryChildDirectories(child)) {
            if (OcrModelRegistryFindPaddleVlModelPairInDirectory(
                    grandchild, modelPath, mmprojPath)) {
                return true;
            }
        }
    }
    return false;
}

inline bool OcrModelRegistryFindDocLayoutModelInDirectory(
    const std::wstring& directory,
    std::wstring& modelPath)
{
    static const wchar_t* kCandidates[] = {
        L"PP-DocLayoutV3.onnx",
        L"doclayout_yolo.onnx",
    };
    for (const wchar_t* name : kCandidates) {
        const std::wstring path = WideJoinPath(directory, name);
        if (OcrModelRegistryPathExists(path)) {
            modelPath = path;
            return true;
        }
    }
    return false;
}

inline bool OcrModelRegistryFindDocLayoutModel(
    const OcrModelRegistryPlan& plan,
    std::wstring& modelPath)
{
    modelPath.clear();
    if (OcrModelRegistryPathExists(plan.explicitDocLayoutModelPath)) {
        modelPath = plan.explicitDocLayoutModelPath;
        return true;
    }
    if (OcrModelRegistryFindDocLayoutModelInDirectory(plan.docLayoutRoot, modelPath)) return true;

    WIN32_FIND_DATAW findData = {};
    const std::wstring pattern = WideJoinPath(plan.docLayoutRoot, L"*");
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            findData.cFileName[0] == L'.') {
            continue;
        }
        if (OcrModelRegistryFindDocLayoutModelInDirectory(
                WideJoinPath(plan.docLayoutRoot, findData.cFileName), modelPath)) {
            FindClose(find);
            return true;
        }
    } while (FindNextFileW(find, &findData));
    FindClose(find);
    return false;
}

inline bool OcrModelRegistryPPOcrV6Ready(const OcrModelRegistryPPOcrV6Paths& paths)
{
    return OcrModelRegistryPathExists(paths.modelDir) &&
        OcrModelRegistryPathExists(paths.detModelPath) &&
        OcrModelRegistryPathExists(paths.recModelPath) &&
        OcrModelRegistryPathExists(paths.dictPath);
}

struct OcrModelRegistryDryRunResult {
    OcrModelRegistryPlan plan;
    std::wstring paddleVlServerPath;
    std::wstring paddleVlModelPath;
    std::wstring paddleVlMmprojPath;
    std::wstring docLayoutModelPath;
    bool paddleVlServerPresent = false;
    bool paddleVlModelPairPresent = false;
    bool docLayoutModelPresent = false;
    bool ppocrv6Ready = false;
};

inline OcrModelRegistryDryRunResult OcrModelRegistryDryRun(const OcrModelRegistryPlan& plan)
{
    OcrModelRegistryDryRunResult result;
    result.plan = plan;
    result.paddleVlServerPresent = OcrModelRegistryFindPaddleVlServer(
        plan, result.paddleVlServerPath);
    result.paddleVlModelPairPresent = OcrModelRegistryFindPaddleVlModelPair(
        plan, result.paddleVlModelPath, result.paddleVlMmprojPath);
    result.docLayoutModelPresent = OcrModelRegistryFindDocLayoutModel(
        plan, result.docLayoutModelPath);
    result.ppocrv6Ready = OcrModelRegistryPPOcrV6Ready(plan.ppocrv6);
    return result;
}

inline std::wstring OcrModelRegistryDryRunJson(const OcrModelRegistryDryRunResult& result)
{
    const auto jsonString = [](const std::wstring& value) {
        return L"\"" + WideEscapeJsonString(value) + L"\"";
    };
    const auto jsonBool = [](bool value) -> std::wstring {
        return value ? L"true" : L"false";
    };
    return L"{\n"
        L"  \"mode\": \"model-dry-run\",\n"
        L"  \"paddleVlRoot\": " + jsonString(result.plan.paddleVlRoot) + L",\n"
        L"  \"docLayoutRoot\": " + jsonString(result.plan.docLayoutRoot) + L",\n"
        L"  \"paddleVlServer\": {\"present\": " + jsonBool(result.paddleVlServerPresent) +
            L", \"path\": " + jsonString(result.paddleVlServerPath) + L"},\n"
        L"  \"paddleVlModelPair\": {\"present\": " + jsonBool(result.paddleVlModelPairPresent) +
            L", \"modelPath\": " + jsonString(result.paddleVlModelPath) +
            L", \"mmprojPath\": " + jsonString(result.paddleVlMmprojPath) + L"},\n"
        L"  \"docLayoutModel\": {\"present\": " + jsonBool(result.docLayoutModelPresent) +
            L", \"path\": " + jsonString(result.docLayoutModelPath) + L"},\n"
        L"  \"ppocrv6\": {\"ready\": " + jsonBool(result.ppocrv6Ready) +
            L", \"modelDir\": " + jsonString(result.plan.ppocrv6.modelDir) +
            L", \"detModelPath\": " + jsonString(result.plan.ppocrv6.detModelPath) +
            L", \"recModelPath\": " + jsonString(result.plan.ppocrv6.recModelPath) +
            L", \"dictPath\": " + jsonString(result.plan.ppocrv6.dictPath) + L"}\n"
        L"}\n";
}
