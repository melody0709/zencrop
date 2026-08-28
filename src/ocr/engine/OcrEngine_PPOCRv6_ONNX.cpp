#include "OcrEngine_PPOCRv6_ONNX.h"
#include "PPOcrV6BlockAssembler.h"
#include "PPOcrV6RecBatchPlan.h"
#include "core/NarrowStringUtils.h"

#include <windows.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

} // namespace

namespace {

struct PPOcrV6Params {
    HBITMAP hBitmap;
    std::function<void(OcrOutput)> callback;
    PPOcrV6Config config;
};

} // namespace

OcrEnginePPOcrV6Onnx::OcrEnginePPOcrV6Onnx(PPOcrV6Config config)
    : m_config(std::move(config))
{
}

bool OcrEnginePPOcrV6Onnx::IsAvailable() {
    return ValidatePPOcrV6Config(m_config).empty();
}

OcrOutput OcrEnginePPOcrV6Onnx::DoRecognize(HBITMAP hBitmap) {
    OcrOutput result;
    const PPOcrV6Config& cfg = m_config;
    BITMAP bm = {};
    if (!hBitmap || !GetObject(hBitmap, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        result.error = L"Invalid bitmap.";
        return result;
    }

    // OWN-118: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPpocrv6Variant(
        cfg.variant.c_str(), cfg.provider.c_str(), cfg.cpuThreads, cfg.recBatchSize,
        cfg.detLimitType.c_str(), cfg.detLimitSideLen, cfg.detMaxSideLimit,
        cfg.detThresh, cfg.detBoxThresh, cfg.detUnclipRatio,
        PPOcrV6DetectorPostprocessName()).c_str());
    OutputDebugStringW((L"[PPOCRv6] det model: " + cfg.detModelPath + L"\n").c_str());
    OutputDebugStringW((L"[PPOCRv6] rec model: " + cfg.recModelPath + L"\n").c_str());

    std::wstring error;
    auto runtime = AcquirePPOcrV6Runtime(cfg, error);
    if (!runtime) {
        result.error = error.empty() ? L"Failed to initialize PP-OCRv6 runtime." : error;
        return result;
    }

    PPOcrV6DetectionInput detInput = BuildPPOcrV6DetectionInput(
        hBitmap, cfg, bm.bmWidth, bm.bmHeight);
    if (detInput.chw.empty()) {
        result.error = L"Failed to preprocess image for PP-OCRv6 detection.";
        return result;
    }

    PPOcrV6TensorOutput detOut;
    std::vector<int64_t> detShape = { 1, 3, detInput.height, detInput.width };
    if (!RunPPOcrV6Detector(*runtime, detInput.chw, detShape, detOut, error)) {
        result.error = error.empty() ? L"PP-OCRv6 detection failed." : error;
        return result;
    }

    auto boxes = ExtractPPOcrV6DetectionBoxes(detOut, cfg, bm.bmWidth, bm.bmHeight);
    // OWN-117: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPpocrv6DetBoxes(boxes.size(), detOut.shape.size()).c_str());

    if (boxes.empty()) {
        result.success = true;
        result.text = L"";
        return result;
    }

    // Geometry snapshot for assembler (original-image coordinates from ExtractBoxes).
    std::vector<PPOcrV6Blocks::DetBoxGeometry> boxGeometry;
    boxGeometry.reserve(boxes.size());
    for (const auto& box : boxes) {
        PPOcrV6Blocks::DetBoxGeometry g;
        g.rect = box.rect;
        g.detScore = box.score;
        g.points.reserve(box.points.size());
        for (const auto& p : box.points) {
            g.points.push_back({ p.X, p.Y });
        }
        boxGeometry.push_back(std::move(g));
    }

    std::vector<PPOcrV6Blocks::AcceptedLine> acceptedLines;
    acceptedLines.reserve(boxes.size());
    int cropSkipped = 0;
    int batchFallback = 0;
    int singleFailed = 0;
    int recInputs = 0;

    auto tryAcceptResults = [&](const std::vector<PPOcrV6RecognitionInput>& inputs,
                                const std::vector<PPOcrV6RecognitionResult>& recResults) -> bool {
        if (!PPOcrV6Blocks::BatchCountsMatch(inputs.size(), recResults.size())) {
            return false;
        }
        // Stage locally first so a mid-batch identity failure never partially
        // commits into acceptedLines (GOAL: validate fully then append).
        std::vector<PPOcrV6Blocks::AcceptedLine> staged;
        staged.reserve(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto& input = inputs[i];
            const auto& rr = recResults[i];
            if (input.sourceBoxIndex >= boxes.size()) {
                return false;
            }
            if (!PPOcrV6Blocks::IsAcceptedRecognition(rr.text, rr.score, cfg.recScoreThresh)) {
                continue;
            }
            PPOcrV6Blocks::AcceptedLine line;
            line.sourceBoxIndex = input.sourceBoxIndex;
            line.text = rr.text;
            line.recScore = rr.score;
            staged.push_back(std::move(line));
        }
        acceptedLines.insert(
            acceptedLines.end(),
            std::make_move_iterator(staged.begin()),
            std::make_move_iterator(staged.end()));
        return true;
    };

    // Permanent CTC/dict contract failure (class count, buffer, layout). Do not
    // fall back per-crop — every crop will fail the same way and would otherwise
    // surface as a silent blank success page.
    std::wstring fatalRecError;
    auto isCtcContractError = [](const std::wstring& err) -> bool {
        if (err.empty()) return false;
        return err.find(L"CTC class count") != std::wstring::npos ||
               err.find(L"CTC output") != std::wstring::npos ||
               err.find(L"CTC tensor") != std::wstring::npos ||
               err.find(L"effective recognition dictionary") != std::wstring::npos ||
               err.find(L"class id out of dictionary") != std::wstring::npos;
    };

    auto acceptSingleFallback = [&](const PPOcrV6RecognitionInput& input) {
        if (!fatalRecError.empty()) return;
        std::vector<PPOcrV6RecognitionInput> one{ input };
        std::vector<PPOcrV6RecognitionResult> recResults;
        std::wstring recError;
        if (!RunPPOcrV6RecognitionBatch(*runtime, one, recResults, recError) ||
            !PPOcrV6Blocks::SingleFallbackCountOk(recResults.size())) {
            ++singleFailed;
            if (isCtcContractError(recError)) {
                fatalRecError = recError;
            }
            if (!recError.empty()) {
                OutputDebugStringW((L"[PPOCRv6] recognition fallback failed: " + recError + L"\n").c_str());
            } else {
                OutputDebugStringA("[PPOCRv6] recognition fallback count mismatch\n");
            }
            return;
        }
        // Exactly one result — apply accept gate for this source box only.
        const auto& rr = recResults[0];
        if (input.sourceBoxIndex >= boxes.size()) {
            ++singleFailed;
            return;
        }
        if (!PPOcrV6Blocks::IsAcceptedRecognition(rr.text, rr.score, cfg.recScoreThresh)) {
            return;
        }
        PPOcrV6Blocks::AcceptedLine line;
        line.sourceBoxIndex = input.sourceBoxIndex;
        line.text = rr.text;
        line.recScore = rr.score;
        acceptedLines.push_back(std::move(line));
    };

    // Collect all recognition crops first, then batch by similar width (official
    // pipeline sorts by aspect before rec). Restore reading order via
    // sourceBoxIndex before block assembly.
    std::vector<PPOcrV6RecognitionInput> allRecInputs;
    allRecInputs.reserve(boxes.size());
    for (size_t boxIndex = 0; boxIndex < boxes.size(); ++boxIndex) {
        const auto& box = boxes[boxIndex];
        HBITMAP crop = CropPPOcrV6PerspectiveBitmap(hBitmap, box);
        if (!crop) {
            ++cropSkipped;
            continue;
        }

        PPOcrV6RecognitionImage recognitionImage = BuildPPOcrV6RecognitionImage(crop);
        DeleteObject(crop);
        PPOcrV6RecognitionInput recInput;
        recInput.width = recognitionImage.width;
        recInput.chw = std::move(recognitionImage.chw);
        if (recInput.chw.empty() || recInput.width <= 0) {
            OutputDebugStringA("[PPOCRv6] recognition preprocess failed for crop\n");
            ++cropSkipped;
            continue;
        }

        recInput.sourceBoxIndex = boxIndex;
        allRecInputs.push_back(std::move(recInput));
        ++recInputs;
    }

    std::vector<int> recWidths(allRecInputs.size(), 0);
    for (size_t i = 0; i < allRecInputs.size(); ++i) {
        recWidths[i] = allRecInputs[i].width;
    }
    const auto widthOrder = PPOcrV6RecBatch::OrderByWidthAscending(recWidths);
    // Cap width span inside a batch so a 3200-wide line does not pad short lines.
    constexpr float kMaxBatchWidthRatio = 2.0f;
    const auto plannedBatches = PPOcrV6RecBatch::BuildBatches(
        widthOrder, recWidths, cfg.recBatchSize, kMaxBatchWidthRatio);
    const long long paddedUnits = PPOcrV6RecBatch::TotalPaddedWidthUnits(plannedBatches, recWidths);
    // OWN-118: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPpocrv6RecPlan(
        allRecInputs.size(), plannedBatches.size(), cfg.recBatchSize, paddedUnits).c_str());

    auto flushRecBatch = [&](std::vector<PPOcrV6RecognitionInput>& recBatch) {
        if (recBatch.empty() || !fatalRecError.empty()) return;

        std::vector<PPOcrV6RecognitionResult> recResults;
        std::wstring recError;
        const bool batchOk = RunPPOcrV6RecognitionBatch(*runtime, recBatch, recResults, recError) &&
            PPOcrV6Blocks::BatchCountsMatch(recBatch.size(), recResults.size());

        if (batchOk) {
            // Count-matched batch: convert via staged accept. Identity failure
            // must not leave partial acceptedLines; fall through to single path.
            if (tryAcceptResults(recBatch, recResults)) {
                recBatch.clear();
                return;
            }
            OutputDebugStringA("[PPOCRv6] batch identity invalid; falling back to single recognition\n");
        } else if (isCtcContractError(recError)) {
            // Dict/class contract is global — abort whole page, no per-crop spam.
            fatalRecError = recError;
            OutputDebugStringW((L"[PPOCRv6] fatal CTC contract error: " + recError + L"\n").c_str());
            recBatch.clear();
            return;
        } else if (!recError.empty()) {
            OutputDebugStringW((L"[PPOCRv6] recognition batch failed: " + recError + L"\n").c_str());
        } else {
            // OWN-117: pure narrow debug (NarrowStringUtils).
            OutputDebugStringA(NarrowFormatPpocrv6RecBatchMismatch(recBatch.size(), recResults.size()).c_str());
        }

        ++batchFallback;
        // Single fallback for every pending input, including batch size 1 and
        // identity-failed count-matched batches (no partial commit above).
        for (const auto& single : recBatch) {
            if (!fatalRecError.empty()) break;
            acceptSingleFallback(single);
        }
        recBatch.clear();
    };

    for (const auto& indexBatch : plannedBatches) {
        if (!fatalRecError.empty()) break;
        std::vector<PPOcrV6RecognitionInput> recBatch;
        recBatch.reserve(indexBatch.size());
        for (size_t idx : indexBatch) {
            if (idx < allRecInputs.size()) {
                recBatch.push_back(std::move(allRecInputs[idx]));
            }
        }
        flushRecBatch(recBatch);
    }

    if (!fatalRecError.empty()) {
        result.success = false;
        result.error = fatalRecError;
        result.text.clear();
        result.blocks.clear();
        result.bboxes.clear();
        result.bboxClasses.clear();
        OutputDebugStringW((L"[PPOCRv6] aborting page on CTC contract failure: " + fatalRecError + L"\n").c_str());
        return result;
    }

    // Width-sorted recognition can accept lines out of reading order; restore
    // ascending sourceBoxIndex so text/blocks match det reading order.
    PPOcrV6RecBatch::SortBySourceBoxIndexMember(acceptedLines);

    PPOcrV6Blocks::AssembledOutput assembled;
    // Drop accepted lines whose DetBox geometry is unusable before assembly so
    // we never return full text with empty blocks (hard contract: same sequence).
    std::vector<PPOcrV6Blocks::AcceptedLine> assembleInput;
    assembleInput.reserve(acceptedLines.size());
    int geometryDropped = 0;
    for (const auto& line : acceptedLines) {
        if (line.sourceBoxIndex >= boxGeometry.size() ||
            !PPOcrV6Blocks::BboxLooksValid(boxGeometry[line.sourceBoxIndex].rect)) {
            ++geometryDropped;
            continue;
        }
        assembleInput.push_back(line);
    }
    if (geometryDropped > 0) {
        // OWN-117: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatPpocrv6DroppedInvalidBoxes(geometryDropped).c_str());
    }

    if (!PPOcrV6Blocks::AssemblePPOcrV6Output(boxGeometry, assembleInput, assembled, /*pageIndex=*/0)) {
        // Internal contract failure (e.g. duplicate sourceBoxIndex). Do not report
        // success with empty OCR — Dashboard/persistence would treat it as a valid
        // blank page. Normal empty det / low-score filter still succeed above.
        OutputDebugStringA("[PPOCRv6] block assembly failed after geometry filter\n");
        result.success = false;
        result.error = L"PP-OCRv6 internal block assembly failed.";
        result.text.clear();
        result.blocks.clear();
        result.bboxes.clear();
        result.bboxClasses.clear();
        // OWN-118: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatPpocrv6FinalStats(
            boxes.size(), recInputs, 0,
            cropSkipped, batchFallback, singleFailed, geometryDropped,
            1 /* assembleFailed */).c_str());
        return result;
    }

    result.text = std::move(assembled.text);
    result.blocks = std::move(assembled.blocks);
    result.bboxes = std::move(assembled.bboxes);
    result.bboxClasses = std::move(assembled.bboxClasses);
    result.success = true;

    // OWN-118: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPpocrv6FinalStats(
        boxes.size(), recInputs, result.blocks.size(),
        cropSkipped, batchFallback, singleFailed, geometryDropped,
        0 /* assembleFailed */).c_str());
    return result;
}

