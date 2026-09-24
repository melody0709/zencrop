#pragma once

// OWN-116: pure narrow (char) debug/label formatters.
// Dual-write style helpers — no HWND ownership; product still owns OutputDebugStringA.
// Keep format strings and argument order identical to historical sprintf_s sites.

#include <cstdio>
#include <format>
#include <string>

// Generic single-int debug: "[Tag] text %d\n"
inline std::string NarrowFormatDebugInt(const char* prefix, int value)
{
    return std::format("{}{}\n", prefix ? prefix : "", value);
}

// Generic single-unsigned-long debug: "[Tag] text %lu\n"
inline std::string NarrowFormatDebugULong(const char* prefix, unsigned long value)
{
    return std::format("{}{}\n", prefix ? prefix : "", value);
}

// Generic single-size_t debug: "[Tag] text %zu\n"
inline std::string NarrowFormatDebugSize(const char* prefix, size_t value)
{
    return std::format("{}{}\n", prefix ? prefix : "", value);
}

// Generic string message: "[Tag] text %s\n"
inline std::string NarrowFormatDebugCStr(const char* prefix, const char* msg)
{
    return std::format("{}{}\n", prefix ? prefix : "", msg ? msg : "unknown");
}

// LayoutEngine CreateSession failed.
inline std::string NarrowFormatLayoutCreateSessionFailed(const char* msg)
{
    return std::format("[LayoutEngine] CreateSession failed: {}\n", msg ? msg : "unknown");
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
    return std::format("[LayoutEngine]   Input {}: {}\n", index, name ? name : "");
}

// LayoutEngine output name/type/rank line.
inline std::string NarrowFormatLayoutOutput(
    size_t index, const char* name, int type, size_t rank)
{
    return std::format("[LayoutEngine]   Output {}: {} type={} rank={}\n", index, name ? name : "", type, rank);
}

// LayoutEngine tile reconciliation.
inline std::string NarrowFormatLayoutTileReconciliation(size_t before, size_t after)
{
    return std::format("[LayoutEngine] Tile reconciliation: {} -> {} regions\n", before, after);
}

// LayoutEngine tile fusion.
inline std::string NarrowFormatLayoutTileFusion(
    size_t full, size_t tile, size_t accepted, size_t finalCount)
{
    return std::format("[LayoutEngine] Tile fusion: full={} tile={} accepted={} final={}\n", full, tile, accepted, finalCount);
}

// LayoutEngine after dedup.
inline std::string NarrowFormatLayoutAfterDedup(size_t count)
{
    return std::format("[LayoutEngine] After dedup: {} regions\n", count);
}

// LayoutEngine PP-DocLayoutV3 detect done.
inline std::string NarrowFormatLayoutDetectDone(size_t count)
{
    return std::format("[LayoutEngine] PP-DocLayoutV3 detect done: {} regions\n", count);
}

// LayoutEngine tiled raw regions.
inline std::string NarrowFormatLayoutTiledRaw(size_t regions, int tiles)
{
    return std::format("[LayoutEngine] Tiled raw regions: {} from {} tiles\n", regions, tiles);
}

// LayoutEngine full stats.
inline std::string NarrowFormatLayoutFullStats(
    int width, int height, float aspect, float scaleH, float scaleW, size_t regions)
{
    return std::format("[LayoutEngine] Full stats: size={}x{} aspect={:.3f} scaleH={:.4f} scaleW={:.4f} regions={}\n", width, height, aspect, scaleH, scaleW, regions);
}

// LayoutEngine tiled stats.
inline std::string NarrowFormatLayoutTiledStats(size_t full, size_t tile)
{
    return std::format("[LayoutEngine] Tiled stats: full={} tile={}\n", full, tile);
}

// LayoutEngine family-change warning (fixed string helper).
inline const char* NarrowLayoutFamilyChangedWarning()
{
    return "[LayoutEngine] WARNING: layout family setting changed; cached session family remains active until reload\n";
}

// LlamaServer CreateJobObject failed.
inline std::string NarrowFormatLlamaCreateJobFailed(unsigned long err)
{
    return std::format("[LlamaServer] CreateJobObject failed: {}\n", err);
}

// LlamaServer SetInformationJobObject failed.
inline std::string NarrowFormatLlamaSetJobInfoFailed(unsigned long err)
{
    return std::format("[LlamaServer] SetInformationJobObject failed: {}\n", err);
}

// LlamaServer CreateProcess failed.
inline std::string NarrowFormatLlamaCreateProcessFailed(unsigned long err)
{
    return std::format("[LlamaServer] CreateProcess failed: {}\n", err);
}

