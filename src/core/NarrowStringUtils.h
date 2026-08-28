#pragma once

// OWN-116: pure narrow (char) debug/label formatters.
// Dual-write style helpers — no HWND ownership; product still owns OutputDebugStringA.
// Keep format strings and argument order identical to historical sprintf_s sites.

#include <cstdio>
#include <string>

// Generic single-int debug: "[Tag] text %d\n"
inline std::string NarrowFormatDebugInt(const char* prefix, int value)
{
    char buf[256] = {};
    sprintf_s(buf, "%s%d\n", prefix ? prefix : "", value);
    return buf;
}

// Generic single-unsigned-long debug: "[Tag] text %lu\n"
inline std::string NarrowFormatDebugULong(const char* prefix, unsigned long value)
{
    char buf[256] = {};
    sprintf_s(buf, "%s%lu\n", prefix ? prefix : "", value);
    return buf;
}

// Generic single-size_t debug: "[Tag] text %zu\n"
inline std::string NarrowFormatDebugSize(const char* prefix, size_t value)
{
    char buf[256] = {};
    sprintf_s(buf, "%s%zu\n", prefix ? prefix : "", value);
    return buf;
}

// Generic string message: "[Tag] text %s\n"
inline std::string NarrowFormatDebugCStr(const char* prefix, const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "%s%s\n", prefix ? prefix : "", msg ? msg : "unknown");
    return buf;
}

// LayoutEngine CreateSession failed.
inline std::string NarrowFormatLayoutCreateSessionFailed(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[LayoutEngine] CreateSession failed: %s\n", msg ? msg : "unknown");
    return buf;
}

// LayoutEngine ONNX loaded summary.
inline std::string NarrowFormatLayoutOnnxLoaded(
    const wchar_t* family, size_t numInputs, size_t numOutputs)
{
    char buf[512] = {};
    sprintf_s(buf, "[LayoutEngine] ONNX loaded: family=%ls inputs=%zu outputs=%zu\n",
        family ? family : L"", numInputs, numOutputs);
    return buf;
}

// LayoutEngine input name line.
inline std::string NarrowFormatLayoutInput(size_t index, const char* name)
{
    char buf[512] = {};
    sprintf_s(buf, "[LayoutEngine]   Input %zu: %s\n", index, name ? name : "");
    return buf;
}

// LayoutEngine output name/type/rank line.
inline std::string NarrowFormatLayoutOutput(
    size_t index, const char* name, int type, size_t rank)
{
    char buf[512] = {};
    sprintf_s(buf, "[LayoutEngine]   Output %zu: %s type=%d rank=%zu\n",
        index, name ? name : "", type, rank);
    return buf;
}

// LayoutEngine tile reconciliation.
inline std::string NarrowFormatLayoutTileReconciliation(size_t before, size_t after)
{
    char buf[256] = {};
    sprintf_s(buf, "[LayoutEngine] Tile reconciliation: %zu -> %zu regions\n", before, after);
    return buf;
}

// LayoutEngine tile fusion.
inline std::string NarrowFormatLayoutTileFusion(
    size_t full, size_t tile, size_t accepted, size_t finalCount)
{
    char buf[320] = {};
    sprintf_s(buf, "[LayoutEngine] Tile fusion: full=%zu tile=%zu accepted=%zu final=%zu\n",
        full, tile, accepted, finalCount);
    return buf;
}

// LayoutEngine after dedup.
inline std::string NarrowFormatLayoutAfterDedup(size_t count)
{
    char buf[256] = {};
    sprintf_s(buf, "[LayoutEngine] After dedup: %zu regions\n", count);
    return buf;
}

// LayoutEngine PP-DocLayoutV3 detect done.
inline std::string NarrowFormatLayoutDetectDone(size_t count)
{
    char buf[256] = {};
    sprintf_s(buf, "[LayoutEngine] PP-DocLayoutV3 detect done: %zu regions\n", count);
    return buf;
}

// LayoutEngine tiled raw regions.
inline std::string NarrowFormatLayoutTiledRaw(size_t regions, int tiles)
{
    char buf[256] = {};
    sprintf_s(buf, "[LayoutEngine] Tiled raw regions: %zu from %d tiles\n", regions, tiles);
    return buf;
}

