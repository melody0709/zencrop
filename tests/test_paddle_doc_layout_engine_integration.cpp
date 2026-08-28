#include "core/Settings.h"
#include "image/BitmapCodec.h"
#include "ocr/BitmapUtils.h"
#include "ocr/OcrUtils.h"
#include "ocr/layout/LayoutEngine.h"

#include <gdiplus.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

OcrSettings LoadOcrSettings() {
    OcrSettings settings;
    settings.layoutModelFamily = L"auto";
    settings.layoutThresholdProfile = L"official";
    settings.docUsePhysicalSorting = false;
    return settings;
}

HBITMAP CropBitmap(HBITMAP bitmap, RECT rect) {
    if (!bitmap || rect.right <= rect.left || rect.bottom <= rect.top) return nullptr;
    Gdiplus::Bitmap source(bitmap, nullptr);
    if (source.GetLastStatus() != Gdiplus::Ok ||
        rect.left < 0 || rect.top < 0 ||
        rect.right > (LONG)source.GetWidth() || rect.bottom > (LONG)source.GetHeight()) {
        return nullptr;
    }
    Gdiplus::Bitmap cropped(
        rect.right - rect.left, rect.bottom - rect.top, PixelFormat32bppARGB);
    if (cropped.GetLastStatus() != Gdiplus::Ok) return nullptr;
    Gdiplus::Graphics graphics(&cropped);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    if (graphics.DrawImage(
            &source,
            Gdiplus::Rect(0, 0, rect.right - rect.left, rect.bottom - rect.top),
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            Gdiplus::UnitPixel) != Gdiplus::Ok) {
        return nullptr;
    }
    HBITMAP result = nullptr;
    return cropped.GetHBITMAP(Gdiplus::Color(255, 255, 255), &result) == Gdiplus::Ok
        ? result : nullptr;
}

const LayoutClassInfo* GetLayoutClassInfo(int classId) {
    static const LayoutClassInfo classes[] = {
        {0, L"abstract", L"OCR:", 0, false, false, false},
        {1, L"algorithm", L"OCR:", 0, false, false, false},
        {2, L"aside_text", L"OCR:", 0, true, false, true},
        {3, L"chart", L"Chart Recognition:", 0, true, true, false},
        {4, L"content", L"OCR:", 0, true, false, false},
        {5, L"display_formula", L"Formula Recognition:", 0, false, false, false},
        {6, L"doc_title", L"OCR:", 1, false, false, false},
        {7, L"figure_title", L"OCR:", 0, false, false, false},
        {8, L"footer", L"OCR:", 0, true, false, true},
        {9, L"footer_image", L"OCR:", 0, true, false, true},
        {10, L"footnote", L"OCR:", 0, false, false, false},
        {11, L"formula_number", L"OCR:", 0, false, false, false},
        {12, L"header", L"OCR:", 0, true, false, true},
        {13, L"header_image", L"OCR:", 0, true, false, true},
        {14, L"image", L"OCR:", 0, true, true, false},
        {15, L"inline_formula", L"Formula Recognition:", 0, false, false, false},
        {16, L"number", L"OCR:", 0, true, false, true},
        {17, L"paragraph_title", L"OCR:", 2, false, false, false},
        {18, L"reference", L"OCR:", 0, false, false, false},
        {19, L"reference_content", L"OCR:", 0, false, false, false},
        {20, L"seal", L"Seal Recognition:", 0, false, true, false},
        {21, L"table", L"Table Recognition:", 0, false, false, false},
        {22, L"text", L"OCR:", 0, false, false, false},
        {23, L"vertical_text", L"OCR:", 0, false, false, false},
        {24, L"vision_footnote", L"OCR:", 0, false, false, false},
    };
    return classId >= 0 && classId < (int)(sizeof(classes) / sizeof(classes[0]))
        ? &classes[classId]
        : nullptr;
}