// LlamaServer AssignProcessToJobObject failed.
inline std::string NarrowFormatLlamaAssignJobFailed(unsigned long err)
{
    return std::format("[LlamaServer] AssignProcessToJobObject failed: {}\n", err);
}

// LlamaServer ready on port.
inline std::string NarrowFormatLlamaServerReady(int port)
{
    return std::format("[LlamaServer] Server ready on port {}\n", port);
}

// MiniHttp started on port.
inline std::string NarrowFormatMiniHttpStarted(int port)
{
    return std::format("[MiniHttp] Started on port {}\n", port);
}

// Hotkey register failed.
inline std::string NarrowFormatHotkeyRegisterFailed(int id)
{
    return std::format("[Hotkey] Failed to register hotkey id={}\n", id);
}

// OCR result received.
inline std::string NarrowFormatOcrResultReceived(int success, size_t textLen, size_t errLen)
{
    return std::format("[OCR] Result received: success={}, textLen={}, errLen={}\n", success, textLen, errLen);
}

// HTTP WinHttpCrackUrl failed.
inline std::string NarrowFormatHttpCrackUrlFailed(unsigned long err)
{
    return std::format("[HTTP] WinHttpCrackUrl failed: {}\n", err);
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
    return std::format("[HTTP] WinHttpOpen failed: {}\n", err);
}

// HTTP WinHttpConnect failed.
inline std::string NarrowFormatHttpConnectFailed(unsigned long err)
{
    return std::format("[HTTP] WinHttpConnect failed: {}\n", err);
}

// HTTP WinHttpOpenRequest failed.
inline std::string NarrowFormatHttpOpenRequestFailed(unsigned long err)
{
    return std::format("[HTTP] WinHttpOpenRequest failed: {}\n", err);
}

// HTTP header count.
inline std::string NarrowFormatHttpHeaderCount(size_t count)
{
    return std::format("[HTTP] Header count: {}\n", count);
}

// HTTP body size.
inline std::string NarrowFormatHttpBodySize(size_t bytes)
{
    return std::format("[HTTP] Body size: {} bytes\n", bytes);
}

// HTTP WinHttpSendRequest failed.
inline std::string NarrowFormatHttpSendFailed(unsigned long err)
{
    return std::format("[HTTP] WinHttpSendRequest failed: {}\n", err);
}

// HTTP WinHttpReceiveResponse failed.
inline std::string NarrowFormatHttpReceiveFailed(unsigned long err)
{
    return std::format("[HTTP] WinHttpReceiveResponse failed: {}\n", err);
}

// HTTP status code.
inline std::string NarrowFormatHttpStatusCode(int status)
{
    return std::format("[HTTP] Status code: {}\n", status);
}

// HTTP response body size.
inline std::string NarrowFormatHttpResponseBodySize(size_t bytes)
{
    return std::format("[HTTP] Response body size: {} bytes\n", bytes);
}

// Generic percent-encoded byte for URL encode paths.
inline std::string NarrowFormatPercentHexByte(unsigned char value)
{
    return std::format("%{:02X}", value);
}

// LayoutEngine generic warning prefix helper.
inline std::string NarrowFormatLayoutWarn(const char* msg)
{
    return std::format("[LayoutEngine] WARNING: {}\n", msg ? msg : "");
}

// LayoutEngine region count with free label.
inline std::string NarrowFormatLayoutRegionsLabeled(const char* label, size_t count)
{
    return std::format("[LayoutEngine] {}: {} regions\n", label ? label : "", count);
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
    return std::format("[LayoutEngine] query[{}] cls={} score={:.4f} box=[{:.1f},{:.1f},{:.1f},{:.1f}] order={}\n", index, cls, score, x0, y0, x1, y1, order);
}

// LayoutEngine postprocess stats line.
inline std::string NarrowFormatLayoutPostprocessStats(
    size_t raw, size_t scorePassed, size_t nmsKept, size_t imageAreaKept,
    size_t classModeKept, size_t polygonFallbacks, size_t overlapKept,
    size_t finalCount, size_t exactScoreTies, int v3PolygonDegraded)
{
    return std::format("[LayoutEngine] postprocess raw={} score={} nms={} image={} class={} polygonFallback={} overlap={} final={} ties={} degraded={}\n", raw, scorePassed, nmsKept, imageAreaKept,
        classModeKept, polygonFallbacks, overlapKept,
        finalCount, exactScoreTies, v3PolygonDegraded);
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
    return std::format("[OCR] Saved {} images (scoped layoutParsingResults)\n", imageCount);
}

