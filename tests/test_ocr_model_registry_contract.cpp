#include "core/OcrModelRegistry.h"
#include "ocr/model_download/OcrModelDownloadCatalog.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static bool CreateEmptyFile(const std::wstring& path) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

static std::wstring CreateRegistryFixture() {
    wchar_t tempDir[MAX_PATH] = {};
    wchar_t uniquePath[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDir) ||
        !GetTempFileNameW(tempDir, L"zcm", 0, uniquePath)) {
        return L"";
    }
    DeleteFileW(uniquePath);
    if (!CreateDirectoryW(uniquePath, nullptr)) return L"";
    return uniquePath;
}

static void TestTwoLevelPaddleVlDiscovery() {
    const std::wstring root = CreateRegistryFixture();
    Expect(!root.empty(), "create registry fixture");
    if (root.empty()) return;

    const std::wstring family = WideJoinPath(root, L"paddleocr-vl-1.6");
    const std::wstring modelDir = WideJoinPath(family, L"model");
    const std::wstring llamaDir = WideJoinPath(family, L"llama");
    Expect(CreateDirectoryW(family.c_str(), nullptr) != FALSE, "create family fixture");
    Expect(CreateDirectoryW(modelDir.c_str(), nullptr) != FALSE, "create model fixture");
    Expect(CreateDirectoryW(llamaDir.c_str(), nullptr) != FALSE, "create llama fixture");

    const std::wstring model = WideJoinPath(modelDir, L"PaddleOCR-VL-1.6-GGUF.gguf");
    const std::wstring mmproj = WideJoinPath(modelDir, L"PaddleOCR-VL-1.6-GGUF-mmproj.gguf");
    const std::wstring server = WideJoinPath(llamaDir, L"llama-server.exe");
    Expect(CreateEmptyFile(model), "create model file");
    Expect(CreateEmptyFile(mmproj), "create mmproj file");
    Expect(CreateEmptyFile(server), "create server file");

    OcrSettings fixtureSettings;
    fixtureSettings.paddleLocalModelDir = root;
    const auto fixturePlan = OcrModelRegistryBuildPlan(fixtureSettings, L"D:\\app");
    const auto fixtureResult = OcrModelRegistryDryRun(fixturePlan);
    Expect(fixtureResult.paddleVlServerPresent, "find server at depth two");
    Expect(fixtureResult.paddleVlModelPairPresent, "find model pair at depth two");
    Expect(fixtureResult.paddleVlServerPath == server, "server path at depth two");
    Expect(fixtureResult.paddleVlModelPath == model, "model path at depth two");
    Expect(fixtureResult.paddleVlMmprojPath == mmproj, "mmproj path at depth two");

    DeleteFileW(server.c_str());
    DeleteFileW(mmproj.c_str());
    DeleteFileW(model.c_str());
    RemoveDirectoryW(llamaDir.c_str());
    RemoveDirectoryW(modelDir.c_str());
    RemoveDirectoryW(family.c_str());
    RemoveDirectoryW(root.c_str());
}

static void TestDownloadCatalog() {
    std::wstring error;
    Expect(OcrModelDownloadValidateCatalog(error), "download catalog valid");
    Expect(error.empty(), "download catalog has no error");
    Expect(OcrModelDownloadCatalog().size() == 4, "download catalog bundle count");

    const auto* vl = OcrModelDownloadFindBundle(OcrModelBundleId::PaddleOcrVl16);
    Expect(vl != nullptr, "find PaddleOCR-VL download bundle");
    Expect(vl && vl->artifacts.size() == 3, "PaddleOCR-VL artifact count");
    Expect(vl && OcrModelDownloadBundleBytes(*vl) == 1833601080ULL,
        "PaddleOCR-VL pinned download bytes");

    const auto smallResult = OcrModelDownloadBuildInstallResult(
        OcrModelBundleId::PpOcrV6Small, L"D:\\models");
    Expect(smallResult.ppocrv6ModelDir == L"D:\\models\\pp-ocrv6",
        "PP-OCRv6 install mapping");
    Expect(smallResult.ppocrv6Variant == L"small", "PP-OCRv6 variant mapping");
    const auto layout = OcrModelDownloadBuildInstallResult(
        OcrModelBundleId::DocLayout, L"D:\\models");
    Expect(layout.docLayoutModelPath == L"D:\\models\\shared\\PP-DocLayoutV3.onnx",
        "DocLayout install mapping");
}

int main() {
    const auto& catalog = OcrModelRegistryCatalog();
    Expect(!catalog.empty(), "catalog non-empty");
    Expect(catalog.size() >= 4, "catalog size");

    const auto* v6 = OcrModelRegistryFindById(L"pp-ocrv6");
    Expect(v6 != nullptr, "find v6");
    Expect(v6 && v6->family == OcrModelFamily::PpOcrV6, "family v6");
    Expect(v6 && v6->requiredForDefaultLocal, "v6 required");

    Expect(OcrModelRegistryFindById(L"missing") == nullptr, "missing");

    OcrSettings settings;
    settings.paddleLocalModelDir = L"D:\\models\\paddle\\";
    settings.docLayoutModelPath = L"D:\\models\\layout.onnx";
    settings.ppocrv6ModelDir = L"D:\\models\\ppocrv6\\";
    settings.ppocrv6Variant = L"MEDIUM";
    const auto plan = OcrModelRegistryBuildPlan(settings, L"D:\\app");
    Expect(plan.paddleVlRoot == L"D:\\models\\paddle", "configured Paddle-VL root");
    Expect(plan.docLayoutRoot == L"D:\\models\\paddle", "configured layout root");
    Expect(plan.explicitDocLayoutModelPath == L"D:\\models\\layout.onnx", "explicit layout path");
    Expect(plan.ppocrv6.variant == L"medium", "variant normalize");
    Expect(plan.ppocrv6.detModelPath == L"D:\\models\\ppocrv6\\medium\\det\\inference.onnx", "det path");
    Expect(plan.ppocrv6.recModelPath == L"D:\\models\\ppocrv6\\medium\\rec\\inference.onnx", "rec path");
    Expect(plan.ppocrv6.dictPath == L"D:\\models\\ppocrv6\\medium\\rec\\ppocrv6_rec_dict.txt", "dict path");

    OcrSettings defaults;
    const auto defaultPlan = OcrModelRegistryBuildPlan(defaults, L"D:\\app");
    Expect(defaultPlan.paddleVlRoot == L"D:\\app", "default Paddle-VL root");
    Expect(defaultPlan.docLayoutRoot == L"D:\\app\\ocr", "default layout root");

    const auto dryRun = OcrModelRegistryDryRun(defaultPlan);
    Expect(!dryRun.paddleVlServerPresent, "dry-run does not require server");
    Expect(!dryRun.paddleVlModelPairPresent, "dry-run does not require model pair");
    Expect(!dryRun.ppocrv6Ready, "dry-run reports unconfigured PP-OCRv6");
    const std::wstring json = OcrModelRegistryDryRunJson(dryRun);
    Expect(json.find(L"\"mode\": \"model-dry-run\"") != std::wstring::npos, "dry-run JSON");

    TestTwoLevelPaddleVlDiscovery();
    TestDownloadCatalog();

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
