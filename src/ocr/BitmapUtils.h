#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

void GetBitmapBits32(HBITMAP hBitmap, int& width, int& height, std::vector<uint8_t>& pixels);
std::vector<unsigned char> HBitmapToPng(HBITMAP hBitmap);
std::vector<unsigned char> HBitmapToJpeg(HBITMAP hBitmap, long quality = 95);
std::vector<unsigned char> DecodeBase64Image(const std::string& base64Data);
HBITMAP CropBitmap(HBITMAP hSrc, RECT rect);
