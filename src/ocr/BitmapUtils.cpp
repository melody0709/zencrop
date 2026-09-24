#include "BitmapUtils.h"
#include "core/GdiHandles.h"
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <climits>

void GetBitmapBits32(HBITMAP hBitmap, int& width, int& height, std::vector<uint8_t>& pixels) {
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);
    width = bmp.bmWidth;
    height = bmp.bmHeight;
    pixels.resize(width * height * 4);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    zencrop::ScopedWindowDC hDC(nullptr, GetDC(nullptr));
    GetDIBits(hDC.get(), hBitmap, 0, height, pixels.data(), &bi, DIB_RGB_COLORS);
}

std::vector<unsigned char> DecodeBase64Image(const std::string& base64Data) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> decoded;
    int val = 0, bits = 0;
    for (char c : base64Data) {
        if (c == '=') break;
        size_t pos = chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + (int)pos;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back((val >> bits) & 0xFF);
        }
    }

    return decoded;
}

std::vector<unsigned char> HBitmapToPng(HBITMAP hBitmap) {
    std::vector<unsigned char> pngData;

    Gdiplus::Bitmap bmp(hBitmap, nullptr);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        return pngData;
    }

    CLSID pngClsid;
    CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);

    if (bmp.Save(stream, &pngClsid, nullptr) == Gdiplus::Ok) {
        STATSTG stat;
        stream->Stat(&stat, STATFLAG_NONAME);
        LARGE_INTEGER zero = {};
        stream->Seek(zero, STREAM_SEEK_SET, nullptr);

        SIZE_T size = (SIZE_T)stat.cbSize.QuadPart;
        pngData.resize(size);
        ULONG read = 0;
        stream->Read(pngData.data(), (ULONG)size, &read);
    }

    stream->Release();
    return pngData;
}

std::vector<unsigned char> HBitmapToJpeg(HBITMAP hBitmap, long quality) {
    std::vector<unsigned char> jpegData;
    if (!hBitmap) return jpegData;

    Gdiplus::Bitmap source(hBitmap, nullptr);
    if (source.GetLastStatus() != Gdiplus::Ok ||
        source.GetWidth() == 0 || source.GetHeight() == 0) {
        return jpegData;
    }

    // JPEG has no alpha channel. Render onto an explicit white canvas so
    // transparent OCR inputs never acquire a black background.
    Gdiplus::Bitmap opaque(source.GetWidth(), source.GetHeight(), PixelFormat24bppRGB);
    if (opaque.GetLastStatus() != Gdiplus::Ok) return jpegData;
    {
        Gdiplus::Graphics graphics(&opaque);
        if (graphics.GetLastStatus() != Gdiplus::Ok) return jpegData;
        graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
        if (graphics.DrawImage(
                &source,
                0,
                0,
                static_cast<INT>(source.GetWidth()),
                static_cast<INT>(source.GetHeight())) != Gdiplus::Ok) {
            return jpegData;
        }
    }

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        return jpegData;
    }

    CLSID jpegClsid;
    CLSIDFromString(L"{557CF401-1A04-11D3-9A73-0000F81EF32E}", &jpegClsid);

    quality = (std::max)(1L, (std::min)(100L, quality));
    Gdiplus::EncoderParameters encParams = {};
    encParams.Count = 1;
    encParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
    encParams.Parameter[0].NumberOfValues = 1;
    encParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    encParams.Parameter[0].Value = &quality;

    if (opaque.Save(stream, &jpegClsid, &encParams) == Gdiplus::Ok) {
        STATSTG stat = {};
        if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) ||
            stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > ULONG_MAX) {
            stream->Release();
            return jpegData;
        }
        LARGE_INTEGER zero = {};
        if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) {
            stream->Release();
            return jpegData;
        }

        SIZE_T size = (SIZE_T)stat.cbSize.QuadPart;
        jpegData.resize(size);
        ULONG read = 0;
        if (FAILED(stream->Read(jpegData.data(), (ULONG)size, &read)) || read != size) {
            jpegData.clear();
        }
    }

    stream->Release();
    return jpegData;
}

HBITMAP CropBitmap(HBITMAP hSrc, RECT rect) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return nullptr;

    zencrop::ScopedWindowDC hSrcDC(nullptr, GetDC(nullptr));
    zencrop::ScopedDC hSrcMem(CreateCompatibleDC(hSrcDC.get()));
    zencrop::ScopedSelectObject selectSrc(hSrcMem.get(), hSrc);

    zencrop::ScopedDC hDstMem(CreateCompatibleDC(hSrcDC.get()));
    HBITMAP hDstBmp = CreateCompatibleBitmap(hSrcDC.get(), w, h);
    if (!hDstBmp) return nullptr;
    zencrop::ScopedSelectObject selectDst(hDstMem.get(), hDstBmp);

    BitBlt(hDstMem.get(), 0, 0, w, h, hSrcMem.get(), rect.left, rect.top, SRCCOPY);

    return hDstBmp;
}