// LayoutEngine full stats.
inline std::string NarrowFormatLayoutFullStats(
    int width, int height, float aspect, float scaleH, float scaleW, size_t regions)
{
    char buf[384] = {};
    sprintf_s(buf,
        "[LayoutEngine] Full stats: size=%dx%d aspect=%.3f scaleH=%.4f scaleW=%.4f regions=%zu\n",
        width, height, aspect, scaleH, scaleW, regions);
    return buf;
}

// LayoutEngine tiled stats.
inline std::string NarrowFormatLayoutTiledStats(size_t full, size_t tile)
{
    char buf[256] = {};
    sprintf_s(buf, "[LayoutEngine] Tiled stats: full=%zu tile=%zu\n", full, tile);
    return buf;
}

// LayoutEngine family-change warning (fixed string helper).
inline const char* NarrowLayoutFamilyChangedWarning()
{
    return "[LayoutEngine] WARNING: layout family setting changed; cached session family remains active until reload\n";
}

// LlamaServer CreateJobObject failed.
inline std::string NarrowFormatLlamaCreateJobFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[LlamaServer] CreateJobObject failed: %lu\n", err);
    return buf;
}

// LlamaServer SetInformationJobObject failed.
inline std::string NarrowFormatLlamaSetJobInfoFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[LlamaServer] SetInformationJobObject failed: %lu\n", err);
    return buf;
}

// LlamaServer CreateProcess failed.
inline std::string NarrowFormatLlamaCreateProcessFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[LlamaServer] CreateProcess failed: %lu\n", err);
    return buf;
}

// LlamaServer AssignProcessToJobObject failed.
inline std::string NarrowFormatLlamaAssignJobFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[LlamaServer] AssignProcessToJobObject failed: %lu\n", err);
    return buf;
}

// LlamaServer ready on port.
inline std::string NarrowFormatLlamaServerReady(int port)
{
    char buf[256] = {};
    sprintf_s(buf, "[LlamaServer] Server ready on port %d\n", port);
    return buf;
}

// MiniHttp started on port.
inline std::string NarrowFormatMiniHttpStarted(int port)
{
    char buf[256] = {};
    sprintf_s(buf, "[MiniHttp] Started on port %d\n", port);
    return buf;
}

// Hotkey register failed.
inline std::string NarrowFormatHotkeyRegisterFailed(int id)
{
    char buf[256] = {};
    sprintf_s(buf, "[Hotkey] Failed to register hotkey id=%d\n", id);
    return buf;
}

// OCR result received.
inline std::string NarrowFormatOcrResultReceived(int success, size_t textLen, size_t errLen)
{
    char buf[256] = {};
    sprintf_s(buf, "[OCR] Result received: success=%d, textLen=%zu, errLen=%zu\n",
        success, textLen, errLen);
    return buf;
}

// HTTP WinHttpCrackUrl failed.
inline std::string NarrowFormatHttpCrackUrlFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[HTTP] WinHttpCrackUrl failed: %lu\n", err);
    return buf;
}

// HTTP host/path/https/port.
inline std::string NarrowFormatHttpHostPath(
    const wchar_t* host, const wchar_t* path, int https, int port)
{
    char buf[768] = {};
    sprintf_s(buf, "[HTTP] Host: %ls, Path: %ls, HTTPS: %d, Port: %d\n",
        host ? host : L"", path ? path : L"", https, port);
    return buf;
}

// HTTP WinHttpOpen failed.
inline std::string NarrowFormatHttpOpenFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[HTTP] WinHttpOpen failed: %lu\n", err);
    return buf;
}

// HTTP WinHttpConnect failed.
inline std::string NarrowFormatHttpConnectFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[HTTP] WinHttpConnect failed: %lu\n", err);
    return buf;
}

// HTTP WinHttpOpenRequest failed.
inline std::string NarrowFormatHttpOpenRequestFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[HTTP] WinHttpOpenRequest failed: %lu\n", err);
    return buf;
}

// HTTP header count.
inline std::string NarrowFormatHttpHeaderCount(size_t count)
{
    char buf[128] = {};
    sprintf_s(buf, "[HTTP] Header count: %zu\n", count);
    return buf;
}

// HTTP body size.
inline std::string NarrowFormatHttpBodySize(size_t bytes)
{
    char buf[128] = {};
    sprintf_s(buf, "[HTTP] Body size: %zu bytes\n", bytes);
    return buf;
}