// OCR saved images.
inline std::string NarrowFormatOcrSavedImages(int imageCount)
{
    return std::format("[OCR] Saved {} images\n", imageCount);
}

// OCR async API model/body.
inline std::string NarrowFormatOcrAsyncApiModel(const char* model, size_t bodyBytes)
{
    return std::format("[OCR] Async API model={} body={} bytes\n", model ? model : "", bodyBytes);
}

// OCR async job submitted (fixed).
inline const char* NarrowOcrAsyncJobSubmitted()
{
    return "[OCR] Async job submitted\n";
}

// OCR exception.
inline std::string NarrowFormatOcrException(const char* msg)
{
    return std::format("[OCR] Exception: {}\n", msg ? msg : "unknown");
}

// PaddleDoc layout detected regions.
inline std::string NarrowFormatPaddleDocLayoutDetected(size_t regions)
{
    return std::format("[PaddleDoc] Layout detected {} original regions\n", regions);
}

// PaddleDoc exception.
inline std::string NarrowFormatPaddleDocException(const char* msg)
{
    return std::format("[PaddleDoc] Exception: {}\n", msg ? msg : "unknown");
}

// PaddleLocal exception.
inline std::string NarrowFormatPaddleLocalException(const char* msg)
{
    return std::format("[PaddleLocal] Exception: {}\n", msg ? msg : "unknown");
}

// PPOCRv6 loaded model.
inline std::string NarrowFormatPpocrv6LoadedModel(
    const char* which, const char* input, const char* output)
{
    return std::format("[PPOCRv6] Loaded {} model. input={} output={}\n", which ? which : "", input ? input : "", output ? output : "");
}

// PPOCRv6 OpenCV DBPostProcess failed.
inline std::string NarrowFormatPpocrv6DbPostFailed(const char* msg)
{
    return std::format("[PPOCRv6] OpenCV DBPostProcess failed: {}\n", msg ? msg : "unknown");
}

// PPOCRv6 OpenCV crop failed.
inline std::string NarrowFormatPpocrv6CropFailed(const char* msg)
{
    return std::format("[PPOCRv6] OpenCV crop failed: {}\n", msg ? msg : "unknown");
}

// PPOCRv6 det boxes.
inline std::string NarrowFormatPpocrv6DetBoxes(size_t boxes, size_t dims)
{
    return std::format("[PPOCRv6] det boxes={} output_shape_dims={}\n", boxes, dims);
}

// PPOCRv6 recognition batch count mismatch.
inline std::string NarrowFormatPpocrv6RecBatchMismatch(size_t inputs, size_t results)
{
    return std::format("[PPOCRv6] recognition batch count mismatch inputs={} results={}\n", inputs, results);
}

// PPOCRv6 dropped invalid box lines.
inline std::string NarrowFormatPpocrv6DroppedInvalidBoxes(int count)
{
    return std::format("[PPOCRv6] dropped {} accepted line(s) with invalid box geometry\n", count);
}

// PPOCRv6 exception.
inline std::string NarrowFormatPpocrv6Exception(const char* msg)
{
    return std::format("[PPOCRv6] Exception: {}\n", msg ? msg : "unknown");
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
    return std::format("{}{} {}\n", prefix ? prefix : "", a, b);
}

// ---------------------------------------------------------------------------
// OWN-118: residual complex multi-arg product narrow debug formatters.
// ---------------------------------------------------------------------------

// OCR Cloud upload image summary.
inline std::string NarrowFormatOcrCloudUploadImage(
    const char* contentType, size_t bytes, int usedPngFallback)
{
    return std::format("[OCR] Cloud upload image: {}, {} bytes{}\n", contentType ? contentType : "",
        bytes,
        usedPngFallback ? " (JPEG encode fallback)" : "");
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
    return std::format("[PPOCRv6] rec plan: inputs={} batches={} batchSize={} paddedWidthUnits={}\n", inputs, batches, batchSize, paddedUnits);
}

// PPOCRv6 final stats (assemble failed path uses acceptedBlocks=0).
inline std::string NarrowFormatPpocrv6FinalStats(
    size_t detBoxes, int recInputs, size_t acceptedBlocks,
    int cropSkipped, int batchFallback, int singleFailed, int geometryDropped,
    int assembleFailed)
{
    return std::format(
        "[PPOCRv6] det boxes={} rec inputs={} accepted blocks={} crop skipped={} batch fallback={} single failed={} geometry dropped={}{}\n",
        detBoxes, recInputs, acceptedBlocks, cropSkipped, batchFallback, singleFailed, geometryDropped,
        assembleFailed ? " (assemble failed)" : "");
}
