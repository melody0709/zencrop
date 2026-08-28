#pragma once

#include "OcrModelDownloadTypes.h"

#include <string>
#include <vector>

const std::vector<OcrModelBundleSpec>& OcrModelDownloadCatalog();
const OcrModelBundleSpec* OcrModelDownloadFindBundle(OcrModelBundleId id);
const OcrModelBundleSpec* OcrModelDownloadFindBundle(const std::wstring& stableId);
std::uint64_t OcrModelDownloadBundleBytes(const OcrModelBundleSpec& bundle);
bool OcrModelDownloadValidateCatalog(std::wstring& error);
OcrModelInstallResult OcrModelDownloadBuildInstallResult(
    OcrModelBundleId bundle,
    const std::wstring& modelRoot);