// HTTP WinHttpSendRequest failed.
inline std::string NarrowFormatHttpSendFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[HTTP] WinHttpSendRequest failed: %lu\n", err);
    return buf;
}

// HTTP WinHttpReceiveResponse failed.
inline std::string NarrowFormatHttpReceiveFailed(unsigned long err)
{
    char buf[256] = {};
    sprintf_s(buf, "[HTTP] WinHttpReceiveResponse failed: %lu\n", err);
    return buf;
}

// HTTP status code.
inline std::string NarrowFormatHttpStatusCode(int status)
{
    char buf[128] = {};
    sprintf_s(buf, "[HTTP] Status code: %d\n", status);
    return buf;
}

// HTTP response body size.
inline std::string NarrowFormatHttpResponseBodySize(size_t bytes)
{
    char buf[128] = {};
    sprintf_s(buf, "[HTTP] Response body size: %zu bytes\n", bytes);
    return buf;
}

// Generic percent-encoded byte for URL encode paths.
inline std::string NarrowFormatPercentHexByte(unsigned char value)
{
    char buf[8] = {};
    sprintf_s(buf, "%%%02X", value);
    return buf;
}

// LayoutEngine generic warning prefix helper.
inline std::string NarrowFormatLayoutWarn(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[LayoutEngine] WARNING: %s\n", msg ? msg : "");
    return buf;
}

// LayoutEngine region count with free label.
inline std::string NarrowFormatLayoutRegionsLabeled(const char* label, size_t count)
{
    char buf[320] = {};
    sprintf_s(buf, "[LayoutEngine] %s: %zu regions\n", label ? label : "", count);
    return buf;
}

// LayoutEngine detect image stats.
inline std::string NarrowFormatLayoutDetectImage(
    int origW, int origH, float aspect, float scaleH, float scaleW, const wchar_t* family)
{
    char buf[384] = {};
    sprintf_s(buf,
        "[LayoutEngine] Detect image: %dx%d aspect=%.3f scaleH=%.4f scaleW=%.4f family=%ls\n",
        origW, origH, aspect, scaleH, scaleW, family ? family : L"");
    return buf;
}

// LayoutEngine query dump row.
inline std::string NarrowFormatLayoutQueryRow(
    size_t index, int cls, float score,
    float x0, float y0, float x1, float y1, int order)
{
    char buf[320] = {};
    sprintf_s(buf,
        "[LayoutEngine] query[%zu] cls=%d score=%.4f box=[%.1f,%.1f,%.1f,%.1f] order=%d\n",
        index, cls, score, x0, y0, x1, y1, order);
    return buf;
}

// LayoutEngine postprocess stats line.
inline std::string NarrowFormatLayoutPostprocessStats(
    size_t raw, size_t scorePassed, size_t nmsKept, size_t imageAreaKept,
    size_t classModeKept, size_t polygonFallbacks, size_t overlapKept,
    size_t finalCount, size_t exactScoreTies, int v3PolygonDegraded)
{
    char buf[512] = {};
    sprintf_s(buf,
        "[LayoutEngine] postprocess raw=%zu score=%zu nms=%zu image=%zu class=%zu polygonFallback=%zu overlap=%zu final=%zu ties=%zu degraded=%d\n",
        raw, scorePassed, nmsKept, imageAreaKept,
        classModeKept, polygonFallbacks, overlapKept,
        finalCount, exactScoreTies, v3PolygonDegraded);
    return buf;
}

// ---------------------------------------------------------------------------
// OWN-117: residual product OCR / engine narrow debug formatters.
// ---------------------------------------------------------------------------

// OCR rejected provider image URL.
inline std::string NarrowFormatOcrRejectedProviderUrl(const wchar_t* err)
{
    char buf[512] = {};
    sprintf_s(buf, "[OCR] Rejected provider image URL: %ls\n", err ? err : L"");
    return buf;
}

// OCR provider image download failed attempt.
inline std::string NarrowFormatOcrProviderDownloadFailed(
    int attempt, int maxAttempts, int status, size_t body, const wchar_t* err)
{
    char buf[640] = {};
    sprintf_s(buf,
        "[OCR] Provider image download failed attempt=%d/%d status=%d body=%zu err=%ls\n",
        attempt, maxAttempts, status, body, err ? err : L"");
    return buf;
}

