#include "ocr/engine/PaddleDocRecognitionImage.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

HBITMAP CreateBitmap32(int width, int height, std::array<uint8_t, 4> bgra) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* rawBits = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        nullptr, &info, DIB_RGB_COLORS, &rawBits, nullptr, 0);
    Expect(bitmap != nullptr && rawBits != nullptr, "CreateDIBSection failed");
    auto* bits = static_cast<uint8_t*>(rawBits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::memcpy(bits + ((size_t)y * width + x) * 4, bgra.data(), 4);
        }
    }
    return bitmap;
}

struct BitmapView {
    uint8_t* bits = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

BitmapView View(HBITMAP bitmap) {
    DIBSECTION section = {};
    Expect(GetObject(bitmap, sizeof(section), &section) == sizeof(section),
        "GetObject DIBSECTION failed");
    BitmapView view;
    view.bits = static_cast<uint8_t*>(section.dsBm.bmBits);
    view.width = section.dsBm.bmWidth;
    view.height = std::abs(section.dsBm.bmHeight);
    view.stride = section.dsBm.bmWidthBytes;
    Expect(view.bits != nullptr && view.width > 0 && view.height > 0,
        "bitmap view unavailable");
    return view;
}

std::array<uint8_t, 4> Pixel(HBITMAP bitmap, int x, int y) {
    const auto view = View(bitmap);
    Expect(x >= 0 && x < view.width && y >= 0 && y < view.height,
        "pixel coordinate outside bitmap");
    const uint8_t* pixel = view.bits + (size_t)y * view.stride + x * 4;
    return { pixel[0], pixel[1], pixel[2], pixel[3] };
}

void FillRect(
    HBITMAP bitmap,
    RECT rect,
    std::array<uint8_t, 4> bgra)
{
    auto view = View(bitmap);
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            std::memcpy(view.bits + (size_t)y * view.stride + x * 4,
                bgra.data(), 4);
        }
    }
}

LayoutRegion Region(RECT box) {
    LayoutRegion region;
    region.bbox = box;
    region.className = L"text";
    region.classId = 22;
    region.vlmPrompt = L"OCR:";
    return region;
}

void ExpectDimensions(HBITMAP bitmap, int width, int height, const std::string& message) {
    const auto view = View(bitmap);
    Expect(view.width == width && view.height == height,
        message + " actual=" + std::to_string(view.width) + "x" +
        std::to_string(view.height));
}

void TestExactExclusiveCrop() {
    HBITMAP source = CreateBitmap32(10, 10, { 10, 20, 30, 255 });
    FillRect(source, RECT{ 2, 3, 3, 4 }, { 40, 50, 60, 255 });
    auto region = Region(RECT{ 2, 3, 7, 9 });
    PaddleDocRecognitionImageStats stats;
    HBITMAP crop = CropPaddleDocRecognitionRegion(source, region, false, &stats);
    Expect(crop != nullptr, "exact crop failed");
    ExpectDimensions(crop, 5, 6, "right/bottom-exclusive crop size");
    Expect(Pixel(crop, 0, 0) == std::array<uint8_t, 4>{ 40, 50, 60, 255 },
        "crop origin maps exactly to bbox left/top");
    Expect(stats.sourceRect.left == 2 && stats.sourceRect.top == 3 &&
        stats.sourceRect.right == 7 && stats.sourceRect.bottom == 9,
        "official crop does not add PAD8");
    DeleteObject(crop);
    DeleteObject(source);
}

void TestPolygonOutsideWhiteAndTruncation() {
    HBITMAP source = CreateBitmap32(10, 10, { 10, 20, 30, 255 });
    auto region = Region(RECT{ 0, 0, 10, 10 });
    region.polygonFromMask = true;
    region.polygon = {
        { 2.9f, 2.9f }, { 7.9f, 2.9f },
        { 7.9f, 7.9f }, { 2.9f, 7.9f },
    };
    PaddleDocRecognitionImageStats stats;
    HBITMAP crop = CropPaddleDocRecognitionRegion(source, region, false, &stats);
    Expect(crop != nullptr && stats.polygonApplied && !stats.polygonFallback,
        "valid polygon mask was not applied");
    Expect(Pixel(crop, 1, 1) == std::array<uint8_t, 4>{ 255, 255, 255, 255 },
        "polygon exterior is white in all channels");
    Expect(Pixel(crop, 2, 2) == std::array<uint8_t, 4>{ 10, 20, 30, 255 },
        "polygon float points truncate toward zero before fill");
    DeleteObject(crop);

    region.polygon = {
        { 0, 0 }, { 9, 9 }, { 0, 9 }, { 9, 0 },
    };
    crop = CropPaddleDocRecognitionRegion(source, region, false, &stats);
    Expect(crop != nullptr && stats.polygonFallback,
        "self-intersecting polygon does not silently mask pixels");
    Expect(Pixel(crop, 9, 9) == std::array<uint8_t, 4>{ 10, 20, 30, 255 },
        "invalid polygon safely falls back to exact rectangle");
    DeleteObject(crop);
    DeleteObject(source);
}

