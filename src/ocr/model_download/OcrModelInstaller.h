#pragma once

#include "OcrModelDownloadTypes.h"

#include <cstdint>
#include <string>

class OcrModelInstaller final {
public:
    explicit OcrModelInstaller(std::wstring runtimeTemplateDir);

    bool PrepareTarget(
        const std::wstring& modelRoot,
        std::uint64_t requiredBytes,
        OcrModelDownloadError& error) const;

    std::wstring StagingPath(
        const std::wstring& modelRoot,
        const OcrModelArtifactSpec& artifact) const;

    bool PrepareStaging(
        const std::wstring& modelRoot,
        const OcrModelArtifactSpec& artifact,
        std::wstring& stagingPath,
        OcrModelDownloadError& error) const;

    bool ArtifactInstalled(
        const std::wstring& modelRoot,
        const OcrModelArtifactSpec& artifact,
        bool verifyContent = false) const;

    bool VerifyStaging(
        const OcrModelArtifactSpec& artifact,
        const std::wstring& stagingPath,
        OcrModelDownloadError& error) const;

    bool InstallArtifact(
        const std::wstring& modelRoot,
        const OcrModelArtifactSpec& artifact,
        const std::wstring& stagingPath,
        OcrModelDownloadError& error) const;

    bool FinalizeBundle(
        const std::wstring& modelRoot,
        const OcrModelBundleSpec& bundle,
        OcrModelDownloadError& error) const;

    bool BundleInstalled(
        const std::wstring& modelRoot,
        const OcrModelBundleSpec& bundle,
        bool verifyContent = false) const;

private:
    bool InstallLlamaRuntime(
        const std::wstring& modelRoot,
        const OcrModelArtifactSpec& artifact,
        const std::wstring& archivePath,
        OcrModelDownloadError& error) const;
    bool InstallPpOcrV6Metadata(
        const std::wstring& modelRoot,
        const std::wstring& variant,
        OcrModelDownloadError& error) const;
    bool LlamaRuntimeInstalled(
        const std::wstring& modelRoot,
        const OcrModelArtifactSpec& artifact) const;

    std::wstring runtimeTemplateDir_;
};

std::wstring OcrModelDefaultDownloadRoot();
std::wstring OcrModelRuntimeTemplateDirectory();