namespace {

[[noreturn]] void Fail(const std::string& message) {
    fprintf(stderr, "FAIL: %s\n", message.c_str());
    fflush(stderr);
    std::exit(1);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

HBITMAP BuildLongDocumentFixture(HBITMAP bitmap) {
    Gdiplus::Bitmap source(bitmap, nullptr);
    if (source.GetLastStatus() != Gdiplus::Ok) return nullptr;
    const int width = static_cast<int>(source.GetWidth());
    const int sourceHeight = static_cast<int>(source.GetHeight());
    constexpr int kRepeatCount = 3;
    const int height = sourceHeight * kRepeatCount;
    Gdiplus::Bitmap longImage(width, height, PixelFormat32bppARGB);
    if (longImage.GetLastStatus() != Gdiplus::Ok) return nullptr;
    Gdiplus::Graphics graphics(&longImage);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    for (int index = 0; index < kRepeatCount; ++index) {
        if (graphics.DrawImage(&source, 0, index * sourceHeight, width, sourceHeight) !=
            Gdiplus::Ok) {
            return nullptr;
        }
    }
    HBITMAP result = nullptr;
    return longImage.GetHBITMAP(Gdiplus::Color(255, 255, 255), &result) == Gdiplus::Ok
        ? result : nullptr;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool longDocument = argc == 4 && wcscmp(argv[3], L"--long") == 0;
    const bool emptyOptionalArgument = argc == 4 && argv[3][0] == L'\0';
    if (argc != 3 && !longDocument && !emptyOptionalArgument) {
        std::cerr << "Expected model path, image path, and optional --long.\n";
        return 2;
    }

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok) {
        Fail("GDI+ startup failed");
    }

    std::wstring imageError;
    HBITMAP bitmap = ImageCodec::LoadHBitmapFromFile(argv[2], &imageError);
    Expect(bitmap != nullptr, "real sample image failed to load");
    if (longDocument) {
        HBITMAP longFixture = BuildLongDocumentFixture(bitmap);
        DeleteObject(bitmap);
        bitmap = longFixture;
        Expect(bitmap != nullptr, "failed to create long-document fixture");
    }
    BITMAP imageBitmap = {};
    Expect(GetObject(bitmap, sizeof(imageBitmap), &imageBitmap) == sizeof(imageBitmap),
        "real sample bitmap dimensions unavailable");
    const int width = imageBitmap.bmWidth;
    const int height = std::abs(imageBitmap.bmHeight);
    fprintf(stderr, "Loaded sample %dx%d\n", width, height);
    fflush(stderr);

    LayoutEngine engine;
    Expect(engine.Initialize(argv[1]), "layout model initialization failed");
    fprintf(stderr, "Initialized layout model\n");
    fflush(stderr);
    LayoutDetectionDiagnostics diagnostics;
    auto regions = engine.Detect(bitmap, &diagnostics);
    fprintf(stderr, "Detected %zu regions\n", regions.size());
    fflush(stderr);
    DeleteObject(bitmap);
    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (longDocument) {
        Expect(!regions.empty(), "long-document fixture must retain layout regions");
        Expect(diagnostics.tiledTriggered,
            "extreme-aspect fixture must execute the real tiled fallback");
        Expect(diagnostics.tiled.raw > 0,
            "real CropBitmap fixture must produce tile inference diagnostics");
        Expect(regions.size() >= diagnostics.full.finalCount,
            "tile fusion must not discard the full-result baseline");
    } else {
        Expect(regions.size() >= 15 && regions.size() <= 30,
            "official V3 profile should retain a plausible 15..30 block set");
    }
    Expect(diagnostics.modelPath == argv[1],
        "layout diagnostics retain the loaded model path");
    Expect(diagnostics.modelBytes > 0,
        "layout diagnostics retain the model byte size");
    Expect(diagnostics.modelSha256.size() == 64,
        "layout diagnostics retain a SHA-256 model fingerprint");
    Expect(diagnostics.modelSha256Error.empty(),
        "model fingerprint diagnostics do not report a hash failure");
    const int maxDimension = (std::max)(width, height);
    const int minDimension = (std::min)(width, height);
    const float aspect = minDimension > 0
        ? static_cast<float>(maxDimension) / minDimension : 1.0f;
    const float minScale = (std::min)(800.0f / width, 800.0f / height);
    if (!longDocument && aspect <= 3.0f && minScale >= 0.20f) {
        Expect(!diagnostics.tiledTriggered,
            "ordinary page must retain the official full-image layout path");
    }
    bool retainedLowScore = false;
    bool hasMaskPolygon = false;
    bool hasNonRectPolygon = false;
    bool hasGiantTextUnion = false;
    for (const auto& region : regions) {
        retainedLowScore |= region.confidence > 0.30f && region.confidence < 0.40f;
        hasMaskPolygon |= region.polygonFromMask;
        hasNonRectPolygon |= region.polygonFromMask && region.polygon.size() > 4;
        Expect(region.polygon.size() >= 4, "every region persists polygon geometry");
        if (region.classId == 22 || region.classId == 23 || region.classId == 4) {
            hasGiantTextUnion |=
                region.bbox.bottom - region.bbox.top > (LONG)(height * 0.50);
        }
        Expect(region.bbox.left >= 0 && region.bbox.top >= 0 &&
            region.bbox.right <= width && region.bbox.bottom <= height,
            "region bbox remains inside the page");
    }
    if (!longDocument) {
        Expect(retainedLowScore, "V3 scalar 0.30 retains a 0.30..0.40 sample block");
        Expect(hasMaskPolygon, "V3 masks are consumed by the real engine path");
        Expect(hasNonRectPolygon, "real sample preserves at least one non-rect mask polygon");
        Expect(!hasGiantTextUnion, "layout engine does not recreate the giant text union");
    }

    std::cout << "Paddle Doc layout engine integration passed: blocks="
              << regions.size() << " size=" << width << 'x' << height
              << (longDocument ? " long-document" : "") << "\n";
    return 0;
}