void TestFormulaMargin() {
    HBITMAP source = CreateBitmap32(10, 10, { 255, 255, 255, 255 });
    FillRect(source, RECT{ 2, 3, 7, 8 }, { 0, 0, 0, 255 });
    auto formula = Region(RECT{ 0, 0, 10, 10 });
    formula.className = L"display_formula";
    formula.classId = 5;
    formula.vlmPrompt = L"Formula Recognition:";
    PaddleDocRecognitionImageStats stats;
    HBITMAP crop = CropPaddleDocRecognitionRegion(source, formula, true, &stats);
    Expect(crop != nullptr && stats.formulaMarginApplied,
        "formula white-margin crop was not applied");
    ExpectDimensions(crop, 5, 5, "formula margin boundingRect");
    DeleteObject(crop);
    DeleteObject(source);

    source = CreateBitmap32(10, 10, { 255, 255, 255, 255 });
    FillRect(source, RECT{ 2, 3, 7, 8 }, { 200, 200, 200, 255 });
    FillRect(source, RECT{ 4, 4, 5, 5 }, { 0, 0, 0, 255 });
    FillRect(source, RECT{ 0, 0, 1, 1 }, { 201, 201, 201, 255 });
    crop = CropPaddleDocRecognitionRegion(source, formula, true, &stats);
    Expect(crop != nullptr && stats.formulaMarginApplied,
        "formula threshold-200 boundary crop failed");
    ExpectDimensions(crop, 5, 5,
        "normalized gray 200 is foreground while 201 stays outside");
    DeleteObject(crop);
    DeleteObject(source);

    source = CreateBitmap32(10, 10, { 255, 255, 255, 255 });
    crop = CropPaddleDocRecognitionRegion(source, formula, true, &stats);
    Expect(crop != nullptr && stats.formulaMarginFallback,
        "constant formula image should keep original crop");
    ExpectDimensions(crop, 10, 10, "constant formula fallback size");
    DeleteObject(crop);
    DeleteObject(source);

    source = CreateBitmap32(10, 10, { 255, 255, 255, 255 });
    FillRect(source, RECT{ 2, 2, 4, 8 }, { 0, 0, 0, 255 });
    crop = CropPaddleDocRecognitionRegion(source, formula, true, &stats);
    Expect(crop != nullptr && stats.formulaMarginFallback,
        "formula crop with one dimension <=2 falls back");
    ExpectDimensions(crop, 10, 10, "thin formula fallback size");
    DeleteObject(crop);
    DeleteObject(source);
}

void TestWhiteCanvasCompositionUsesSourceCropsOnly() {
    HBITMAP source = CreateBitmap32(12, 8, { 255, 255, 255, 255 });
    FillRect(source, RECT{ 0, 0, 4, 3 }, { 0, 0, 255, 255 });
    FillRect(source, RECT{ 6, 4, 12, 6 }, { 255, 0, 0, 255 });
    FillRect(source, RECT{ 4, 0, 6, 8 }, { 0, 255, 0, 255 });
    std::vector<LayoutRegion> regions = {
        Region(RECT{ 0, 0, 4, 3 }),
        Region(RECT{ 6, 4, 12, 6 }),
    };
    PaddleDocRecognitionGroup group;
    group.regionIndices = { 0, 1 };
    group.alignments = { PaddleDocGroupAlignment::Center };
    group.contentOwnerIndex = 0;
    group.prompt = L"OCR:";
    PaddleDocRecognitionImageStats stats;
    HBITMAP composed = ComposePaddleDocRecognitionGroup(
        source, regions, group, &stats);
    Expect(composed != nullptr, "recognition group composition failed");
    ExpectDimensions(composed, 6, 5, "vertical composition dimensions");
    Expect(Pixel(composed, 0, 0) ==
        std::array<uint8_t, 4>{ 255, 255, 255, 255 },
        "center composition leaves white canvas margin");
    Expect(Pixel(composed, 1, 0) ==
        std::array<uint8_t, 4>{ 0, 0, 255, 255 },
        "first crop is centered on final canvas");
    Expect(Pixel(composed, 0, 3) ==
        std::array<uint8_t, 4>{ 255, 0, 0, 255 },
        "second crop is pasted immediately below first");
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 6; ++x) {
            Expect(Pixel(composed, x, y) !=
                std::array<uint8_t, 4>{ 0, 255, 0, 255 },
                "composition leaked pixels from union-bbox gap");
        }
    }
    Expect(!stats.legacyPad8Union && stats.memberCount == 2,
        "official composition does not use legacy union path");
    DeleteObject(composed);
    DeleteObject(source);
}

void TestLegacyPad8AbIsExplicit() {
    HBITMAP source = CreateBitmap32(30, 30, { 255, 255, 255, 255 });
    std::vector<LayoutRegion> regions = { Region(RECT{ 10, 10, 20, 20 }) };
    PaddleDocRecognitionGroup group;
    group.regionIndices = {0};
    group.contentOwnerIndex = 0;
    group.prompt = L"OCR:";
    group.useLegacyUnionCrop = true;
    PaddleDocRecognitionImageStats stats;
    HBITMAP crop = ComposePaddleDocRecognitionGroup(source, regions, group, &stats);
    Expect(crop != nullptr && stats.legacyPad8Union,
        "legacy PAD8 A/B must require the explicit legacy group flag");
    ExpectDimensions(crop, 26, 26, "legacy PAD8 crop dimensions");
    DeleteObject(crop);
    DeleteObject(source);
}

} // namespace

int main() {
    TestExactExclusiveCrop();
    TestPolygonOutsideWhiteAndTruncation();
    TestFormulaMargin();
    TestWhiteCanvasCompositionUsesSourceCropsOnly();
    TestLegacyPad8AbIsExplicit();
    std::cout << "Paddle Doc recognition image contract passed.\n";
    return 0;
}
