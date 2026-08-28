#pragma once

#include "BatchOcrTypes.h"
#include "OcrUtils.h"

#include <string>
#include <vector>

// Asset bytes are published before their Markdown/manifest metadata. Keep the
// previous bytes until the owning writer has committed all metadata files.
struct BatchOcrAssetTransactionEntry {
    std::wstring targetPath;
    std::wstring backupPath;
    bool published = false;
};

struct BatchOcrAssetTransaction {
    std::vector<BatchOcrAssetTransactionEntry> entries;
    bool active = false;

    ~BatchOcrAssetTransaction();
    BatchOcrAssetTransaction() = default;
    BatchOcrAssetTransaction(const BatchOcrAssetTransaction&) = delete;
    BatchOcrAssetTransaction& operator=(const BatchOcrAssetTransaction&) = delete;
    BatchOcrAssetTransaction(BatchOcrAssetTransaction&& other) noexcept;
    BatchOcrAssetTransaction& operator=(BatchOcrAssetTransaction&& other) noexcept;
};

struct BatchOcrImageLinkRewriteResult {
    std::wstring markdown;
    std::vector<std::wstring> assets;
    // Absolute app-owned files. Populated for transient materialization so a
    // History record can release every cache dependency explicitly.
    std::vector<std::wstring> ownedFiles;
    BatchOcrAssetTransaction transaction;
    std::wstring error;
};

enum class OcrEmbeddedAssetReferenceKind {
    OutputRelative,
    LocalhostCache,
};

BatchOcrImageLinkRewriteResult MaterializeOcrEmbeddedAssets(
    const std::wstring& markdown,
    const std::wstring& canonicalSourceImagePath,
    const std::vector<OcrEmbeddedAssetSpec>& specs,
    const std::wstring& assetsDir,
    int pageIndex,
    const OcrOutputArtifactOptions& options,
    OcrEmbeddedAssetReferenceKind referenceKind,
    const std::wstring& transientAssetStem = L"");

BatchOcrImageLinkRewriteResult MaterializeTransientOcrEmbeddedAssets(
    const std::wstring& markdown,
    const std::wstring& canonicalSourceImagePath,
    const std::vector<OcrEmbeddedAssetSpec>& specs);

BatchOcrImageLinkRewriteResult RewriteOcrImageLinksForExport(
    const std::wstring& markdown,
    const std::wstring& outputAssetsDir,
    int pageIndex,
    const OcrOutputArtifactOptions& options = OcrOutputArtifactOptions(),
    int firstAssetIndex = 1);

void MergeOcrAssetTransactions(
    BatchOcrAssetTransaction& target,
    BatchOcrAssetTransaction&& source);
void CommitOcrAssetTransaction(BatchOcrAssetTransaction& transaction);
void RollbackOcrAssetTransaction(BatchOcrAssetTransaction& transaction);

// Call only after the page/image metadata commit succeeds. Removes older
// page-scoped asset variants and higher indexes that the committed Markdown
// no longer owns; it never scans or deletes outside assetsDir.
bool RemoveStaleOcrEmbeddedAssetFiles(
    const std::wstring& assetsDir,
    int pageIndex,
    const std::vector<std::wstring>& committedAssets,
    std::wstring& warning);
