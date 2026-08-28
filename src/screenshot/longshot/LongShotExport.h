#pragma once

#include "LongShotImage.h"
#include "Settings.h"

#include <atomic>
#include <functional>
#include <string>

namespace longshot {

// Encodes LongShotImage tiles without first flattening the complete image in
// memory for the WIC-backed formats. The destination is replaced only after a
// successful, non-cancelled encode.
bool SaveLongShotImageToFile(
    const LongShotImage& image,
    const std::wstring& destinationPath,
    ScreenshotFormat format,
    int jpegQuality,
    const std::atomic<bool>* cancel,
    const std::function<void(int)>& progress,
    std::wstring* error);

} // namespace longshot
