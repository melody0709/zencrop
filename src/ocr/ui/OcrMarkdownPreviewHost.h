#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "DashboardSourceMap.h"

class OcrMarkdownPreviewHost {
public:
    struct StructuredSelectionRequest {
        std::wstring token;
        uint64_t generation = 0;
        std::wstring format;
        std::wstring payload;
        std::wstring sourceUrl;
        uint64_t selectionGeneration = 0;
        bool previewSelection = false;
    };

    struct PreviewBlock {
        std::wstring id;
        int pageIndex = 0;
        int order = 0;
        std::wstring label;
        std::wstring displayLabel;
        std::wstring content;
        std::wstring groupId;
        std::wstring contentOwnerId;
        bool edited = false;
        bool canRestoreOriginal = false;
        bool editable = true;
    };

    struct PreviewContentMetrics {
        int scrollHeight = 0;
        int scrollWidth = 0;
        int clientWidth = 0;
        std::wstring renderToken;
    };

    struct Callbacks {
        std::function<void()> onReady;
        std::function<void(const PreviewContentMetrics&)> onContentMetrics;
        std::function<void(double)> onZoomFactorChanged;
        std::function<void(const std::wstring&)> onUnavailable;
        std::function<void(int, const std::wstring&)> onRenderError;
        std::function<void(int, const std::wstring&)> onImageLoadError;
        std::function<void()> onProcessFailed;
        std::function<void(const std::wstring&)> onOpenExternal;
        std::function<bool(UINT, bool)> onAcceleratorKey;
        std::function<void(bool, bool, bool, bool, bool)> onPreviewEditorState;
        std::function<void(bool)> onPreviewDocumentEdit;
        std::function<void(const std::wstring&, const std::wstring&)> onPreviewDocumentSave;
        std::function<void()> onPreviewDocumentCancel;
        std::function<void(const std::wstring&)> onPreviewBlockHover;
        std::function<void(const std::wstring&)> onPreviewBlockSelect;
        std::function<void(const std::wstring&)> onPreviewBlockEdit;
        std::function<void(const std::wstring&, const std::wstring&, const DashboardSourceEditRequest&, const std::wstring&)> onPreviewBlockSave;
        std::function<void(const std::wstring&, const DashboardSourceEditRequest&, const std::wstring&)> onPreviewBlockRestore;
        std::function<void(const std::wstring&)> onPreviewBlockCancel;
        std::function<void(bool, uint64_t)> onPreviewSelectionState;
        std::function<void(const std::wstring&, uint64_t, bool,
            const std::wstring&, const std::wstring&)>
            onStructuredSelectionPrepared;
    };

    OcrMarkdownPreviewHost();
    ~OcrMarkdownPreviewHost();

    bool Create(HWND parent, const RECT& bounds, Callbacks callbacks);
    void Destroy();
    void SetBounds(const RECT& bounds);
    void SetZoomFactor(double zoomFactor);
    void SetTextFontSize(int fontSize);
    void Show(bool visible);
    void SetVerticalScrollbarBoundaryHover(bool hovered);
    void SetLocalAssetRoot(const std::wstring& root);
    void RenderMarkdown(int recordId, const std::wstring& markdown, bool compactLayout = false);
    void RenderTransientMarkdown(int recordId, const std::wstring& markdown, bool compactLayout = false);
    void RenderMarkdownBlocks(
        int recordId,
        const std::wstring& markdown,
        const std::vector<PreviewBlock>& blocks,
        const std::wstring& sourceMarkdown = L"");
    void SetHoveredBlock(const std::wstring& id);
    void SetSelectedBlock(const std::wstring& id, bool ensureVisible);
    void SetEditingBlock(const std::wstring& id);
    void StartDocumentEditing();
    void RequestActiveEditorSave();
    void CancelActiveEditor();
    void PostPreviewBlockSaveResult(
        const std::wstring& id,
        const std::wstring& renderToken,
        bool success,
        const std::wstring& errorCode = L"");
    void PostPreviewBlockRestoreResult(
        const std::wstring& id,
        const std::wstring& renderToken,
        bool success,
        const std::wstring& errorCode = L"");
    void PostPreviewDocumentSaveResult(
        const std::wstring& renderToken,
        bool success,
        const std::wstring& errorCode = L"");
    bool PrepareStructuredSelection(const StructuredSelectionRequest& request);
    void CancelStructuredSelection(
        const std::wstring& token, uint64_t generation,
        const std::wstring& errorCode = L"cancelled");

    bool IsReady() const;
    bool IsAvailable() const;
    bool IsCreating() const;
    bool HasActiveEditor() const;
    bool HasDirtyEditor() const;
    bool IsEditorComposing() const;
    bool CanSaveActiveEditor() const;
    bool IsEditorActionPending() const;

#ifdef ZENCROP_PREVIEW_HOST_TESTS
    static bool RunStaticContractForTests(std::wstring& error);
    bool ExecuteScriptForTests(
        const std::wstring& script,
        std::function<void(bool, const std::wstring&)> callback);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
