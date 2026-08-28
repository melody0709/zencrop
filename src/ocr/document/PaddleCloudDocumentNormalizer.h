#pragma once

#include "OcrDocumentTypes.h"

#include <string>
#include <vector>

struct PaddleCloudDocumentNormalizeOptions {
    // Must be enabled only by a Gate-0-verified provider profile.
    bool allowStrictOrdinalFallback = false;
    bool serverRestructureEnabled = false;
    std::wstring model = L"PaddleOCR-VL-1.6";
};

bool NormalizePaddleCloudDocumentJsonl(
    const std::wstring& jsonl,
    const std::vector<int>& requestedOriginalPageNumbers,
    const PaddleCloudDocumentNormalizeOptions& options,
    DocumentOcrResult& result);

std::wstring BuildCanonicalCloudPageRanges(const std::vector<int>& pages);

bool ValidatePaddleCloudJsonObjectSyntax(
    const std::wstring& json,
    std::wstring& error);
