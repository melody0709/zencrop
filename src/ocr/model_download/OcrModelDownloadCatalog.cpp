#include "OcrModelDownloadCatalog.h"

#include "core/Sha256.h"
#include "core/WideStringUtils.h"

#include <cwctype>
#include <set>

namespace {

OcrModelArtifactSpec File(
    const wchar_t* id,
    const wchar_t* url,
    const wchar_t* relativePath,
    std::uint64_t bytes,
    const wchar_t* sha256,
    OcrModelArtifactKind kind = OcrModelArtifactKind::File,
    const wchar_t* mirrorUrl = nullptr)
{
    OcrModelArtifactSpec artifact;
    artifact.id = id;
    artifact.urls.push_back(url);
    if (mirrorUrl) artifact.urls.push_back(mirrorUrl);
    artifact.relativeInstallPath = relativePath;
    artifact.expectedBytes = bytes;
    artifact.sha256 = sha256;
    artifact.kind = kind;
    return artifact;
}

bool IsSafeRelativePath(const std::wstring& path)
{
    if (path.empty() || path.front() == L'\\' || path.front() == L'/' ||
        path.find(L':') != std::wstring::npos) {
        return false;
    }
    std::wstring segment;
    for (size_t i = 0; i <= path.size(); ++i) {
        const wchar_t ch = i < path.size() ? path[i] : L'\\';
        if (ch == L'\\' || ch == L'/') {
            if (segment.empty() || segment == L"." || segment == L"..") return false;
            segment.clear();
        } else {
            segment.push_back(ch);
        }
    }
    return true;
}

bool IsImmutableHttpsUrl(const std::wstring& url)
{
    if (url.rfind(L"https://", 0) != 0) return false;
    std::wstring lower;
    lower.reserve(url.size());
    for (wchar_t ch : url) lower.push_back(static_cast<wchar_t>(std::towlower(ch)));

    const std::wstring hfMarker = L"huggingface.co/";
    if (lower.find(hfMarker) != std::wstring::npos) {
        const std::wstring resolveMarker = L"/resolve/";
        const size_t revisionStart = lower.find(resolveMarker);
        if (revisionStart == std::wstring::npos) return false;
        const size_t valueStart = revisionStart + resolveMarker.size();
        const size_t valueEnd = lower.find(L'/', valueStart);
        if (valueEnd == std::wstring::npos || valueEnd - valueStart != 40) return false;
        for (size_t i = valueStart; i < valueEnd; ++i) {
            if (!std::iswxdigit(lower[i])) return false;
        }
        return true;
    }

    const std::wstring githubMarker = L"github.com/";
    const std::wstring releaseMarker = L"/releases/download/";
    if (lower.find(githubMarker) != std::wstring::npos) {
        const size_t tagStart = lower.find(releaseMarker);
        if (tagStart == std::wstring::npos) return false;
        const size_t valueStart = tagStart + releaseMarker.size();
        const size_t valueEnd = lower.find(L'/', valueStart);
        return valueEnd != std::wstring::npos && valueEnd > valueStart &&
            lower.substr(valueStart, valueEnd - valueStart) != L"latest";
    }

    // ModelScope: modelscope.cn/api/v1/models/.../repo?Revision={40-hex}&FilePath=...
    const std::wstring modelscopeMarker = L"modelscope.cn/api/v1/models/";
    if (lower.find(modelscopeMarker) != std::wstring::npos) {
        const std::wstring revisionParam = L"revision=";
        const size_t revStart = lower.find(revisionParam);
        if (revStart == std::wstring::npos) return false;
        const size_t valueStart = revStart + revisionParam.size();
        const size_t valueEnd = lower.find(L'&', valueStart);
        const size_t revLen = (valueEnd == std::wstring::npos)
            ? lower.size() - valueStart
            : valueEnd - valueStart;
        if (revLen != 40) return false;
        for (size_t i = valueStart; i < valueStart + 40; ++i) {
            if (!std::iswxdigit(lower[i])) return false;
        }
        return true;
    }
    return false;
}

std::wstring InstallPathKey(const std::wstring& path)
{
    std::wstring key;
    key.reserve(path.size());
    for (wchar_t ch : path) {
        key.push_back(ch == L'/'
            ? L'\\'
            : static_cast<wchar_t>(std::towlower(ch)));
    }
    return key;
}

bool InstallPathConflicts(
    const std::vector<std::wstring>& existingPaths,
    const std::wstring& candidate)
{
    for (const std::wstring& existing : existingPaths) {
        if (candidate == existing ||
            candidate.rfind(existing + L"\\", 0) == 0 ||
            existing.rfind(candidate + L"\\", 0) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

const std::vector<OcrModelBundleSpec>& OcrModelDownloadCatalog()
{
    static const std::vector<OcrModelBundleSpec> kCatalog = {
        {
            OcrModelBundleId::PpOcrV6Small,
            L"pp_ocrv6_small",
            L"PP-OCRv6 Small",
            L"Fast CPU OCR detector and recognizer",
            L"Apache-2.0",
            {
                File(
                    L"ppocrv6-small-det",
                    L"https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx/resolve/28fe5895c24fd108c19eb3e8479f4ab385fbfc62/inference.onnx",
                    L"pp-ocrv6\\small\\det\\inference.onnx",
                    9880512ULL,
                    L"d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e",
                    OcrModelArtifactKind::File,
                    L"https://modelscope.cn/api/v1/models/PaddlePaddle/PP-OCRv6_small_det_onnx/repo?Revision=37b02eded8dbca659f8ee5d51f822ea1ebd9bcba&FilePath=inference.onnx"),
                File(
                    L"ppocrv6-small-rec",
                    L"https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/b8f84f0b80c529de40b4fbb3544b84fa7233a513/inference.onnx",
                    L"pp-ocrv6\\small\\rec\\inference.onnx",
                    21159378ULL,
                    L"5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634",
                    OcrModelArtifactKind::File,
                    L"https://modelscope.cn/api/v1/models/PaddlePaddle/PP-OCRv6_small_rec_onnx/repo?Revision=ba215b1cc49d9ed4459d161b96778e8643fe0c1f&FilePath=inference.onnx"),
            },
        },
        {
            OcrModelBundleId::PpOcrV6Medium,
            L"pp_ocrv6_medium",
            L"PP-OCRv6 Medium",
            L"Higher accuracy CPU OCR detector and recognizer",
            L"Apache-2.0",
            {
                File(
                    L"ppocrv6-medium-det",
                    L"https://huggingface.co/PaddlePaddle/PP-OCRv6_medium_det_onnx/resolve/61323801669c338b7891481ec7bac61ce31b576a/inference.onnx",
                    L"pp-ocrv6\\medium\\det\\inference.onnx",
                    62032837ULL,
                    L"eb13b44b25bb36f89528b68720af8a61d9cf381176107f465db1757b65d086e1",
                    OcrModelArtifactKind::File,
                    L"https://modelscope.cn/api/v1/models/PaddlePaddle/PP-OCRv6_medium_det_onnx/repo?Revision=8cb026ecab7a28b7f7e479dccd7f93ebe3ff47c1&FilePath=inference.onnx"),
                File(
                    L"ppocrv6-medium-rec",
                    L"https://huggingface.co/PaddlePaddle/PP-OCRv6_medium_rec_onnx/resolve/50c7eacafc52fa7bcf4194e8cd08e46f8558504b/inference.onnx",
                    L"pp-ocrv6\\medium\\rec\\inference.onnx",
                    76554979ULL,
                    L"9c09abf0957f7968c7586464b7397b84ad2387a0497a351af40e9acc71b673ba",
                    OcrModelArtifactKind::File,
                    L"https://modelscope.cn/api/v1/models/PaddlePaddle/PP-OCRv6_medium_rec_onnx/repo?Revision=4b0e4965d5d68d048ec6ff9e1fdaaaaed0b7abd5&FilePath=inference.onnx"),
            },
        },
        {
            OcrModelBundleId::PaddleOcrVl16,
            L"paddle_vl_16",
            L"PaddleOCR-VL 1.6",
            L"Vision-language OCR model and pinned llama.cpp runtime",
            L"Apache-2.0 models; MIT llama.cpp",
            {
                File(
                    L"paddleocr-vl16-model",
                    L"https://huggingface.co/PaddlePaddle/PaddleOCR-VL-1.6-GGUF/resolve/511b09642bb324401f15f97cc23bc67e8f0a291d/PaddleOCR-VL-1.6-GGUF.gguf",
                    L"paddleocr-vl-1.6\\model\\PaddleOCR-VL-1.6-GGUF.gguf",
                    935769056ULL,
                    L"f3ae46ec885050acf4b3d31944431e1fd90d50664fb09126af4a3c050ba14ee8",
                    OcrModelArtifactKind::File,
                    L"https://modelscope.cn/api/v1/models/PaddlePaddle/PaddleOCR-VL-1.6-GGUF/repo?Revision=dea493a839bfee633c4014ddad016ab7f0336b6f&FilePath=PaddleOCR-VL-1.6-GGUF.gguf"),
                File(
                    L"paddleocr-vl16-mmproj",
                    L"https://huggingface.co/PaddlePaddle/PaddleOCR-VL-1.6-GGUF/resolve/511b09642bb324401f15f97cc23bc67e8f0a291d/PaddleOCR-VL-1.6-GGUF-mmproj.gguf",
                    L"paddleocr-vl-1.6\\model\\PaddleOCR-VL-1.6-GGUF-mmproj.gguf",
                    881770560ULL,
                    L"204d757d7610d9b3faab10d506d69e5b244e32bf765e2bab2d0167e65e0a058a",
                    OcrModelArtifactKind::File,
                    L"https://modelscope.cn/api/v1/models/PaddlePaddle/PaddleOCR-VL-1.6-GGUF/repo?Revision=dea493a839bfee633c4014ddad016ab7f0336b6f&FilePath=PaddleOCR-VL-1.6-GGUF-mmproj.gguf"),
                File(
                    L"llama-b9128-cpu-x64",
                    L"https://github.com/ggml-org/llama.cpp/releases/download/b9128/llama-b9128-bin-win-cpu-x64.zip",
                    L"paddleocr-vl-1.6\\llama",
                    16061464ULL,
                    L"75bf3dbeb83733b413c18216ad21e51afe4bd6ff8d3d516137f0b48353dccca5",
                    OcrModelArtifactKind::LlamaRuntimeZip),
            },
        },
        {
            OcrModelBundleId::DocLayout,
            L"doc_layout",
            L"PP-DocLayoutV3",
            L"Document layout analysis model",
            L"Apache-2.0",
            {
                File(
                    L"pp-doclayout-v3",
                    L"https://huggingface.co/PaddlePaddle/PP-DocLayoutV3_onnx/resolve/46bbdf188bb0a772c08aed74882ce7e51a8f1ea6/inference.onnx",
                    L"shared\\PP-DocLayoutV3.onnx",
                    130502049ULL,
                    L"45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba",
                    OcrModelArtifactKind::File,
                    L"https://modelscope.cn/api/v1/models/PaddlePaddle/PP-DocLayoutV3_onnx/repo?Revision=afe9e948f9492ae88399f7b6f734a784fc1a80c6&FilePath=inference.onnx"),
            },
        },
    };
    return kCatalog;
}

const OcrModelBundleSpec* OcrModelDownloadFindBundle(OcrModelBundleId id)
{
    for (const auto& bundle : OcrModelDownloadCatalog()) {
        if (bundle.id == id) return &bundle;
    }
    return nullptr;
}

const OcrModelBundleSpec* OcrModelDownloadFindBundle(const std::wstring& stableId)
{
    for (const auto& bundle : OcrModelDownloadCatalog()) {
        if (bundle.stableId == stableId) return &bundle;
    }
    return nullptr;
}

std::uint64_t OcrModelDownloadBundleBytes(const OcrModelBundleSpec& bundle)
{
    std::uint64_t total = 0;
    for (const auto& artifact : bundle.artifacts) total += artifact.expectedBytes;
    return total;
}

bool OcrModelDownloadValidateCatalog(std::wstring& error)
{
    error.clear();
    std::set<std::wstring> bundleIds;
    std::set<std::wstring> artifactIds;
    std::vector<std::wstring> installPaths;
    for (const auto& bundle : OcrModelDownloadCatalog()) {
        if (bundle.stableId.empty() || !bundleIds.insert(bundle.stableId).second) {
            error = L"Model download catalog contains a missing or duplicate bundle id.";
            return false;
        }
        if (bundle.artifacts.empty()) {
            error = L"Model download bundle has no artifacts: " + bundle.stableId;
            return false;
        }
        for (const auto& artifact : bundle.artifacts) {
            if (artifact.id.empty() || !artifactIds.insert(artifact.id).second) {
                error = L"Model download catalog contains a missing or duplicate artifact id.";
                return false;
            }
            if (artifact.expectedBytes == 0 || !IsSha256Hex(artifact.sha256)) {
                error = L"Model artifact lacks a fixed size or SHA-256: " + artifact.id;
                return false;
            }
            const std::wstring installPathKey = InstallPathKey(artifact.relativeInstallPath);
            if (!IsSafeRelativePath(artifact.relativeInstallPath) ||
                InstallPathConflicts(installPaths, installPathKey)) {
                error = L"Model artifact has an unsafe or duplicate install path: " + artifact.id;
                return false;
            }
            installPaths.push_back(installPathKey);
            if (artifact.urls.empty()) {
                error = L"Model artifact has no download URL: " + artifact.id;
                return false;
            }
            for (const auto& url : artifact.urls) {
                if (!IsImmutableHttpsUrl(url)) {
                    error = L"Model artifact URL is not immutable HTTPS: " + artifact.id;
                    return false;
                }
            }
        }
    }
    return true;
}

OcrModelInstallResult OcrModelDownloadBuildInstallResult(
    OcrModelBundleId bundle,
    const std::wstring& modelRoot)
{
    OcrModelInstallResult result;
    result.bundle = bundle;
    result.modelRoot = modelRoot;
    switch (bundle) {
    case OcrModelBundleId::PpOcrV6Small:
        result.ppocrv6ModelDir = WideJoinPath(modelRoot, L"pp-ocrv6");
        result.ppocrv6Variant = L"small";
        break;
    case OcrModelBundleId::PpOcrV6Medium:
        result.ppocrv6ModelDir = WideJoinPath(modelRoot, L"pp-ocrv6");
        result.ppocrv6Variant = L"medium";
        break;
    case OcrModelBundleId::PaddleOcrVl16:
        result.paddleLocalModelDir = modelRoot;
        break;
    case OcrModelBundleId::DocLayout:
        result.docLayoutModelPath = WideJoinPath(
            WideJoinPath(modelRoot, L"shared"), L"PP-DocLayoutV3.onnx");
        break;
    }
    return result;
}