// OCR provider image content-type rejected.
inline std::string NarrowFormatOcrProviderContentTypeRejected(const wchar_t* contentType)
{
    char buf[512] = {};
    sprintf_s(buf, "[OCR] Provider image content-type rejected: %ls\n",
        contentType ? contentType : L"");
    return buf;
}

// OCR found image key.
inline std::string NarrowFormatOcrFoundImageKey(int pageOrdinal, const wchar_t* key)
{
    char buf[512] = {};
    sprintf_s(buf, "[OCR] Found image key (page %d): %ls\n",
        pageOrdinal, key ? key : L"");
    return buf;
}

// OCR image is URL (fixed).
inline const char* NarrowOcrImageIsUrl()
{
    return "[OCR] Image is URL, downloading...\n";
}

// OCR image is base64 (fixed).
inline const char* NarrowOcrImageIsBase64()
{
    return "[OCR] Image is base64, decoding...\n";
}

// OCR saved images scoped.
inline std::string NarrowFormatOcrSavedImagesScoped(int imageCount)
{
    char buf[256] = {};
    sprintf_s(buf, "[OCR] Saved %d images (scoped layoutParsingResults)\n", imageCount);
    return buf;
}

// OCR saved images.
inline std::string NarrowFormatOcrSavedImages(int imageCount)
{
    char buf[128] = {};
    sprintf_s(buf, "[OCR] Saved %d images\n", imageCount);
    return buf;
}

// OCR async API model/body.
inline std::string NarrowFormatOcrAsyncApiModel(const char* model, size_t bodyBytes)
{
    char buf[320] = {};
    sprintf_s(buf, "[OCR] Async API model=%s body=%zu bytes\n",
        model ? model : "", bodyBytes);
    return buf;
}

// OCR async job submitted (fixed).
inline const char* NarrowOcrAsyncJobSubmitted()
{
    return "[OCR] Async job submitted\n";
}

// OCR exception.
inline std::string NarrowFormatOcrException(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[OCR] Exception: %s\n", msg ? msg : "unknown");
    return buf;
}

// PaddleDoc layout detected regions.
inline std::string NarrowFormatPaddleDocLayoutDetected(size_t regions)
{
    char buf[256] = {};
    sprintf_s(buf, "[PaddleDoc] Layout detected %zu original regions\n", regions);
    return buf;
}

// PaddleDoc exception.
inline std::string NarrowFormatPaddleDocException(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[PaddleDoc] Exception: %s\n", msg ? msg : "unknown");
    return buf;
}

// PaddleLocal exception.
inline std::string NarrowFormatPaddleLocalException(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[PaddleLocal] Exception: %s\n", msg ? msg : "unknown");
    return buf;
}

// PPOCRv6 loaded model.
inline std::string NarrowFormatPpocrv6LoadedModel(
    const char* which, const char* input, const char* output)
{
    char buf[512] = {};
    sprintf_s(buf, "[PPOCRv6] Loaded %s model. input=%s output=%s\n",
        which ? which : "", input ? input : "", output ? output : "");
    return buf;
}

// PPOCRv6 OpenCV DBPostProcess failed.
inline std::string NarrowFormatPpocrv6DbPostFailed(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[PPOCRv6] OpenCV DBPostProcess failed: %s\n", msg ? msg : "unknown");
    return buf;
}

// PPOCRv6 OpenCV crop failed.
inline std::string NarrowFormatPpocrv6CropFailed(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[PPOCRv6] OpenCV crop failed: %s\n", msg ? msg : "unknown");
    return buf;
}

// PPOCRv6 det boxes.
inline std::string NarrowFormatPpocrv6DetBoxes(size_t boxes, size_t dims)
{
    char buf[256] = {};
    sprintf_s(buf, "[PPOCRv6] det boxes=%zu output_shape_dims=%zu\n", boxes, dims);
    return buf;
}

// PPOCRv6 recognition batch count mismatch.
inline std::string NarrowFormatPpocrv6RecBatchMismatch(size_t inputs, size_t results)
{
    char buf[256] = {};
    sprintf_s(buf, "[PPOCRv6] recognition batch count mismatch inputs=%zu results=%zu\n",
        inputs, results);
    return buf;
}

