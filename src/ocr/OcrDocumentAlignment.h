#pragma once

#include "OcrDocumentTypes.h"

#include <string>
#include <vector>

std::wstring CanonicalizeOcrMarkdownSource(const std::wstring& markdown);

bool BuildVerifiedBlockSourceMap(
    const std::wstring& canonicalMarkdown,
    const std::vector<OcrLayoutBlock>& blocks,
    std::vector<OcrBlockSourceMapEntry>& sourceMap,
    std::wstring& revisionSha256,
    OcrAlignmentState& semanticState,
    std::wstring& error);

bool ValidateOcrBlockSourceMap(
    const std::wstring& canonicalMarkdown,
    const std::vector<OcrLayoutBlock>& blocks,
    const std::vector<OcrBlockSourceMapEntry>& sourceMap,
    const std::wstring& revisionSha256,
    std::wstring& error);

bool ValidateDocumentPageGeometry(
    const OcrCoordinateSpaceMetadata& coordinateSpace,
    const std::vector<OcrLayoutBlock>& blocks,
    OcrAlignmentState& geometryState,
    std::wstring& error);

OcrAlignmentState ComputeOcrPageOverallAlignment(const OcrPageAlignmentStatus& status);

void RefreshOcrPageOverallAlignment(OcrPageAlignmentStatus& status);

void RefreshDocumentPageOverallAlignment(DocumentOcrPageResult& page);

bool IsDocumentPageInteractiveAlignmentVerified(const DocumentOcrPageResult& page);
