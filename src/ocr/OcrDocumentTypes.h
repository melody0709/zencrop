#pragma once

#include "OcrBlock.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class DocumentOcrTransportState {
    NotSubmitted,
    Submitting,
    Pending,
    Running,
    Downloading,
    Normalizing,
    Materializing,
    Completed,
    Failed,
    Expired,
    Detached,
    FallbackPending,
    Unknown
};

enum class OcrAlignmentState {
    NotChecked,
    Verified,
    TextOnlyWarning,
    Unresolved,
    Ambiguous,
    Failed
};

enum class OcrBlockSourceRelation {
    Direct,
    Alias,
    LayoutOnly,
    Unresolved,
    Ambiguous
};

const wchar_t* DocumentOcrTransportStateToString(DocumentOcrTransportState state);
DocumentOcrTransportState DocumentOcrTransportStateFromString(const std::wstring& value);
const wchar_t* OcrAlignmentStateToString(OcrAlignmentState state);
OcrAlignmentState OcrAlignmentStateFromString(const std::wstring& value);
const wchar_t* OcrBlockSourceRelationToString(OcrBlockSourceRelation relation);
OcrBlockSourceRelation OcrBlockSourceRelationFromString(const std::wstring& value);

struct OcrBlockSourceMapEntry {
    std::wstring blockId;
    OcrBlockSourceRelation relation = OcrBlockSourceRelation::Unresolved;
    std::wstring contentOwnerId;
    // UTF-16 code-unit offsets into canonicalSourceMarkdown. -1 means absent.
    int64_t sourceStart = -1;
    int64_t sourceEnd = -1;
    std::wstring sourceRevisionSha256;
    std::wstring reason;
};

struct OcrCoordinateSpaceMetadata {
    std::wstring canonicalImageKind;
    std::wstring canonicalImagePath;
    std::wstring canonicalImageSha256;
    uint32_t canonicalImageWidth = 0;
    uint32_t canonicalImageHeight = 0;
    uint32_t recognitionImageWidth = 0;
    uint32_t recognitionImageHeight = 0;
    int rotationDegrees = 0;
    std::wstring origin = L"top_left";
    std::wstring bboxConvention = L"xyxy_half_open";
    std::wstring polygonConvention = L"ordered_pixel_points";
    std::wstring coordinateSpaceKind;
    bool transformVerified = false;
    // Affine transform from recognition coordinates to canonical image:
    // x' = m0*x + m2*y + m4, y' = m1*x + m3*y + m5.
    std::array<double, 6> recognitionToCanonical = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    std::wstring warning;
};

struct OcrPageAlignmentStatus {
    OcrAlignmentState pageIdentity = OcrAlignmentState::NotChecked;
    OcrAlignmentState geometry = OcrAlignmentState::NotChecked;
    OcrAlignmentState semantic = OcrAlignmentState::NotChecked;
    OcrAlignmentState overall = OcrAlignmentState::NotChecked;
    std::wstring reason;
};

struct DocumentOcrResourceDescriptor {
    std::wstring kind;
    std::wstring remoteUrl;
    std::wstring localPath;
    std::wstring sha256;
    uint64_t byteSize = 0;
    bool required = false;
};

struct DocumentOcrPageResult {
    // Original source PDF page number is always 1-based.
    int originalPageNumber = 0;
    // Ordinal in the fully flattened provider result, always 0-based.
    int resultOrdinal = -1;
    bool originalPageNumberExplicit = false;
    std::wstring stablePageId;
    std::wstring markdown;
    std::wstring plainText;
    std::wstring canonicalSourceMarkdown;
    std::wstring sourceRevisionSha256;
    std::wstring rawJson;
    std::vector<OcrLayoutBlock> blocks;
    std::vector<OcrBlockSourceMapEntry> blockSourceMap;
    OcrCoordinateSpaceMetadata coordinateSpace;
    OcrPageAlignmentStatus alignment;
    std::vector<DocumentOcrResourceDescriptor> resources;
    std::wstring warning;
};

struct DocumentOcrResult {
    std::wstring provider;
    std::wstring model;
    std::wstring rawJsonlSha256;
    std::vector<int> requestedOriginalPageNumbers;
    std::vector<DocumentOcrPageResult> pages;
    std::vector<std::wstring> warnings;
    std::wstring error;
    bool success = false;
};

// Stage3 3-A-2: remote document job metadata sole in document package.
// Batch/Transport reuse this type; document no longer includes batch headers for it.
struct DocumentOcrRemoteJob {
    std::wstring provider;
    std::wstring model;
    std::wstring jobId;
    std::wstring batchId;
    DocumentOcrTransportState state = DocumentOcrTransportState::NotSubmitted;
    std::vector<int> requestedPageNumbers;
    std::wstring pageRanges;
    std::wstring requestFingerprint;
    // SHA-256 of the complete normalized provider JSONL input. This binds
    // idempotent page materialization to one immutable remote result.
    std::wstring resultSha256;
    std::wstring submittedAtUtc;
    std::wstring lastPollAtUtc;
    int attempt = 0;
    std::wstring diagnosticCode;
    std::wstring diagnosticMessage;
};

std::wstring OcrBlockSourceMapToJson(
    const std::vector<OcrBlockSourceMapEntry>& entries,
    int indent = 2);

std::vector<OcrBlockSourceMapEntry> ParseOcrBlockSourceMap(const std::wstring& json);

std::wstring OcrCoordinateSpaceToJson(
    const OcrCoordinateSpaceMetadata& metadata,
    int indent = 2);

OcrCoordinateSpaceMetadata ParseOcrCoordinateSpace(const std::wstring& json);

std::wstring OcrPageAlignmentToJson(
    const OcrPageAlignmentStatus& status,
    int indent = 2);

OcrPageAlignmentStatus ParseOcrPageAlignment(const std::wstring& json);

// Removes credentials, signed URL queries, secret JSON values, and local
// absolute paths before diagnostic text is written to a durable manifest.
std::wstring RedactDocumentOcrSensitiveText(const std::wstring& text);