// PPOCRv6 dropped invalid box lines.
inline std::string NarrowFormatPpocrv6DroppedInvalidBoxes(int count)
{
    char buf[256] = {};
    sprintf_s(buf, "[PPOCRv6] dropped %d accepted line(s) with invalid box geometry\n", count);
    return buf;
}

// PPOCRv6 exception.
inline std::string NarrowFormatPpocrv6Exception(const char* msg)
{
    char buf[512] = {};
    sprintf_s(buf, "[PPOCRv6] Exception: %s\n", msg ? msg : "unknown");
    return buf;
}

// Generic tag + wide string: "[Tag] text %ls\n"
inline std::string NarrowFormatDebugWStr(const char* prefix, const wchar_t* msg)
{
    char buf[768] = {};
    sprintf_s(buf, "%s%ls\n", prefix ? prefix : "", msg ? msg : L"");
    return buf;
}

// Generic tag + int + size_t.
inline std::string NarrowFormatDebugIntSize(
    const char* prefix, int a, size_t b)
{
    char buf[256] = {};
    sprintf_s(buf, "%s%d %zu\n", prefix ? prefix : "", a, b);
    return buf;
}

// ---------------------------------------------------------------------------
// OWN-118: residual complex multi-arg product narrow debug formatters.
// ---------------------------------------------------------------------------

// OCR Cloud upload image summary.
inline std::string NarrowFormatOcrCloudUploadImage(
    const char* contentType, size_t bytes, int usedPngFallback)
{
    char buf[320] = {};
    sprintf_s(buf,
        "[OCR] Cloud upload image: %s, %zu bytes%s\n",
        contentType ? contentType : "",
        bytes,
        usedPngFallback ? " (JPEG encode fallback)" : "");
    return buf;
}

// Shared server probe summary (PaddleLocal / PaddleDoc).
inline std::string NarrowFormatPaddleServerProbe(
    const char* tag,
    int modelsReachable, int modelsHttpStatus,
    int propsReachable, int propsHttpStatus,
    int modelListed, int multimodal,
    int totalSlots, int slotContext,
    const wchar_t* warning)
{
    char buf[768] = {};
    sprintf_s(buf,
        "[%s] server models=%d/%d props=%d/%d modelListed=%d multimodal=%d "
        "slots=%d context=%d warning=%ls\n",
        tag ? tag : "Paddle",
        modelsReachable, modelsHttpStatus,
        propsReachable, propsHttpStatus,
        modelListed, multimodal,
        totalSlots, slotContext,
        warning ? warning : L"");
    return buf;
}

// PaddleLocal VLM metrics line.
inline std::string NarrowFormatPaddleLocalMetrics(
    int httpStatus, unsigned long elapsedMs, int timeoutMs,
    size_t pngBytes, size_t requestBytes, size_t responseBytes,
    int promptTokens, int completionTokens, int totalTokens,
    const wchar_t* finishReason, const wchar_t* repetitionReason,
    const wchar_t* errorCategory)
{
    char buf[768] = {};
    sprintf_s(buf,
        "[PaddleLocal] status=%d elapsed=%lu timeout=%d png=%zu request=%zu response=%zu "
        "tokens=%d/%d/%d finish=%ls repetition=%ls errorCategory=%ls\n",
        httpStatus, elapsedMs, timeoutMs,
        pngBytes, requestBytes, responseBytes,
        promptTokens, completionTokens, totalTokens,
        finishReason ? finishReason : L"",
        repetitionReason ? repetitionReason : L"",
        errorCategory ? errorCategory : L"");
    return buf;
}

