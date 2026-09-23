#pragma once

#include "OcrMarkdownPreviewHost.h"

#include <string>
#include <vector>

namespace OcrMarkdownPreviewProtocol {

std::wstring EscapeJson(const std::wstring& value);
void AppendJsonStringField(std::wstring& json, const wchar_t* name, const std::wstring& value);
void AppendPreviewBlocksJson(std::wstring& json, const std::vector<OcrMarkdownPreviewHost::PreviewBlock>& blocks);
std::wstring BuildRenderMessage(int recordId, const std::wstring& markdown, const std::wstring& sourceMarkdown, const std::wstring& revisionSha256, const std::vector<OcrMarkdownPreviewHost::PreviewBlock>& blocks, const std::wstring& hoveredBlockId, const std::wstring& selectedBlockId, const std::wstring& editingBlockId, const std::wstring& renderToken, bool compactLayout = false);
std::wstring BuildTransientRenderMessage(int recordId, const std::wstring& markdown, const std::wstring& renderToken, bool compactLayout = false);
std::wstring BuildBlockState(const wchar_t* type, const std::wstring& id, bool ensureVisible);
std::wstring BuildBlockSaveResult(const std::wstring& id, const std::wstring& renderToken, bool success, const std::wstring& errorCode);
std::wstring BuildBlockRestoreResult(const std::wstring& id, const std::wstring& renderToken, bool success, const std::wstring& errorCode);
std::wstring BuildDocumentSaveResult(const std::wstring& renderToken, bool success, const std::wstring& errorCode);

} // namespace OcrMarkdownPreviewProtocol