DWORD WINAPI OcrEnginePPOcrV6Onnx::WorkerThread(LPVOID param) {
    auto* p = static_cast<PPOcrV6Params*>(param);
    OcrOutput result;
    ULONGLONG startTick = GetTickCount64();

    try {
        OcrEnginePPOcrV6Onnx engine(std::move(p->config));
        result = engine.DoRecognize(p->hBitmap);
    } catch (std::exception const& ex) {
        // OWN-117: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatPpocrv6Exception(ex.what()).c_str());
        result.error = L"Exception during PP-OCRv6 ONNX OCR.";
    } catch (...) {
        OutputDebugStringA("[PPOCRv6] Unknown exception\n");
        result.error = L"Unknown error during PP-OCRv6 ONNX OCR.";
    }

    result.elapsedMs = (DWORD)(GetTickCount64() - startTick);
    DeleteObject(p->hBitmap);
    InvokeOcrCallbackSafely(p->callback, result);
    delete p;
    return 0;
}

void OcrEnginePPOcrV6Onnx::Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) {
    auto* params = new PPOcrV6Params{ hBitmap, std::move(callback), m_config };
    HANDLE h = CreateThread(nullptr, 0, WorkerThread, params, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        OcrOutput result;
        result.error = L"Failed to start the PP-OCRv6 worker thread.";
        DeleteObject(params->hBitmap);
        InvokeOcrCallbackSafely(params->callback, result);
        delete params;
    }
}