// PaddleDoc per-group recognition metrics line.
inline std::string NarrowFormatPaddleDocGroupMetrics(
    const wchar_t* groupId, size_t members, size_t owner,
    int attempts, int skipped,
    int cropW, int cropH, int polygon, int margin,
    int httpStatus, unsigned long elapsedMs, int timeoutMs,
    size_t imageBytes, const wchar_t* imageMime, size_t pngBytes,
    unsigned long buildUs, size_t requestBytes, size_t responseBytes,
    int promptTokens, int completionTokens, int totalTokens,
    const wchar_t* finishReason, const wchar_t* repetitionReason,
    const wchar_t* errorCategory)
{
    char buf[1024] = {};
    sprintf_s(buf,
        "[PaddleDoc] group=%ls members=%zu owner=%zu attempts=%d skipped=%d "
        "crop=%dx%d polygon=%d margin=%d status=%d elapsed=%lu timeout=%d "
        "image=%zu mime=%ls png=%zu buildUs=%lu request=%zu response=%zu tokens=%d/%d/%d finish=%ls "
        "repetition=%ls errorCategory=%ls\n",
        groupId ? groupId : L"", members, owner,
        attempts, skipped,
        cropW, cropH, polygon, margin,
        httpStatus, elapsedMs, timeoutMs,
        imageBytes, imageMime ? imageMime : L"", pngBytes,
        buildUs, requestBytes, responseBytes,
        promptTokens, completionTokens, totalTokens,
        finishReason ? finishReason : L"",
        repetitionReason ? repetitionReason : L"",
        errorCategory ? errorCategory : L"");
    return buf;
}

// PaddleDoc pipeline summary line.
inline std::string NarrowFormatPaddleDocPipelineSummary(
    size_t blocks, size_t groups, size_t recognized, size_t secondary,
    size_t skipped, size_t failed, size_t maxMembers, float maxAspect,
    size_t aspectSplits, size_t limitSplits,
    size_t totalImage, size_t totalPng,
    const wchar_t* mode, int fallback)
{
    char buf[768] = {};
    sprintf_s(buf,
        "[PaddleDoc] blocks=%zu groups=%zu recognized=%zu secondary=%zu skipped=%zu "
        "failed=%zu maxMembers=%zu maxAspect=%.3f aspectSplits=%zu limitSplits=%zu "
        "totalImage=%zu totalPng=%zu mode=%ls fallback=%d\n",
        blocks, groups, recognized, secondary,
        skipped, failed, maxMembers, maxAspect,
        aspectSplits, limitSplits,
        totalImage, totalPng,
        mode ? mode : L"", fallback);
    return buf;
}

// PPOCRv6 config/variant line.
inline std::string NarrowFormatPpocrv6Variant(
    const wchar_t* variant, const wchar_t* provider,
    int threads, int recBatch,
    const wchar_t* limitType, int limitSide, int maxSide,
    float thresh, float boxThresh, float unclip,
    const char* dbpost)
{
    char buf[640] = {};
    sprintf_s(buf,
        "[PPOCRv6] variant=%ls provider=%ls threads=%d recBatch=%d limit=%ls/%d max=%d thresh=%.2f/%.2f unclip=%.2f dbpost=%s\n",
        variant ? variant : L"", provider ? provider : L"",
        threads, recBatch,
        limitType ? limitType : L"", limitSide, maxSide,
        thresh, boxThresh, unclip,
        dbpost ? dbpost : "");
    return buf;
}

// PPOCRv6 rec plan line.
inline std::string NarrowFormatPpocrv6RecPlan(
    size_t inputs, size_t batches, int batchSize, long long paddedUnits)
{
    char buf[320] = {};
    sprintf_s(buf,
        "[PPOCRv6] rec plan: inputs=%zu batches=%zu batchSize=%d paddedWidthUnits=%lld\n",
        inputs, batches, batchSize, paddedUnits);
    return buf;
}

// PPOCRv6 final stats (assemble failed path uses acceptedBlocks=0).
inline std::string NarrowFormatPpocrv6FinalStats(
    size_t detBoxes, int recInputs, size_t acceptedBlocks,
    int cropSkipped, int batchFallback, int singleFailed, int geometryDropped,
    int assembleFailed)
{
    char buf[384] = {};
    if (assembleFailed) {
        sprintf_s(buf,
            "[PPOCRv6] det boxes=%zu rec inputs=%d accepted blocks=0 crop skipped=%d batch fallback=%d single failed=%d geometry dropped=%d (assemble failed)\n",
            detBoxes, recInputs, cropSkipped, batchFallback, singleFailed, geometryDropped);
    } else {
        sprintf_s(buf,
            "[PPOCRv6] det boxes=%zu rec inputs=%d accepted blocks=%zu crop skipped=%d batch fallback=%d single failed=%d geometry dropped=%d\n",
            detBoxes, recInputs, acceptedBlocks, cropSkipped, batchFallback, singleFailed, geometryDropped);
    }
    return buf;
}
