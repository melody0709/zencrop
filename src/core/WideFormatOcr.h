#pragma once

#include <format>
#include <string>

// Pure wide formatting for OCR/Document domains: IDs, stems, runtime keys, and URIs.

inline std::wstring WideFormatPageAssetId(int page, int assetOrder)
{
    return std::format(L"{}:{}", page, assetOrder);
}

inline std::wstring WideFormatPageIndexName(int pageIndex)
{
    return std::format(L"page_{:04d}", pageIndex);
}

inline std::wstring WideFormatImageIndexName(int index)
{
    return std::format(L"image_{:03d}", index);
}

inline std::wstring WideFormatPageAssetStem(int pageIndex, int assetIndex)
{
    return std::format(L"page_{:04d}_img_{:03d}", pageIndex, assetIndex);
}

inline std::wstring WideFormatPageAssetPrefix(int pageIndex)
{
    return std::format(L"page_{:04d}_img_", pageIndex);
}

inline std::wstring WideFormatTiffPagePrefix(unsigned pageOneBased)
{
    return std::format(L"tiff_p{:04d}", pageOneBased);
}

inline std::wstring WideFormatOcrVirtualStem(
    int hour, int minute, int second, int milliseconds, unsigned seq)
{
    return std::format(L"ocr_virtual_{:02d}{:02d}{:02d}_{:03d}_{:03d}",
        hour, minute, second, milliseconds, seq);
}

inline std::wstring WideFormatImageRuntimeKeyPrefix(int index)
{
    return std::format(L"image:runtime:{}:", index);
}

inline std::wstring WideFormatPdfRuntimeKey(int index)
{
    return std::format(L"pdf:runtime:{}", index);
}

inline std::wstring WideFormatPageMetaSuffix(int pageIndex, const std::wstring& elapsed)
{
    return std::format(L"P{} · {}", pageIndex, elapsed);
}

inline std::wstring WideFormatPageMetaLive(int pageOneBased, const std::wstring& elapsed)
{
    return std::format(L"P{} · {}", pageOneBased, elapsed);
}

inline std::wstring WideFormatPageBlockId(int pageOneBased, int order)
{
    return std::format(L"page_{}:block_{}", pageOneBased, order);
}

inline std::wstring WideFormatPageBboxId(int n)
{
    return std::format(L"page_1:bbox_{}", n);
}

inline std::wstring WideFormatPageLayoutAssetId(int regionOneBased)
{
    return std::format(L"page_1:layout_{}:asset", regionOneBased);
}

inline std::wstring WideFormatPageProviderAssetId(int pageNumber, int localOrder)
{
    return std::format(L"page_{}:provider_asset_{}", pageNumber, localOrder);
}

inline std::wstring WideFormatPageKindOrderId(
    int pageNumber, const wchar_t* kind, int order)
{
    return std::format(L"page_{}:{}_{}", pageNumber, kind ? kind : L"", order);
}

inline std::wstring WideFormatProviderAssetUri(int pageNumber, int localOrder)
{
    return std::format(L"zencrop-asset://provider/page_{}/asset_{}", pageNumber, localOrder);
}

inline std::wstring WideFormatPageId(int pageNumber)
{
    return std::format(L"page_{}", pageNumber);
}

inline std::wstring WideFormatAssetId(int order)
{
    return std::format(L"asset_{}", order);
}

inline std::wstring WideFormatProviderAssetLocal(int order)
{
    return std::format(L"provider_asset_{}", order);
}

inline std::wstring WideFormatBlockId(int order)
{
    return std::format(L"block_{}", order);
}

inline std::wstring WideFormatPagePrefix(int pageNumber)
{
    return std::format(L"page_{}:", pageNumber);
}

inline std::wstring WideFormatAnnIdPlain(unsigned long long id)
{
    return std::format(L"ann_{}", id);
}

inline std::wstring WideFormatLegacyId(int index)
{
    return std::format(L"legacy_{}", index);
}

inline std::wstring WideFormatGroupId(int order)
{
    return std::format(L"group_{}", order);
}

inline std::wstring WideFormatOcrFpSuffix(const std::wstring& stableKey, int pageIndex)
{
    return std::format(L"OCR:{}:{}|", stableKey, pageIndex);
}

inline std::wstring WideFormatExProgressPrefix(int progressId)
{
    return std::format(L"EX:{}:", progressId);
}
