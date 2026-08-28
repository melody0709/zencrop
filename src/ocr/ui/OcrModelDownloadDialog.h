#pragma once

#include "ocr/model_download/OcrModelDownloadTypes.h"

#include <windows.h>

#include <string>

bool ShowOcrModelDownloadDialog(
    HWND owner,
    OcrModelBundleId initialBundle,
    const std::wstring& initialRoot,
    OcrModelInstallResult& installResult);

